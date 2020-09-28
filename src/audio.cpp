/****************************************************************************/
//    Copyright (C) 2009 Aali132                                            //
//    Copyright (C) 2018 quantumpencil                                      //
//    Copyright (C) 2018 Maxime Bacoux                                      //
//    Copyright (C) 2020 Julian Xhokaxhiu                                   //
//    Copyright (C) 2020 myst6re                                            //
//    Copyright (C) 2020 Chris Rizzitello                                   //
//    Copyright (C) 2020 John Pritchard                                     //
//                                                                          //
//    This file is part of FFNx                                             //
//                                                                          //
//    FFNx is free software: you can redistribute it and/or modify          //
//    it under the terms of the GNU General Public License as published by  //
//    the Free Software Foundation, either version 3 of the License         //
//                                                                          //
//    FFNx is distributed in the hope that it will be useful,               //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of        //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         //
//    GNU General Public License for more details.                          //
/****************************************************************************/

#include "audio.h"

#include "log.h"

NxAudioEngine nxAudioEngine;

// PRIVATE

void NxAudioEngine::getMusicFilenameFullPath(char* _out, char* _name)
{
	sprintf(_out, "%s/%s/%s.%s", basedir, external_music_path, _name, external_music_ext);
}

void NxAudioEngine::getVoiceFilenameFullPath(char* _out, char* _name)
{
	sprintf(_out, "%s/%s/%s.%s", basedir, external_voice_path, _name, external_voice_ext);
}

void NxAudioEngine::getSFXFilenameFullPath(char* _out, int _id)
{
	sprintf(_out, "%s/%s/%d.%s", basedir, external_sfx_path, _id, external_sfx_ext);
}

bool NxAudioEngine::fileExists(char* filename)
{
	struct stat dummy;

	bool ret = (stat(filename, &dummy) == 0);

	if (!ret) warning("Could not find file %s\n", __func__, filename);

	return ret;
}

// PUBLIC

bool NxAudioEngine::init()
{
	if (_engine.init() == 0)
	{
		_engineInitialized = true;

		if (he_bios_path != nullptr) {
			if (!Psf::initialize_psx_core(he_bios_path)) {
				error("NxAudioEngine::%s couldn't load %s, please verify 'he_bios_path' or comment it\n", __func__, he_bios_path);
			}
			else {
				_openpsf_loaded = true;
				info("NxAudioEngine::%s OpenPSF music plugin loaded using %s\n", __func__, he_bios_path);
			}
		}

		_sfxVolumePerChannels.resize(10, 1.0f);
		_sfxTempoPerChannels.resize(10, 1.0f);
		_sfxStreams.resize(1000, nullptr);

		while (!_sfxStack.empty())
		{
			loadSFX(_sfxStack.top());
			_sfxStack.pop();
		}

		return true;
	}

	return false;
}

void NxAudioEngine::flush()
{
	_engine.stopAll();

	_musicStack.empty();
	_musicHandle = NXAUDIOENGINE_INVALID_HANDLE;
	
	_voiceHandle = NXAUDIOENGINE_INVALID_HANDLE;
}

void NxAudioEngine::cleanup()
{
	_engine.deinit();
}

// SFX
bool NxAudioEngine::canPlaySFX(int id)
{
	struct stat dummy;

	char filename[MAX_PATH];

	getSFXFilenameFullPath(filename, id);

	return (stat(filename, &dummy) == 0);
}

void NxAudioEngine::loadSFX(int id)
{
	if (_engineInitialized)
	{
		if (_sfxStreams[id - 1] == nullptr)
		{
			char filename[MAX_PATH];

			getSFXFilenameFullPath(filename, id);

			if (trace_all || trace_sfx) trace("NxAudioEngine::%s: %s\n", __func__, filename);

			if (fileExists(filename))
			{
				SoLoud::Wav* sfx = new SoLoud::Wav();

				sfx->load(filename);

				_sfxStreams[id - 1] = sfx;
			}
		}
	}
	else
		_sfxStack.push(id);
}

void NxAudioEngine::unloadSFX(int id)
{
	if (_sfxStreams[id - 1] != nullptr)
	{
		delete _sfxStreams[id - 1];

		_sfxStreams[id - 1] = nullptr;
	}
}

void NxAudioEngine::playSFX(int id, int channel, float panning)
{
	if (_sfxStreams[id - 1] != nullptr)
	{
		SoLoud::handle _handle = _engine.play(
			*_sfxStreams[id - 1],
			_sfxVolumePerChannels[channel - 1],
			panning
		);

		_engine.setRelativePlaySpeed(_handle, _sfxTempoPerChannels[channel - 1]);
	}
}

void NxAudioEngine::setSFXVolume(float volume, int channel)
{
	_sfxVolumePerChannels[channel - 1] = volume;
}

void NxAudioEngine::setSFXSpeed(float speed, int channel)
{
	_sfxTempoPerChannels[channel - 1] = speed;
}

// Music
bool NxAudioEngine::canPlayMusic(char* name)
{
	struct stat dummy;

	char filename[MAX_PATH];

	getMusicFilenameFullPath(filename, name);

	return (stat(filename, &dummy) == 0);
}

void NxAudioEngine::playMusic(char* name, bool crossfade, uint32_t time)
{
	if (_engine.isValidVoiceHandle(_musicHandle)) stopMusic(crossfade ? time : 0);

	char filename[MAX_PATH];

	getMusicFilenameFullPath(filename, name);

	if (trace_all || trace_music) trace("NxAudioEngine::%s: %s\n", __func__, filename);

	if (fileExists(filename))
	{
		SoLoud::AudioSource* music = nullptr;

		if (_openpsf_loaded) {
			SoLoud::OpenPsf* openpsf = new SoLoud::OpenPsf();
			music = openpsf;

			if (openpsf->load(filename) != SoLoud::SO_NO_ERROR) {
				error("NxAudioEngine::%s: Cannot load %s with openpsf\n", __func__, filename);
				delete openpsf;
				music = nullptr;
			}
		}

		if (music == nullptr) {
			SoLoud::VGMStream* vgmstream = new SoLoud::VGMStream();
			music = vgmstream;
			if (vgmstream->load(filename) != SoLoud::SO_NO_ERROR) {
				error("NxAudioEngine::%s: Cannot load %s with vgmstream\n", __func__, filename);
			}
		}

		_musicHandle = _engine.playBackground(*music, crossfade ? 0.0f : 1.0f);
		if (crossfade) setMusicVolume(1.0f, time);
	}
}

void NxAudioEngine::stopMusic(uint32_t time)
{
	if (time > 0)
	{
		_engine.fadeVolume(_musicHandle, 0, time);
		_engine.scheduleStop(_musicHandle, time);
	}
	else
	{
		_engine.stop(_musicHandle);
	}
}

void NxAudioEngine::pauseMusic()
{
	_engine.setPause(_musicHandle, true);

	// Save for later usage
	_musicStack.push(_musicHandle);

	// Invalidate the current handle
	_musicHandle = NXAUDIOENGINE_INVALID_HANDLE;
}

void NxAudioEngine::resumeMusic()
{
	// Whatever is currently playing, just stop it
	// If the handle is still invalid, nothing will happen
	_engine.stop(_musicHandle);

	// Restore the last known paused music
	_musicHandle = _musicStack.top();
	_musicStack.pop();

	// Play it again from where it was left off
	_engine.setPause(_musicHandle, false);
}

bool NxAudioEngine::isMusicPlaying()
{
	return _engine.isValidVoiceHandle(_musicHandle);
}

void NxAudioEngine::setMusicMasterVolume(float volume, size_t time)
{
	_previousMusicMasterVolume = _musicMasterVolume;

	_musicMasterVolume = volume;

	resetMusicVolume(time);
}

void NxAudioEngine::restoreMusicMasterVolume(size_t time)
{
	if (_previousMusicMasterVolume != _musicMasterVolume)
	{
		_musicMasterVolume = _previousMusicMasterVolume;

		// Set them equally so if this API is called again, nothing will happen
		_previousMusicMasterVolume = _musicMasterVolume;

		resetMusicVolume(time);
	}
}

float NxAudioEngine::getMusicVolume()
{
	return _engine.getVolume(_musicHandle);
}

void NxAudioEngine::setMusicVolume(float volume, size_t time)
{	
	_wantedMusicVolume = volume;
	
	float _volume = volume * _musicMasterVolume;

	if (time > 0)
		_engine.fadeVolume(_musicHandle, _volume, time);
	else
		_engine.setVolume(_musicHandle, _volume);
}

void NxAudioEngine::resetMusicVolume(size_t time)
{
	float volume = _wantedMusicVolume * _musicMasterVolume;

	if (time > 0)
		_engine.fadeVolume(_musicHandle, volume, time);
	else
		_engine.setVolume(_musicHandle, volume);
}

void NxAudioEngine::setMusicSpeed(float speed)
{
	_engine.setRelativePlaySpeed(_musicHandle, speed);
}

void NxAudioEngine::setMusicLooping(bool looping)
{
	_engine.setLooping(_musicHandle, looping);
}

// Voice
bool NxAudioEngine::canPlayVoice(char* name)
{
	struct stat dummy;

	char filename[MAX_PATH];

	getVoiceFilenameFullPath(filename, name);

	return (stat(filename, &dummy) == 0);
}

void NxAudioEngine::playVoice(char* name)
{
	char filename[MAX_PATH];

	getVoiceFilenameFullPath(filename, name);

	if (trace_all || trace_voice) trace("NxAudioEngine::%s: %s\n", __func__, filename);

	if (fileExists(filename))
	{
		SoLoud::WavStream* voice = new SoLoud::WavStream();

		voice->load(filename);

		// Stop any previously playing voice
		if (_engine.isValidVoiceHandle(_voiceHandle)) _engine.stop(_voiceHandle);

		_voiceHandle = _engine.play(*voice);
	}
}

void NxAudioEngine::stopVoice(uint32_t time)
{
	if (time > 0)
	{
		_engine.fadeVolume(_voiceHandle, 0, time);
		_engine.scheduleStop(_voiceHandle, time);
	}
	else
	{
		_engine.stop(_voiceHandle);
	}
}