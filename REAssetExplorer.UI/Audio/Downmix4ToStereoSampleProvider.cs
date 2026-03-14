using NAudio.Wave;

namespace REAssetExplorer.UI.Audio;

public sealed class Downmix4ToStereoSampleProvider : ISampleProvider
{
    private readonly ISampleProvider _src;
    private float[] _buffer;

    public WaveFormat WaveFormat { get; }

    public Downmix4ToStereoSampleProvider(ISampleProvider source)
    {
        _src = source;

        // We're going to ignore this for now because we're not mixing anything, just ignoring extra channels
        /*if (source.WaveFormat.Channels != 4)
            throw new ArgumentException("Source must have 4 channels and not " + source.WaveFormat.Channels);*/

        WaveFormat = WaveFormat.CreateIeeeFloatWaveFormat(source.WaveFormat.SampleRate, 2);
        _buffer = new float[4096 * 4];
    }

    public int Read(float[] dest, int offset, int count)
    {
        int framesRequested = count / 2;
        int srcSamplesRequested = framesRequested * 4;

        if (_buffer.Length < srcSamplesRequested)
            Array.Resize(ref _buffer, srcSamplesRequested);

        int srcRead = _src.Read(_buffer, 0, srcSamplesRequested);
        if (srcRead == 0)
            return 0;

        int framesRead = srcRead / 4;

        // TODO: Allow channel mixing options, currently just takes front left and front right
        for (int i = 0; i < framesRead; i++)
        {
            dest[offset + i * 2 + 0] = _buffer[i * 4 + 0];
            dest[offset + i * 2 + 1] = _buffer[i * 4 + 1];
        }

        return framesRead * 2;
    }
}
