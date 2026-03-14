using System;
using System.IO;
using NAudio.Wave;
using NAudio.Vorbis;
using NAudio.Wave.SampleProviders;
using PlaybackState = NAudio.Wave.PlaybackState;

namespace REAssetExplorer.UI.Audio;

public class OggAudioPlayer : IDisposable
{
    private IWavePlayer? _waveOut;
    private VorbisWaveReader? _vorbisReader;
    private VolumeSampleProvider? _volumeProvider;
    private string? _tempOggFile;

    public event EventHandler? PlaybackStopped;

    public TimeSpan Duration => _vorbisReader?.TotalTime ?? TimeSpan.Zero;
    
    public TimeSpan Position
    {
        get => _vorbisReader?.CurrentTime ?? TimeSpan.Zero;
        set
        {
            if (_vorbisReader != null)
            {
                _vorbisReader.CurrentTime = value;
            }
        }
    }

    public double Volume
    {
        get => _volumeProvider?.Volume ?? 0.5f;
        set
        {
            if (_volumeProvider != null)
            {
                _volumeProvider.Volume = (float)Math.Clamp(value, 0.0, 1.0);
            }
        }
    }

    public bool IsPlaying => _waveOut?.PlaybackState == PlaybackState.Playing;

    public void LoadOgg(byte[] oggData)
    {
        Stop();

        try
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "REAssetExplorer_Audio");
            Directory.CreateDirectory(tempDir);
            _tempOggFile = Path.Combine(tempDir, $"wem_audio_{Guid.NewGuid()}.ogg");
            File.WriteAllBytes(_tempOggFile, oggData);

            _vorbisReader = new VorbisWaveReader(_tempOggFile);
            var sampleProvider = _vorbisReader.ToSampleProvider();
            
            ISampleProvider processedProvider = sampleProvider;
            if (sampleProvider.WaveFormat.Channels > 2)
            {
                processedProvider = new Downmix4ToStereoSampleProvider(sampleProvider);
            }
            else if (sampleProvider.WaveFormat.Channels == 1)
            {
                processedProvider = new MonoToStereoSampleProvider(sampleProvider);
            }
            
            _volumeProvider = new VolumeSampleProvider(processedProvider)
            {
                Volume = 0.5f
            };

            try
            {
                _waveOut = new WaveOutEvent
                {
                    DesiredLatency = 200,
                    NumberOfBuffers = 2
                };
                
                _waveOut.PlaybackStopped += OnPlaybackStopped;
                _waveOut.Init(_volumeProvider);
            }
            catch (Exception)
            {
                _waveOut = new DirectSoundOut(200);
                _waveOut.PlaybackStopped += OnPlaybackStopped;
                _waveOut.Init(_volumeProvider);
            }
        }
        catch (Exception)
        {
            Stop();
            throw;
        }
    }

    private void OnPlaybackStopped(object? sender, StoppedEventArgs e)
    {
        PlaybackStopped?.Invoke(this, EventArgs.Empty);
    }

    public void Play()
    {
        _waveOut?.Play();
    }

    public void Pause()
    {
        _waveOut?.Pause();
    }

    public void Stop()
    {
        if (_waveOut != null)
        {
            _waveOut.Stop();
            _waveOut.Dispose();
            _waveOut = null;
        }

        _volumeProvider = null;

        if (_vorbisReader != null)
        {
            _vorbisReader.Dispose();
            _vorbisReader = null;
        }
    }

    public void Dispose()
    {
        Stop();
        GC.SuppressFinalize(this);
    }
}
