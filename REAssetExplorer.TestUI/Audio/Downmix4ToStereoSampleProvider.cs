using System;
using NAudio.Wave;

namespace REAssetExplorer.TestUI.Audio;

/// <summary>
/// Strips extra channels by passing only front-left/front-right through. Wwise
/// 4.0 source layouts (FL, FR, BL, BR) sound acceptable this way without summing.
/// </summary>
public sealed class Downmix4ToStereoSampleProvider : ISampleProvider
{
    private readonly ISampleProvider _src;
    private float[] _buffer;

    public WaveFormat WaveFormat { get; }

    public Downmix4ToStereoSampleProvider(ISampleProvider source)
    {
        _src        = source;
        WaveFormat  = WaveFormat.CreateIeeeFloatWaveFormat(source.WaveFormat.SampleRate, 2);
        _buffer     = new float[4096 * 4];
    }

    public int Read(float[] dest, int offset, int count)
    {
        int framesRequested      = count / 2;
        int srcSamplesRequested  = framesRequested * 4;

        if (_buffer.Length < srcSamplesRequested)
            Array.Resize(ref _buffer, srcSamplesRequested);

        int srcRead = _src.Read(_buffer, 0, srcSamplesRequested);
        if (srcRead == 0) return 0;

        int framesRead = srcRead / 4;
        for (int i = 0; i < framesRead; i++)
        {
            dest[offset + i * 2 + 0] = _buffer[i * 4 + 0];
            dest[offset + i * 2 + 1] = _buffer[i * 4 + 1];
        }
        return framesRead * 2;
    }
}
