using System;
using System.IO;
using NAudio.Vorbis;
using NAudio.Wave;
using NAudio.Wave.SampleProviders;
using PlaybackState = NAudio.Wave.PlaybackState;

namespace REAssetExplorer.TestUI.Audio;

/// <summary>
/// Plays OGG/Vorbis data through NAudio with downmix to stereo when the source has
/// more channels. Caller can drive Play/Pause/Stop and set Position/Volume.
/// </summary>
public sealed class OggAudioPlayer : IDisposable
{
    private IWavePlayer?         _waveOut;
    private VorbisWaveReader?    _vorbisReader;
    private VolumeSampleProvider? _volumeProvider;
    private string?              _tempOggFile;

    public event EventHandler? PlaybackStopped;

    public TimeSpan Duration => _vorbisReader?.TotalTime ?? TimeSpan.Zero;

    public TimeSpan Position
    {
        get => _vorbisReader?.CurrentTime ?? TimeSpan.Zero;
        set { if (_vorbisReader != null) _vorbisReader.CurrentTime = value; }
    }

    public double Volume
    {
        get => _volumeProvider?.Volume ?? 0.5f;
        set { if (_volumeProvider != null) _volumeProvider.Volume = (float)Math.Clamp(value, 0.0, 1.0); }
    }

    public bool IsPlaying => _waveOut?.PlaybackState == PlaybackState.Playing;

    public void LoadOgg(byte[] oggData)
    {
        Stop();

        try
        {
            // VorbisWaveReader expects a file path; round-trip through %TEMP%.
            var tempDir = Path.Combine(Path.GetTempPath(), "REAssetExplorer_Audio");
            Directory.CreateDirectory(tempDir);
            _tempOggFile = Path.Combine(tempDir, $"wem_audio_{Guid.NewGuid()}.ogg");
            File.WriteAllBytes(_tempOggFile, oggData);

            _vorbisReader = new VorbisWaveReader(_tempOggFile);
            var sampleProvider = _vorbisReader.ToSampleProvider();

            ISampleProvider stereo = sampleProvider switch
            {
                { WaveFormat.Channels: > 2 } => new Downmix4ToStereoSampleProvider(sampleProvider),
                { WaveFormat.Channels: 1   } => new MonoToStereoSampleProvider(sampleProvider),
                _                            => sampleProvider,
            };

            _volumeProvider = new VolumeSampleProvider(stereo) { Volume = 0.5f };

            try
            {
                _waveOut = new WaveOutEvent { DesiredLatency = 200, NumberOfBuffers = 2 };
                _waveOut.PlaybackStopped += OnPlaybackStopped;
                _waveOut.Init(_volumeProvider);
            }
            catch
            {
                // WaveOutEvent isn't always available (no winmm.dll, etc.); fall back to DSound.
                _waveOut = new DirectSoundOut(200);
                _waveOut.PlaybackStopped += OnPlaybackStopped;
                _waveOut.Init(_volumeProvider);
            }
        }
        catch
        {
            Stop();
            throw;
        }
    }

    private void OnPlaybackStopped(object? sender, StoppedEventArgs e) =>
        PlaybackStopped?.Invoke(this, EventArgs.Empty);

    public void Play()  => _waveOut?.Play();
    public void Pause() => _waveOut?.Pause();

    public void Stop()
    {
        if (_waveOut != null)
        {
            _waveOut.Stop();
            _waveOut.Dispose();
            _waveOut = null;
        }
        _volumeProvider = null;
        _vorbisReader?.Dispose();
        _vorbisReader = null;
    }

    public void Dispose()
    {
        Stop();
        GC.SuppressFinalize(this);
    }
}
