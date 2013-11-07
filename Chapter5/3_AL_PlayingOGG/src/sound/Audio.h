#pragma once

#include "LAL.h"
#include "Thread.h"
#include <vector>

double Env_GetSeconds();
void Env_Sleep( int Milliseconds );

const int BUFFER_SIZE = 2 * 262144;

/// Provider of waveform data for streaming
class iWaveDataProvider: public iObject
{
public:
	iWaveDataProvider(): FChannels( 0 ),
		FSamplesPerSec( 0 ),
		FBitsPerSample( 0 ) {}

	virtual ubyte*                   GetWaveData() = 0;
	virtual size_t                   GetWaveDataSize() const = 0;

	virtual bool    IsEOF() const { return true; }
	virtual void    Seek( float Time ) {}
	virtual bool IsStreaming() const { return false; }
	virtual int                      StreamWaveData( int Size ) { return 0; }

	/// Format of waveform data
	ALuint GetALFormat() const
	{
		if ( FBitsPerSample == 8 ) { return ( FChannels == 2 ) ? AL_FORMAT_STEREO8  : AL_FORMAT_MONO8;  }

		if ( FBitsPerSample == 16 ) { return ( FChannels == 2 ) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16; }

		return AL_FORMAT_MONO8;
	}

	int    FChannels;
	int    FSamplesPerSec;
	int    FBitsPerSample;
};

/// Audio source interface, also directly used in silent mode
class AudioSource: public iObject
{
public:
	void Play()
	{
		if ( IsPlaying() ) { return; }

		if ( !FWaveDataProvider ) { return; }

		int State;
		alGetSourcei( FSourceID, AL_SOURCE_STATE, &State );

		if ( State != AL_PAUSED && FWaveDataProvider->IsStreaming() )
		{
			UnqueueAll();

			StreamBuffer( FBufferID[0], BUFFER_SIZE );
			StreamBuffer( FBufferID[1], BUFFER_SIZE );

			alSourceQueueBuffers( FSourceID, 2, &FBufferID[0] );
		}

		alSourcePlay( FSourceID );
	}

	void Stop()
	{
		alSourceStop( FSourceID );
	}

	void Pause()
	{
		alSourcePause( FSourceID );
		UnqueueAll();
	}

	void LoopSound( bool Loop )
	{
		alSourcei( FSourceID, AL_LOOPING, Loop ? 1 : 0 );
	}

	bool IsPlaying() const
	{
		int State;
		alGetSourcei( FSourceID, AL_SOURCE_STATE, &State );
		return State == AL_PLAYING;
	}

	int StreamBuffer( unsigned int BufferID, int Size )
	{
		int ActualSize = FWaveDataProvider->StreamWaveData( Size );

		ubyte* Data = FWaveDataProvider->GetWaveData();
		int Sz = ( int )FWaveDataProvider->GetWaveDataSize();

		alBufferData( BufferID, FWaveDataProvider->GetALFormat(), Data, Sz,
		              FWaveDataProvider->FSamplesPerSec );

		return ActualSize;
	}

	void Update( float DeltaSeconds )
	{
		if ( !FWaveDataProvider ) { return; }

		if ( !IsPlaying() ) { return; }

		if ( FWaveDataProvider->IsStreaming() )
		{
			int Processed;
			alGetSourcei( FSourceID, AL_BUFFERS_PROCESSED, &Processed );

			while ( Processed-- )
			{
				unsigned int BufID;
				alSourceUnqueueBuffers( FSourceID, 1, &BufID );

				StreamBuffer( BufID, BUFFER_SIZE );

				alSourceQueueBuffers( FSourceID, 1, &BufID );
			}
		}
	}

	AudioSource(): FWaveDataProvider( NULL ),
		FBuffersCount( 0 )
	{
		alGenSources( 1, &FSourceID );

		alSourcef( FSourceID, AL_GAIN,    1.0 );
		alSourcei( FSourceID, AL_LOOPING, 0   );
	}

	virtual ~AudioSource()
	{
		Stop();
		FWaveDataProvider = NULL;

		alDeleteSources( 1, &FSourceID );
		alDeleteBuffers( FBuffersCount, &FBufferID[0] );
	}

	void SetVolume( float Volume )
	{
		alSourcef( FSourceID, AL_GAIN, Volume );
	}

	void BindWaveform( clPtr<iWaveDataProvider> Wave )
	{
		FWaveDataProvider = Wave;

		if ( !Wave ) { return; }

		if ( FWaveDataProvider->IsStreaming() )
		{
			FBuffersCount = 2;
			alGenBuffers( FBuffersCount, &FBufferID[0] );
		}
		else
		{
			FBuffersCount = 1;

			alGenBuffers( FBuffersCount, &FBufferID[0] );
			alBufferData( FBufferID[0],
			              FWaveDataProvider->GetALFormat(),
			              FWaveDataProvider->GetWaveData(),
			              ( int )FWaveDataProvider->GetWaveDataSize(),
			              FWaveDataProvider->FSamplesPerSec );

			alSourcei( FSourceID, AL_BUFFER, FBufferID[0] );
		}
	}

private:
	void   UnqueueAll()
	{
		int Queued;
		alGetSourcei( FSourceID, AL_BUFFERS_QUEUED, &Queued );

		if ( Queued > 0 )
		{
			alSourceUnqueueBuffers( FSourceID, Queued, &FBufferID[0] );
		}
	}

	clPtr<iWaveDataProvider> FWaveDataProvider;
private:
	unsigned int FSourceID;
	unsigned int FBufferID[2];
	int      FBuffersCount;
};

/// Manages OpenAL in a separate thread
class AudioThread: public iThread
{
public:
	AudioThread(): FDevice( NULL ), FContext( NULL ), FInitialized( false ) {}
	virtual ~AudioThread() {}

	virtual void Run()
	{
		if ( !LoadAL() ) { return; }

		// We should use actual device name if the default does not work
		FDevice = alcOpenDevice( NULL );

		FContext = alcCreateContext( FDevice, NULL );

		alcMakeContextCurrent( FContext );

		FInitialized = true;

		FPendingExit = false;

		while ( !IsPendingExit() ) { Env_Sleep( 100 ); }

		alcDestroyContext( FContext );
		alcCloseDevice( FDevice );

		UnloadAL();
	}

	bool FInitialized;
private:
	ALCdevice*     FDevice;
	ALCcontext*    FContext;
};
