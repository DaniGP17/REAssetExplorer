using NAudio.Wave;

namespace REAssetExplorer.UI.Audio;

/// <summary>
/// Converts multi-channel audio to stereo by mixing channels
/// </summary>
public class MultichannelToStereoSampleProvider : ISampleProvider
{
    private readonly ISampleProvider _source;
    private readonly WaveFormat _waveFormat;
    private readonly int _sourceChannels;
    
    public MultichannelToStereoSampleProvider(ISampleProvider source)
    {
        if (source.WaveFormat.Channels < 2)
        {
            throw new ArgumentException("Source must have at least 2 channels");
        }
        
        _source = source;
        _sourceChannels = source.WaveFormat.Channels;
        
        // Create stereo wave format with same sample rate
        _waveFormat = WaveFormat.CreateIeeeFloatWaveFormat(source.WaveFormat.SampleRate, 2);
    }
    
    public WaveFormat WaveFormat => _waveFormat;
    
    public int Read(float[] buffer, int offset, int count)
    {
        int sourceSamplesRequired = (count / 2) * _sourceChannels;
        float[] sourceBuffer = new float[sourceSamplesRequired];
        
        int sourceSamplesRead = _source.Read(sourceBuffer, 0, sourceSamplesRequired);
        int framesRead = sourceSamplesRead / _sourceChannels;
        
        int outIndex = offset;
        int sourceIndex = 0;
        
        for (int frame = 0; frame < framesRead; frame++)
        {
            float left = 0f;
            float right = 0f;
            
            if (_sourceChannels == 4)
            {
                // 4.0 layout típico: FL, FR, BL, BR
                float frontLeft = sourceBuffer[sourceIndex];
                float frontRight = sourceBuffer[sourceIndex + 1];
                float backLeft = sourceBuffer[sourceIndex + 2];
                float backRight = sourceBuffer[sourceIndex + 3];
                
                left = (frontLeft + backLeft) * 0.5f;
                right = (frontRight + backRight) * 0.5f;
            }
            else if (_sourceChannels == 3)
            {
                // 3.0 layout: L, R, C
                left = (sourceBuffer[sourceIndex] + sourceBuffer[sourceIndex + 2] * 0.5f);
                right = (sourceBuffer[sourceIndex + 1] + sourceBuffer[sourceIndex + 2] * 0.5f);
            }
            else if (_sourceChannels == 6)
            {
                // 5.1 layout: FL, FR, FC, LFE, BL, BR
                float frontLeft = sourceBuffer[sourceIndex];
                float frontRight = sourceBuffer[sourceIndex + 1];
                float center = sourceBuffer[sourceIndex + 2];
                float lfe = sourceBuffer[sourceIndex + 3];
                float backLeft = sourceBuffer[sourceIndex + 4];
                float backRight = sourceBuffer[sourceIndex + 5];
                
                left = frontLeft + (center * 0.707f) + (backLeft * 0.707f) + (lfe * 0.5f);
                right = frontRight + (center * 0.707f) + (backRight * 0.707f) + (lfe * 0.5f);
                
                // Normalize to prevent clipping
                float max = Math.Max(Math.Abs(left), Math.Abs(right));
                if (max > 1.0f)
                {
                    left /= max;
                    right /= max;
                }
            }
            else
            {
                left = sourceBuffer[sourceIndex];
                right = sourceBuffer[sourceIndex + 1];
                
                for (int ch = 2; ch < _sourceChannels; ch++)
                {
                    float sample = sourceBuffer[sourceIndex + ch] * 0.3f;
                    if (ch % 2 == 0)
                        left += sample;
                    else
                        right += sample;
                }
            }
            
            buffer[outIndex++] = left;
            buffer[outIndex++] = right;
            sourceIndex += _sourceChannels;
        }
        
        return framesRead * 2;
    }
}
