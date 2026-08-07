#include "NativeMovie.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>

#include "Explorer/SceneBuilder.h"

using Microsoft::WRL::ComPtr;

// Exported by mfplat but missing from the MinGW headers.
extern "C" HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream* stream, IMFByteStream** byteStream);

namespace {

void Check(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return;
    char text[160];
    std::snprintf(text, sizeof(text), "movie: %s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    throw std::runtime_error(text);
}

class MediaFoundationScope {
public:
    MediaFoundationScope() {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            if (com) CoUninitialize();
            Check(hr, "MFStartup");
        }
    }
    ~MediaFoundationScope() {
        MFShutdown();
        if (com) CoUninitialize();
    }

    MediaFoundationScope(const MediaFoundationScope&) = delete;
    MediaFoundationScope& operator=(const MediaFoundationScope&) = delete;

private:
    bool com = false;
};

class PakStream final : public IStream {
public:
    PakStream(const std::filesystem::path& pak, uint64_t base, uint64_t size) : base(base), size(size) {
        file = CreateFileW(pak.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("movie: cannot open " + pak.string());
    }
    explicit PakStream(std::shared_ptr<const std::vector<uint8_t>> data) : size(data->size()), memory(std::move(data)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISequentialStream) || IsEqualIID(riid, IID_IStream)) {
            *out = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&refs);
        if (left == 0) delete this;
        return static_cast<ULONG>(left);
    }

    HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG count, ULONG* read) override {
        uint64_t wanted = position < size ? std::min<uint64_t>(count, size - position) : 0;
        DWORD got = 0;
        if (memory) {
            std::memcpy(buffer, memory->data() + position, wanted);
            got = static_cast<DWORD>(wanted);
        } else if (wanted > 0) {
            OVERLAPPED at{};
            uint64_t offset = base + position;
            at.Offset = static_cast<DWORD>(offset);
            at.OffsetHigh = static_cast<DWORD>(offset >> 32);
            if (!ReadFile(file, buffer, static_cast<DWORD>(wanted), &got, &at)) return HRESULT_FROM_WIN32(GetLastError());
        }
        position += got;
        if (read) *read = got;
        return got == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* result) override {
        int64_t from = origin == STREAM_SEEK_SET ? 0
            : origin == STREAM_SEEK_CUR ? static_cast<int64_t>(position) : static_cast<int64_t>(size);
        int64_t target = from + move.QuadPart;
        if (target < 0) return STG_E_INVALIDFUNCTION;
        position = static_cast<uint64_t>(target);
        if (result) result->QuadPart = position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (stat == nullptr) return STG_E_INVALIDPOINTER;
        *stat = STATSTG{};
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = size;
        stat->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

private:
    ~PakStream() {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }

    LONG refs = 1;
    HANDLE file = INVALID_HANDLE_VALUE;
    uint64_t base = 0;
    uint64_t size = 0;
    uint64_t position = 0;
    std::shared_ptr<const std::vector<uint8_t>> memory;
};

struct MovieSource {
    MovieSource(const LoadedGame& game, const std::string& pakPath) {
        path = StreamingCopyPath(game, pakPath);
        stored = game.FindStored(path);
        if (!stored) memory = std::make_shared<const std::vector<uint8_t>>(game.ExtractFile(path));
        size = stored ? stored->size : memory->size();
        char magic[4]{};
        ComPtr<IStream> probe = Open();
        ULONG read = 0;
        probe->Read(magic, 4, &read);
        // The non-streaming copy of a streamed movie is a stub naming "dummy.mp4".
        if (read == 4 && std::memcmp(magic, "REMV", 4) == 0) {
            throw std::runtime_error("placeholder movie with no streaming copy: " + pakPath);
        }
    }

    ComPtr<IStream> Open() const {
        ComPtr<IStream> stream;
        stream.Attach(stored ? new PakStream(stored->pak, stored->offset, stored->size) : new PakStream(memory));
        return stream;
    }

    ComPtr<IMFSourceReader> CreateReader(bool videoProcessing) const {
        ComPtr<IMFByteStream> bytes;
        Check(MFCreateMFByteStreamOnStream(Open().Get(), &bytes), "byte stream");
        ComPtr<IMFAttributes> attributes;
        Check(MFCreateAttributes(&attributes, 1), "attributes");
        // Converts the decoder's YUV output to RGB32.
        if (videoProcessing) attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        ComPtr<IMFSourceReader> reader;
        Check(MFCreateSourceReaderFromByteStream(bytes.Get(), attributes.Get(), &reader), "source reader");
        return reader;
    }

    std::string path;
    std::optional<StoredFile> stored;
    std::shared_ptr<const std::vector<uint8_t>> memory;
    uint64_t size = 0;
};

std::string FourCC(uint32_t code) {
    std::string text;
    for (int i = 0; i < 4; i++) {
        char c = static_cast<char>((code >> (i * 8)) & 0xFF);
        text += c >= 0x20 && c < 0x7F ? c : '?';
    }
    return text;
}

std::string VideoCodecName(const GUID& subtype) {
    std::string code = FourCC(subtype.Data1);
    if (code == "WVC1") return "VC-1 (WVC1)";
    if (code == "WMV3") return "Windows Media Video 9 (WMV3)";
    if (code == "H264") return "H.264";
    return code;
}

std::string AudioCodecName(const GUID& subtype) {
    char tag[16];
    std::snprintf(tag, sizeof(tag), "0x%04lX", static_cast<unsigned long>(subtype.Data1));
    switch (subtype.Data1) {
        case 0x0001: return std::string("PCM (") + tag + ")";
        case 0x0003: return std::string("PCM float (") + tag + ")";
        case 0x0055: return std::string("MP3 (") + tag + ")";
        case 0x0161: return std::string("Windows Media Audio 2 (") + tag + ")";
        case 0x0162: return std::string("Windows Media Audio Pro (") + tag + ")";
        case 0x0163: return std::string("Windows Media Audio Lossless (") + tag + ")";
        case 0x1610: return std::string("AAC (") + tag + ")";
        default: return tag;
    }
}

MovieInfo Describe(IMFSourceReader* reader, const MovieSource& source) {
    MovieInfo info;
    info.source = source.path;
    info.bytes = source.size;
    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration))) {
        info.duration = static_cast<double>(duration.uhVal.QuadPart) / 1e7;
    }
    PropVariantClear(&duration);
    for (DWORD stream = 0;; stream++) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(stream, 0, &type))) break;
        GUID major{};
        GUID subtype{};
        type->GetGUID(MF_MT_MAJOR_TYPE, &major);
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (major == MFMediaType_Video && info.videoCodec.empty()) {
            info.videoCodec = VideoCodecName(subtype);
            MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &info.width, &info.height);
            UINT32 numerator = 0;
            UINT32 denominator = 0;
            MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
            info.frameRate = denominator ? static_cast<double>(numerator) / denominator : 0;
        } else if (major == MFMediaType_Audio && info.audioCodec.empty()) {
            info.audioCodec = AudioCodecName(subtype);
            info.sampleRate = MFGetAttributeUINT32(type.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
            info.channels = MFGetAttributeUINT32(type.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
        }
    }
    if (info.videoCodec.empty()) throw std::runtime_error("movie: no video stream in " + source.path);
    return info;
}

ComPtr<IMFMediaType> OutputType(const GUID& major, const GUID& subtype) {
    ComPtr<IMFMediaType> type;
    Check(MFCreateMediaType(&type), "media type");
    type->SetGUID(MF_MT_MAJOR_TYPE, major);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    return type;
}

void SelectOnly(IMFSourceReader* reader, DWORD stream) {
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(stream, TRUE);
}

}

MovieInfo ReadMovieInfo(const LoadedGame& game, const std::string& pakPath) {
    MediaFoundationScope scope;
    MovieSource source(game, pakPath);
    ComPtr<IMFSourceReader> reader = source.CreateReader(false);
    return Describe(reader.Get(), source);
}

MoviePlayer::MoviePlayer(const LoadedGame& game, const std::string& pakPath) {
    Check(MFStartup(MF_VERSION), "MFStartup");
    try {
        MovieSource source(game, pakPath);
        video = source.CreateReader(true);
        info = Describe(video.Get(), source);
        SelectOnly(video.Get(), static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM));
        Check(video->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
                                         OutputType(MFMediaType_Video, MFVideoFormat_RGB32).Get()), "RGB32 output");
        ComPtr<IMFMediaType> current;
        Check(video->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current), "video type");
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &info.width, &info.height);
        UINT32 defaultStride = 0;
        stride = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &defaultStride))
            ? static_cast<int32_t>(defaultStride) : static_cast<int32_t>(info.width * 4);
        frame.assign(static_cast<std::size_t>(info.width) * info.height * 4, 0);

        if (!info.audioCodec.empty()) {
            ComPtr<IMFSourceReader> sound = source.CreateReader(false);
            SelectOnly(sound.Get(), static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM));
            Check(sound->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
                                             OutputType(MFMediaType_Audio, MFAudioFormat_Float).Get()), "float output");
            ComPtr<IMFMediaType> pcm;
            Check(sound->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &pcm), "audio type");
            uint32_t rate = MFGetAttributeUINT32(pcm.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
            uint32_t channels = MFGetAttributeUINT32(pcm.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
            // A second of slack: the container duration is not sample exact.
            auto capacity = static_cast<uint64_t>((info.duration + 1.0) * rate);
            audio = std::make_unique<AudioPlayer>();
            audio->PlayStream(rate, channels, capacity);
            audioThread = std::thread(&MoviePlayer::DecodeAudio, this, std::move(sound), capacity);
        }
    } catch (...) {
        pending.Reset();
        video.Reset();
        MFShutdown();
        throw;
    }
}

MoviePlayer::~MoviePlayer() {
    stopAudio = true;
    if (audioThread.joinable()) audioThread.join();
    audio.reset();
    pending.Reset();
    video.Reset();
    MFShutdown();
}

void MoviePlayer::DecodeAudio(ComPtr<IMFSourceReader> reader, uint64_t capacity) {
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    ComPtr<IMFMediaType> pcm;
    uint32_t channels = 0;
    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &pcm))) {
        channels = MFGetAttributeUINT32(pcm.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    }
    float* out = audio->StreamData();
    uint64_t written = 0;
    while (channels > 0 && !stopAudio.load() && written < capacity) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr,
                                      &sample)) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            break;
        }
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) continue;
        uint64_t frames = std::min<uint64_t>(length / (sizeof(float) * channels), capacity - written);
        std::memcpy(out + written * channels, data, frames * channels * sizeof(float));
        buffer->Unlock();
        written += frames;
        audio->Publish(written, false);
    }
    audio->Publish(written, true);
    pcm.Reset();
    reader.Reset();
    if (com) CoUninitialize();
}

double MoviePlayer::Position() const {
    if (!audio) return clock;
    double position = 0;
    double duration = 0;
    audio->Status(position, duration);
    return position;
}

void MoviePlayer::SetPaused(bool value) {
    if (!value && paused && Position() >= info.duration - 0.05) Seek(0);
    paused = value;
    if (audio) audio->SetPaused(value);
}

void MoviePlayer::Seek(double seconds) {
    seconds = std::clamp(seconds, 0.0, std::max(info.duration, 0.0));
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = static_cast<LONGLONG>(seconds * 1e7);
    video->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    pending.Reset();
    frameTime = -1;
    videoEnded = false;
    clock = seconds;
    if (audio) audio->Seek(seconds);
}

bool MoviePlayer::NextVideoSample() {
    while (!videoEnded) {
        DWORD flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(video->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &time,
                                     &sample)) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            videoEnded = true;
            break;
        }
        if (!sample) continue;
        pending = sample;
        pendingTime = time;
        return true;
    }
    return false;
}

void MoviePlayer::TakeFrame(IMFSample* sample, int64_t time) {
    std::size_t rowBytes = static_cast<std::size_t>(info.width) * 4;
    ComPtr<IMFMediaBuffer> buffer;
    if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer))) {
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* scanline = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scanline, &pitch))) {
            for (uint32_t y = 0; y < info.height; y++) std::memcpy(frame.data() + y * rowBytes, scanline + y * pitch, rowBytes);
            buffer2d->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                // A negative stride means bottom-up rows.
                std::ptrdiff_t step = stride;
                BYTE* first = step < 0 ? data + (info.height - 1) * static_cast<std::size_t>(-step) : data;
                for (uint32_t y = 0; y < info.height; y++) {
                    BYTE* row = first + static_cast<std::ptrdiff_t>(y) * step;
                    if (row < data || row + rowBytes > data + length) break;
                    std::memcpy(frame.data() + y * rowBytes, row, rowBytes);
                }
                buffer->Unlock();
            }
        }
    }
    frameTime = time;
}

bool MoviePlayer::Update(float dt) {
    if (!paused && !audio) clock = std::min(clock + dt, info.duration);
    double now = Position();
    bool ended = now >= info.duration - 0.05;
    if (audio && !paused) {
        // The audio track may end a little before the container says.
        double position = 0;
        double duration = 0;
        ended = ended || audio->Status(position, duration) == AudioPlayer::State::Stopped;
    }
    if (!paused && ended) {
        paused = true;
        if (audio) audio->SetPaused(true);
    }
    auto target = static_cast<int64_t>(now * 1e7);
    ComPtr<IMFSample> due;
    int64_t dueTime = 0;
    while (!videoEnded) {
        if (!pending && !NextVideoSample()) break;
        if ((frameTime >= 0 || due) && pendingTime > target) break;
        // MinGW's ComPtr move leaves the source set; hand over and clear explicitly.
        due = pending;
        pending.Reset();
        dueTime = pendingTime;
    }
    if (!due) return false;
    TakeFrame(due.Get(), dueTime);
    return true;
}
