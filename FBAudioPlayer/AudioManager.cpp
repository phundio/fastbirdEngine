/*
 -----------------------------------------------------------------------------
 This source file is part of fastbird engine
 For the latest info, see http://www.jungwan.net/
 
 Copyright (c) 2013-2015 Jungwan Byun
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 -----------------------------------------------------------------------------
*/

#include "stdafx.h"
#include "AudioManager.h"
#include "AudioHelper.h"
#include "SmoothGain.h"
#include "AudioEx.h"
#include "AudioSourceStatus.h"
#include "FBCommonHeaders/VectorMap.h"
#include "FBCommonHeaders/Helpers.h"
using namespace fb;

AudioId NextAudioId = 1;
namespace fb{
	static void eos_callback(void *userData, ALuint source);
}
FB_DECLARE_SMART_PTR_STRUCT(AudioBuffer);
struct AudioBuffer{
	std::string mFilepath;
	INT64 mLastAccessed;
	ALuint mBuffer;
	unsigned mReferences;
	TIME_PRECISION mLength;

	AudioBuffer()
		: mLastAccessed(0)
, mBuffer(0)
, mReferences(0)
, mLength(0)
{

}
~AudioBuffer(){
	alDeleteBuffers(1, &mBuffer);
}
};

void CheckALError(){
	auto error = alGetError();
	if (error){
		Logger::Log(FB_ERROR_LOG_ARG, FormatString("Open al error code : 0x%x", error).c_str());
	}
}

FB_DECLARE_SMART_PTR_STRUCT(AudioSource);
struct AudioSource{
private:
	AudioId mAudioId;
	AudioBufferPtr mAudioBuffer;
	ALuint mALSource;
	AudioProperty mProperty;
	float mPlayingTime;
	AudioSourceStatus::Enum mStatus;

public:

	AudioSource()
		: mALSource(-1)
		, mAudioId(INVALID_AUDIO_ID)
		, mPlayingTime(0)
		, mStatus(AudioSourceStatus::Waiting)
	{
	}
	~AudioSource(){
		if (mAudioBuffer)
			mAudioBuffer->mReferences--;
	}

	void SetAudioId(AudioId id){
		mAudioId = id;
	}
	const AudioId& GetAudioId() const{
		return mAudioId;
	}

	void SetAudioBuffer(AudioBufferPtr buffer){
		if (mAudioBuffer)
			mAudioBuffer->mReferences--;
		mAudioBuffer = buffer;
		buffer->mReferences++;
	}

	AudioBufferPtr GetAudioBuffer() const{
		return mAudioBuffer;
	}

	void SetALAudioSource(ALuint src){
		mALSource = src;
	}
	ALuint GetALAudioSource() const {
		return mALSource;
	}

	void SetStatus(AudioSourceStatus::Enum status){
		mStatus = status;
	}

	AudioSourceStatus::Enum GetStatus(){
		return mStatus;
	}

	void SetProperty(const AudioProperty& prop){
		mProperty = prop;
	}

	void ApplyProp(){
		if (mALSource != -1){
			alSource3f(mALSource, AL_POSITION, std::get<0>(mProperty.mPosition),
				std::get<1>(mProperty.mPosition), std::get<2>(mProperty.mPosition));
			alSourcei(mALSource, AL_SOURCE_RELATIVE, mProperty.mRelative ? AL_TRUE : AL_FALSE);
			alSourcef(mALSource, AL_REFERENCE_DISTANCE, mProperty.mReferenceDistance);
			alSourcef(mALSource, AL_ROLLOFF_FACTOR, mProperty.mRolloffFactor);
			alSourcef(mALSource, AL_GAIN, mProperty.mGain);
			CheckALError();
		}
	}

	void ApplyRemainedTime(){
		if (mALSource != -1){
			alSourcef(mALSource, AL_SEC_OFFSET, mPlayingTime);
			CheckALError();
		}
	}

	bool IsPlaying() const {
		return mStatus == AudioSourceStatus::Playing;
	}

	bool Update(float dt){
		mPlayingTime += dt;
		if (mStatus == AudioSourceStatus::Waiting && mPlayingTime >= mAudioBuffer->mLength)
			return true; // delete me

		return false; // dont delete me.
	}

	void SetPosition(float x, float y, float z){
		mProperty.mPosition = std::make_tuple(x, y, z);
		if (mALSource!=-1){
			alSource3f(mALSource, AL_POSITION, x, y, z);
			CheckALError();
		}
	}

	Vec3Tuple GetPosition() const{
		return mProperty.mPosition;
	}
	void SetRelative(bool relative){
		mProperty.mRelative = relative;
		if (mALSource != -1){
			alSourcei(mALSource, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
			CheckALError();
		}
	}

	bool GetRelative() const {
		return mProperty.mRelative;
	}

	void SetReferenceDistance(float distance){
		mProperty.mReferenceDistance = distance;
		if (mALSource != -1){
			alSourcef(mALSource, AL_REFERENCE_DISTANCE, distance);
			CheckALError();
		}
	}

	float GetReferenceDistance(){
		return mProperty.mReferenceDistance;
	}

	void SetRollOffFactor(float factor){
		mProperty.mRolloffFactor = factor;
		if (mALSource != -1){
			alSourcef(mALSource, AL_ROLLOFF_FACTOR, factor);
			CheckALError();
		}
	}

	void SetOffsetInSec(float sec){
		mPlayingTime = sec;
		if (mALSource != -1){
			alSourcef(mALSource, AL_SEC_OFFSET, mPlayingTime);
			CheckALError();
		}
	}

	void SetGain(float gain){
		mProperty.mGain = gain;
		if (mALSource != -1){
			alSourcef(mALSource, AL_GAIN, gain);
			CheckALError();
		}
	}

	float GetGain() const{
		return mProperty.mGain;
	}

	float GetLeftTime() const{
		return mAudioBuffer->mLength - mPlayingTime;
	}

	float GetLength() const {
		return mAudioBuffer->mLength;
	}
};

FunctionId NextCallbackId = 1;
//---------------------------------------------------------------------------
class AudioManager::Impl{
public:
	static const int MaximumAudioSources = 24;
	static const int NumReservedAudioSources = 8;
	static const unsigned NumSortPerFrame = 10;
	ALCdevice* mDevice;
	ALCcontext* mContext;
	static LPALGETSOURCEDVSOFT alGetSourcedvSOFT;
	VectorMap<std::string, AudioBufferPtr> mAudioBuffers;	
	// source plyaing audio id
	std::stack<ALuint> mALSources;	
	// audioId - source index
	typedef VectorMap<AudioId, AudioSourcePtr> AudioSources;
	AudioSources mAudioSources;
	VectorMap<float, AudioId> mAudioSourcesSorted;	
	// Source, Buffer	
	VectorMap<AudioId, std::vector<IAudioManipulatorPtr> > mAudioManipulators;
	std::vector<AudioExPtr> mAudioExs;	

	typedef VectorMap<FunctionId, CallbackFunction> CallbackMap;
	typedef VectorMap<AudioId, std::vector<FunctionId> > CallbackIdMap;
	CallbackMap mEndCallbacks;
	CallbackIdMap mEndCallbackIds;
	Vec3Tuple mListenerPos;	
	int mNumPlaying;
	int mNumGeneratedSources;

	//---------------------------------------------------------------------------
	Impl()
		: mDevice(0)
		, mContext(0)
		, mNumPlaying(0)
		, mNumGeneratedSources(0)
	{
		mListenerPos = std::make_tuple(0.f, 0.f, 0.f);
	}

	~Impl(){
		// use deinit()
	}

	bool Init(){
		mDevice = alcOpenDevice(NULL);
		if (!mDevice){
			Error("No sound device found!");
			return false;
		}

		mContext = alcCreateContext(mDevice, NULL);
		if (!mContext){
			Error("Cannot create the audio context!");
			return false;
		}

		if (alcMakeContextCurrent(mContext) == ALC_FALSE)
		{
			Error("Could not make context current!\n");
			return false;
		}

		if (!alIsExtensionPresent("AL_SOFT_source_length"))
		{
			Error("Required AL_SOFT_source_length not supported!\n");			
		}

		if (!alIsExtensionPresent("AL_SOFT_source_latency"))
			Error("AL_SOFT_source_latency not supported, audio may be a bit laggy.");
		else
		{
			alGetSourcedvSOFT = (LPALGETSOURCEDVSOFT)alGetProcAddress("alGetSourcedvSOFT");
		}
		for (int i = 0; i < MaximumAudioSources; ++i){
			ALuint src;
			alGenSources(1, &src);
			if (alGetError() != AL_NO_ERROR){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot create audio source idx(%d).", i).c_str());
			}
			else{
				++mNumGeneratedSources;
				mALSources.push(src);
			}
		}
		Log("OpenAL initialized!");
		return true;
	}

	void Deinit(){
		mAudioExs.clear();

		while (!mAudioSources.empty()){
			auto it = mAudioSources.end() - 1;
			if (it->second->GetALAudioSource() != -1)
				StopAudio(it->second->GetAudioId());
		}		
		assert(mALSources.size() == mNumGeneratedSources);
		while (!mALSources.empty()){
			auto src = mALSources.top();
			mALSources.pop();
			alDeleteSources(1, &src);
		}
		mAudioBuffers.clear();	

		alcMakeContextCurrent(NULL);
		alcDestroyContext(mContext);
		alcCloseDevice(mDevice);
		mDevice = 0;
	}
	
	Real GetDistanceSQ(const Vec3Tuple& a, const Vec3Tuple& b){
		float xgap = std::get<0>(a) - std::get<0>(b);
		float ygap = std::get<1>(a) - std::get<1>(b);
		float zgap = std::get<2>(a) - std::get<2>(b);
		return xgap * xgap + ygap * ygap + zgap * zgap;
	}

	float GetLengthSQ(const Vec3Tuple& a){
		float x = std::get<0>(a);
		float y = std::get<0>(a);
		float z = std::get<0>(a);
		return x * x + y * y + z * z;
	}

	void DropAudio(){
		int numDropping = 0;
		mAudioSourcesSorted.clear();
		int desiredDrop = mNumPlaying - MaximumAudioSources;
		if (desiredDrop <= 0)
			return;
		for (auto& it : mAudioSources){
			if (it.second->GetStatus() == AudioSourceStatus::Playing){
				auto dist = it.second->GetRelative() ?
					GetLengthSQ(it.second->GetPosition()):
					GetDistanceSQ(mListenerPos, it.second->GetPosition());
				mAudioSourcesSorted[dist / it.second->GetReferenceDistance()] = it.second->GetAudioId();
			}
			else if (it.second->GetStatus() == AudioSourceStatus::Dropping){
				++numDropping;
			}
		}
		while (numDropping < desiredDrop){
			auto it = mAudioSourcesSorted.end() - 1;
			auto source = mAudioSources[it->second];
			source->SetStatus(AudioSourceStatus::Dropping);
			StopWithFadeOut(it->second, 0.1f);
			++numDropping;			
		}
	}


	bool PlayWaitingAudio(AudioSourcePtr audioSource, ALuint src, TIME_PRECISION fadeInTime){
		assert(audioSource->GetLeftTime() > 0.f);
		assert(audioSource->GetStatus() == AudioSourceStatus::Waiting);
		assert(src != -1);
		alSourcei(src, AL_BUFFER, audioSource->GetAudioBuffer()->mBuffer);
		CheckALError();
		if (alurePlaySource(src, eos_callback, (void*)audioSource->GetAudioId()) == AL_FALSE){
			CheckALError();
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot play AudioSource(%u, %s)",
				audioSource->GetAudioId(), audioSource->GetAudioBuffer()->mFilepath.c_str()).c_str());
			audioSource->SetStatus(AudioSourceStatus::Waiting);
			return false;
		}
		else{
			CheckALError();
			audioSource->SetALAudioSource(src);
			audioSource->SetStatus(AudioSourceStatus::Playing);
			++mNumPlaying;
			audioSource->ApplyProp();
			audioSource->ApplyRemainedTime();
			auto audioId = audioSource->GetAudioId();
			auto fadeIn = SmoothGain::Create(audioId, 
				1.0f / std::min(fadeInTime, audioSource->GetLeftTime()*.2f), 0.f, 
				audioSource->GetGain());
			mAudioManipulators[audioId].push_back(fadeIn);
			return true;
		}
	}

	void CheckReserved(){		
		if (mALSources.empty())
			return;
		if (mAudioSources.size() < MaximumAudioSources)
			return;
		VectorMap<float, AudioId> sorted;
		for (auto& it : mAudioSources){
			if (it.second->GetStatus() == AudioSourceStatus::Waiting){
				auto dist = it.second->GetRelative() ?
					GetLengthSQ(it.second->GetPosition()) :
					GetDistanceSQ(mListenerPos, it.second->GetPosition());
				sorted[dist / it.second->GetReferenceDistance()] = it.second->GetAudioId();
			}
		}
		for (auto& itSorted : sorted){
			auto source = mAudioSources[itSorted.second];
			auto alsrc = mALSources.top();				
			if (PlayWaitingAudio(source, alsrc, 0.5f)){
				mALSources.pop();				
				if (mNumPlaying > MaximumAudioSources){
					DropAudio();
				}
			}
			if (mALSources.empty())
				break;
		}
	}

	void Update(TIME_PRECISION dt){		
		for (auto itMan = mAudioManipulators.begin(); itMan != mAudioManipulators.end(); /**/){
			for (auto it = itMan->second.begin(); it != itMan->second.end(); /**/)
			{
				bool finished = (*it)->Update(dt);
				if (finished){
					auto sourceIt = mAudioSources.Find((*it)->GetAudioId());
					if (sourceIt != mAudioSources.end()){
						if (sourceIt->second->GetGain() == 0.f){							
							StopAudio(sourceIt->second->GetAudioId());
						}
					}
					it = itMan->second.erase(it);
				}
				else{
					++it;
				}
			}
			if (itMan->second.empty()){
				itMan = mAudioManipulators.erase(itMan);
			}
			else{
				++itMan;
			}
		}
		for (auto it = mAudioExs.begin(); it != mAudioExs.end(); /**/){
			bool finished = (*it)->Update();
			if (finished){
				it = mAudioExs.erase(it);
			}
			else{
				++it;
			}
		}
		for (auto it = mAudioSources.begin(); it != mAudioSources.end(); /**/){
			if (it->second->Update(dt)){
				OnPlayFinishedInternal(it->second->GetAudioId());
				it = mAudioSources.erase(it);
			}
			else{
				++it;
			}
		}
		if (mAudioSources.empty())
			return;
		
		static TIME_PRECISION accumulator = 0;
		accumulator += dt;
		if (accumulator >= 0.125f){
			accumulator -= 0.125f;
			alureUpdate();			
			CheckALError();
			CheckReserved();
		}

		static TIME_PRECISION accumulatorForBuffer = 0;
		accumulatorForBuffer += dt;
		if (accumulatorForBuffer >= 60.f){
			accumulatorForBuffer -= 60.f;
			using namespace std::chrono;
			auto curTick = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
			for (auto it = mAudioBuffers.begin(); it != mAudioBuffers.end(); /**/){
				if (it->second->mReferences==0 && curTick - it->second->mLastAccessed > 60000){
					it = mAudioBuffers.erase(it);
				}
				else{
					++it;
				}
			}
		}

	}
	
	// returning audioid + sourceId;
	AudioSourcePtr PlayAudioBuffer(AudioBufferPtr buffer, std::string pathKey){
		if (!buffer || buffer->mBuffer == -1){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg");
			return 0;
		}

		AudioId audioId = NextAudioId++;
		AudioSourcePtr audioSource(new AudioSource);
		audioSource->SetAudioId(audioId);
		audioSource->SetAudioBuffer(buffer);	
		mAudioSources[audioId] = audioSource;
		ALuint alsource = -1;
		if (!mALSources.empty()){
			alsource = mALSources.top();
			mALSources.pop();
		}
		else{			
			alGenSources(1, &alsource);
			if (alGetError() != AL_NO_ERROR){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot create audio source. Current playing: %d", mNumPlaying).c_str());
			}
			else{
				++mNumGeneratedSources;
			}
		}
		if (alsource != -1){
			alSourcei(alsource, AL_BUFFER, buffer->mBuffer);
			CheckALError();
			if (alurePlaySource(alsource, eos_callback, (void*)audioId) == AL_FALSE){
				CheckALError();
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot play AudioSource(%u, %s)", audioId, buffer->mFilepath.c_str()).c_str());
				audioSource->SetStatus(AudioSourceStatus::Waiting);
				audioSource->SetALAudioSource(-1);
				mALSources.push(alsource);
			}
			else{
				CheckALError();
				audioSource->SetStatus(AudioSourceStatus::Playing);
				++mNumPlaying;
				audioSource->SetALAudioSource(alsource);				
			}
		}

		return audioSource;
	}
	
	AudioBufferPtr GetAudioBuffer(std::string path, std::string loweredPath){
		if (path.size() != loweredPath.size()){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}
		using namespace std::chrono;
		auto curTick = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();		
		std::pair<AudioId, ALuint> audioIdSrcId;
		auto it = mAudioBuffers.Find(loweredPath);
		if (it != mAudioBuffers.end()){
			it->second->mLastAccessed = curTick;
			return it->second;
		}
		else{
			auto buffer = alureCreateBufferFromFile(path.c_str());
			CheckALError();
			if (!buffer){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot create audio buffer for(%s)", path.c_str()).c_str());
				return 0;
			}
			AudioBufferPtr audioBuffer(new AudioBuffer);
			audioBuffer->mBuffer = buffer;
			audioBuffer->mFilepath = path;
			audioBuffer->mLastAccessed = curTick;
			ALint bufferSize, frequency, channels, bit;
			alGetBufferi(buffer, AL_SIZE, &bufferSize);
			alGetBufferi(buffer, AL_FREQUENCY, &frequency);
			alGetBufferi(buffer, AL_CHANNELS, &channels);
			alGetBufferi(buffer, AL_BITS, &bit);
			audioBuffer->mLength = GetDuration(bufferSize, frequency, channels, bit);
			mAudioBuffers[loweredPath] = audioBuffer;
			return audioBuffer;
		}
	}

	struct FBAudio{
		std::string mFilepath;
		AudioProperty mProperty;
	};

	FBAudio ParseFBAudio(const char* path){
		FBAudio ret;
		tinyxml2::XMLDocument doc;
		auto err = doc.LoadFile(path);
		if (err){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot parse fbaudio(%s)", path).c_str());
			return ret;
		}

		auto root = doc.RootElement();
		if (!root){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid format(%s)", path);
			return ret;
		}

		auto filepath = root->Attribute("file");
		if (!filepath){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("'file' attribute is not found in (%s)", path).c_str());			
		}
		else{
			ret.mFilepath = filepath;
		}

		if (!FileSystem::Exists(ret.mFilepath.c_str())){
			std::string xmlpath = path;
			auto onlyFilename = FileSystem::GetFileName(ret.mFilepath.c_str());
			ret.mFilepath = FileSystem::ReplaceFilename(xmlpath.c_str(), onlyFilename.c_str());
			if (!FileSystem::Exists(ret.mFilepath.c_str())){
				ret.mFilepath.clear();
				Logger::Log(FB_ERROR_LOG_ARG, FormatString(
					"Cannot find a audio file(%s or %s)", filepath, ret.mFilepath.c_str()).c_str());
			}			
		}

		auto sz = root->Attribute("gain");
		if (sz){
			ret.mProperty.mGain = StringConverter::ParseReal(sz, ret.mProperty.mGain);
		}

		sz = root->Attribute("relative");
		if (sz){
			ret.mProperty.mRelative = StringConverter::ParseBool(sz, ret.mProperty.mRelative);
		}

		sz = root->Attribute("rollOffFactor");
		if (sz){
			ret.mProperty.mRolloffFactor = StringConverter::ParseReal(sz, ret.mProperty.mRolloffFactor);
		}

		sz = root->Attribute("referenceDistance");
		if (sz){
			ret.mProperty.mReferenceDistance = StringConverter::ParseReal(sz, ret.mProperty.mReferenceDistance);
		}
		return ret;
	}

	AudioId PlayFBAudio(const char* path){
		auto parsedAudio = ParseFBAudio(path);
		if (parsedAudio.mFilepath.empty())
			return INVALID_AUDIO_ID;
		return PlayAudio(parsedAudio.mFilepath.c_str(), parsedAudio.mProperty);

	}

	AudioId PlayAudio(const char* path, AudioSourcePtr* source, bool propWillSet){
		if (!ValidCStringLength(path)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return INVALID_AUDIO_ID;
		}

		if (_stricmp(FileSystem::GetExtension(path),".fbaudio") == 0){
			Logger::Log(FB_ERROR_LOG_ARG, ".fbaudio is played by this function. This is not intended. Use PlayFBAudio()");
			return PlayFBAudio(path);
		}
		else{
			std::string pathKey(path);
			ToLowerCase(pathKey);
			auto audioBuffer = GetAudioBuffer(path, pathKey);
			if (!audioBuffer){
				return INVALID_AUDIO_ID;
			}
			auto audioSource = PlayAudioBuffer(audioBuffer, pathKey);
			if (source){
				*source = audioSource;
			}
			if (!propWillSet && mNumPlaying > MaximumAudioSources){
				DropAudio();
			}
			return audioSource->GetAudioId();
		}
	}

	AudioId PlayAudio(const char* path, float x, float y, float z){
		AudioId audioId;
		if (_stricmp(FileSystem::GetExtension(path), ".fbaudio") == 0){
			audioId = PlayFBAudio(path);
			auto srcIt = mAudioSources.Find(audioId);
			if (srcIt != mAudioSources.end()){
				srcIt->second->SetPosition(x, y, z);
			}
		}
		else{
			AudioSourcePtr src;
			audioId = PlayAudio(path, &src, true);
			if (src){
				src->SetProperty(AudioProperty());
				src->ApplyProp();
				src->SetPosition(x, y, z);
			}
		}

		return audioId;
	}

	AudioId PlayAudio(const char* path, const AudioProperty& property){
		if (_stricmp(FileSystem::GetExtension(path), ".fbaudio") == 0){
			Logger::Log(FB_ERROR_LOG_ARG, ".fbaudio is not intended to play by this function. Use PlayFBAudio()");
		}
		AudioSourcePtr src;
		auto id = PlayAudio(path, &src, true);
		if (!src)
			return id;
		src->SetProperty(property);
		src->ApplyProp();			
		if (mNumPlaying > MaximumAudioSources){
			DropAudio();
		}
		return id;
	}

	AudioId PlayAudioWithFadeIn(const char* path, const AudioProperty& prop, TIME_PRECISION inSec){
		auto id = PlayAudio(path, prop);
		if (id != INVALID_AUDIO_ID){			
			auto fadeIn = SmoothGain::Create(id, 1.0f / inSec, 0.f, prop.mGain);
			mAudioManipulators[id].push_back(fadeIn);
		}
		return id;
	}

	bool StopAudio(AudioId id){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			auto& source = it->second;
			if (source->GetALAudioSource() != -1){
				alureStopSource(source->GetALAudioSource(), AL_TRUE);
				CheckALError();
				return true;
			}
			else{
				OnPlayFinishedInternal(it->second->GetAudioId());
				mAudioSources.erase(it);
				return true;
			}
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Stop invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool StopWithFadeOut(AudioId id, TIME_PRECISION sec){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			if (it->second->GetALAudioSource() != -1){
				auto fadeOutE = GetManipulator(id, AudioManipulatorType::SmoothGain);
				if (fadeOutE){
					fadeOutE->OnGainModified(0.f);
					fadeOutE->SetDuration(sec);
				}
				else{
					auto leftTime = it->second->GetLeftTime();
					auto fadeOut = SmoothGain::Create(id, 1.0f / std::min(leftTime, sec), it->second->GetGain(), 0.f);
					mAudioManipulators[id].push_back(fadeOut);
				}
				return true;
			}
			else{				
				OnPlayFinishedInternal(it->second->GetAudioId());
				mAudioSources.erase(it);
				return true;
			}
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Stop invalid audio(%u)", id).c_str());
			return false;
		}
	}

	TIME_PRECISION GetAudioLeftTime(AudioId id){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			return it->second->GetLeftTime();
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Canno get a length for invalid audio(%u)", id).c_str());
			return 0;
		}
	}

	TIME_PRECISION GetAudioLength(const char* path){
		std::string lowered(path);
		ToLowerCase(lowered);
		auto buffer = GetAudioBuffer(path, lowered);
		if (!buffer){
			return 0.f;
		}
		return buffer->mLength;
	}

	TIME_PRECISION GetAudioLength(AudioId id) {
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			return it->second->GetLength();			
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Canno get a length for invalid audio(%u)", id).c_str());
			return 0;
		}
	}

	FunctionId RegisterEndCallback(AudioId id, std::function< void(AudioId) > callback){
		mEndCallbacks[NextCallbackId] = callback;
		if (!ValueExistsInVector(mEndCallbackIds[id], NextCallbackId))
			mEndCallbackIds[id].push_back(NextCallbackId);

		return NextCallbackId++;
	}

	void UnregisterEndCallbackForAudio(AudioId id){
		auto idIt = mEndCallbackIds.Find(id);
		if (idIt != mEndCallbackIds.end()){
			for (auto& funcId : idIt->second){
				auto funcIt = mEndCallbacks.Find(funcId);
				if (funcIt != mEndCallbacks.end()){
					mEndCallbacks.erase(funcIt);
				}
			}
			mEndCallbackIds.erase(idIt);
		}		
	}

	void UnregisterEndCallbackFunc(FunctionId functionId){
		// functionId, function
		auto funcIt = mEndCallbacks.Find(functionId);
		if (funcIt != mEndCallbacks.end()){
			mEndCallbacks.erase(funcIt);
		}
		// audioId, functionIds
		for (auto it = mEndCallbackIds.begin(); it != mEndCallbackIds.end(); /**/){
			for (auto funcIt = it->second.begin(); funcIt != it->second.end(); /**/){
				if (*funcIt == functionId){
					funcIt = it->second.erase(funcIt);
				}
				else{
					++funcIt;
				}
			}
			if (it->second.empty()){
				it = mEndCallbackIds.erase(it);
			}
			else{
				++it;
			}
		}
	}

	IAudioManipulatorPtr GetManipulator(AudioId id, AudioManipulatorType::Enum type) {
		auto it = mAudioManipulators.Find(id);
		if (it != mAudioManipulators.end()){
			auto& manipulators = it->second;
			for (auto itM = manipulators.begin(); itM != manipulators.end(); ++itM){
				if ((*itM)->GetManipulatorType() == type){
					return *itM;
				}
			}
		}
		return 0;
	}

	void DeleteManipulator(AudioId id, AudioManipulatorType::Enum type){
		auto it = mAudioManipulators.Find(id);
		if (it != mAudioManipulators.end()){
			auto& manipulators = it->second;
			for (auto itM = manipulators.begin(); itM != manipulators.end(); /**/){
				if ((*itM)->GetManipulatorType() == type){
					itM = manipulators.erase(itM);
				}
				else{
					++itM;
				}
			}
			if (manipulators.empty()){
				mAudioManipulators.erase(it);
			}
		}
	}

	bool SetPosition(AudioId id, float x, float y, float z){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			it->second->SetPosition(x, y, z);
			return true;
		}
		else{
			//Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting position for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool SetRelative(AudioId id, bool relative){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			it->second->SetRelative(relative);			
			return true;
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting the relative flag for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool SetReferenceDistance(AudioId id, float distance){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			it->second->SetReferenceDistance(distance);
			return true;
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting max distance for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool SetRolloffFactor(AudioId id, float factor){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			it->second->SetRollOffFactor(factor);
			return true;
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting max distance for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool SetOffsetInSec(AudioId id, float sec){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			it->second->SetOffsetInSec(sec);			
			return true;
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting offset for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool SetGain(AudioId id, float gain, bool checkManipulator){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			bool foundManipulator = false;
			if (checkManipulator){
				auto itM = mAudioManipulators.Find(id);
				if (itM != mAudioManipulators.end()){
					auto& manipulators = itM->second;
					for (auto& it : manipulators){
						it->OnGainModified(gain);
						if (it->GetManipulatorType() == AudioManipulatorType::SmoothGain)
							foundManipulator = true;	
					}
				}
			}
			if (!foundManipulator){
				it->second->SetGain(gain);
			}
			
			return true;
		}
		else{
			// manipulator will set for invalid audio id to find out whether the audio  is finish
			// or not.
			if (checkManipulator)
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting gain for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	bool SetGainSmooth(AudioId id, float gain, float inSec){
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			bool foundManipulator = false;			
			auto itM = mAudioManipulators.Find(id);
			if (itM != mAudioManipulators.end()){
				auto& manipulators = itM->second;
				for (auto& it : manipulators){
					it->OnGainModified(gain);
					if (it->GetManipulatorType() == AudioManipulatorType::SmoothGain){
						it->SetDuration(inSec);
						foundManipulator = true;
					}
				}
			}			
			if (!foundManipulator){
				auto curGain = it->second->GetGain();
				auto smoothGain = SmoothGain::Create(id, std::abs(curGain - gain) / inSec, curGain, gain);
				mAudioManipulators[id].push_back(smoothGain);
			}
			return true;
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting gain for invalid audio(%u)", id).c_str());
			return false;
		}
	}

	float GetGain(AudioId id) const{
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			return it->second->GetGain();
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Setting gain for invalid audio(%u)", id).c_str());
			return 0.f;
		}
	}

	float GetGainFromFBAudio(const char* fbaudioPath){
		return ParseFBAudio(fbaudioPath).mProperty.mGain;
	}

	void RegisterAudioEx(AudioExPtr audioex){
		if (!ValueExistsInVector(mAudioExs, audioex)){
			mAudioExs.push_back(audioex);
		}
	}

	bool IsRegisteredAudioEx(AudioExPtr audioex){
		return ValueExistsInVector(mAudioExs, audioex);
	}
	void OnPlayFinishedInternal(AudioId id){
		for (auto& it : mAudioExs){
			it->OnFinish(id);
		}

		// AudioId, FunctionIds
		auto callbackIt = mEndCallbackIds.Find(id);
		if (callbackIt != mEndCallbackIds.end()){
			for (auto funcId : callbackIt->second){
				// functionId, Function
				auto funcIt = mEndCallbacks.Find(funcId);
				if (funcIt != mEndCallbacks.end()){
					funcIt->second(id);
					mEndCallbacks.erase(funcIt);
				}
			}
			mEndCallbackIds.erase(callbackIt);
		}
	}

	void OnPlayFinished(void* userdata, ALuint source){
		AudioId id = (AudioId)userdata;
		OnPlayFinishedInternal(id);
		--mNumPlaying;
		auto it = mAudioSources.Find(id);
		if (it != mAudioSources.end()){
			assert(it->second->GetALAudioSource() != -1);
			mALSources.push(it->second->GetALAudioSource());			
			mAudioSources.erase(it);			
		}
		else{
			Logger::Log(FB_DEFAULT_LOG_ARG, FormatString("Audio(%u) is not found.", id).c_str());
		}		
	}
};
LPALGETSOURCEDVSOFT AudioManager::Impl::alGetSourcedvSOFT = 0;

//---------------------------------------------------------------------------
AudioManager* sAudioManagerRaw = 0;
AudioManagerWeakPtr sAudioManager;

namespace fb{
	static void eos_callback(void *userData, ALuint source)
	{
		if (sAudioManagerRaw){
			sAudioManagerRaw->mImpl->OnPlayFinished(userData, source);
		}

	}
}
AudioManagerPtr AudioManager::Create(){
	if (sAudioManager.expired()){
		AudioManagerPtr p(new AudioManager, [](AudioManager* obj){ delete obj; });
		sAudioManagerRaw = p.get();
		sAudioManager = p;
		return p;
	}
	return sAudioManager.lock();
}

AudioManager& AudioManager::GetInstance(){
	if (sAudioManager.expired()){
		Logger::Log(FB_ERROR_LOG_ARG, "AudioManager is deleted. Program will crash...");
	}
	return *sAudioManagerRaw;
}

AudioManager::AudioManager()
	: mImpl(new Impl)
{
}
AudioManager::~AudioManager()
{

}

bool AudioManager::Init(){
	return mImpl->Init();
}

void AudioManager::Deinit(){
	mImpl->Deinit();
}

void AudioManager::Update(TIME_PRECISION dt){
	mImpl->Update(dt);
}

AudioId AudioManager::PlayAudio(const char* path){
	if (_stricmp(FileSystem::GetExtension(path), ".fbaudio") == 0){
		return mImpl->PlayFBAudio(path);
	}
	else{
		AudioSourcePtr dummy;
		return mImpl->PlayAudio(path, &dummy, false);
	}
}

AudioId AudioManager::PlayAudio(const char* path, float x, float y, float z){
	return mImpl->PlayAudio(path, x, y, z);
}

AudioId AudioManager::PlayAudio(const char* path, const AudioProperty& property){
	return mImpl->PlayAudio(path, property);
}

AudioId AudioManager::PlayAudioWithFadeIn(const char* path, const AudioProperty& prop, TIME_PRECISION inSec){
	return mImpl->PlayAudioWithFadeIn(path, prop, inSec);
}

bool AudioManager::StopAudio(AudioId id){
	return mImpl->StopAudio(id);
}

TIME_PRECISION AudioManager::GetAudioLength(const char* path) {
	return mImpl->GetAudioLength(path);
}

TIME_PRECISION AudioManager::GetAudioLength(AudioId id) {
	return mImpl->GetAudioLength(id);
}

FunctionId AudioManager::RegisterEndCallback(AudioId id, std::function< void(AudioId) > callback){
	return mImpl->RegisterEndCallback(id, callback);
}

bool AudioManager::StopWithFadeOut(AudioId id, TIME_PRECISION sec){
	return mImpl->StopWithFadeOut(id, sec);
}

TIME_PRECISION AudioManager::GetAudioLeftTime(AudioId id){
	return mImpl->GetAudioLeftTime(id);
}

bool AudioManager::SetPosition(AudioId id, float x, float y, float z){
	return mImpl->SetPosition(id, x, y, z);
}

bool AudioManager::SetRelative(AudioId id, bool relative){
	return mImpl->SetRelative(id, relative);
}

void AudioManager::SetListenerPosition(float x, float y, float z){
	mImpl->mListenerPos = std::make_tuple(x, y, z);
	alListener3f(AL_POSITION, x, y, z);
}

bool AudioManager::SetReferenceDistance(AudioId id, float distance){
	return mImpl->SetReferenceDistance(id, distance);
}

bool AudioManager::SetRolloffFactor(AudioId id, float factor){
	return mImpl->SetRolloffFactor(id, factor);
}

bool AudioManager::SetOffsetInSec(AudioId id, float sec){
	return mImpl->SetOffsetInSec(id, sec);
}

bool AudioManager::SetGain(AudioId id, float gain, bool checkManipulator){
	return mImpl->SetGain(id, gain, checkManipulator);
}

bool AudioManager::SetGainSmooth(AudioId id, float gain, float inSec){
	return mImpl->SetGainSmooth(id, gain, inSec);
}

float AudioManager::GetGain(AudioId id) const{
	return mImpl->GetGain(id);
}

float AudioManager::GetGainFromFBAudio(const char* fbaudioPath){
	return mImpl->GetGainFromFBAudio(fbaudioPath);
}

void AudioManager::RegisterAudioEx(AudioExPtr audioex){
	return mImpl->RegisterAudioEx(audioex);
}

bool AudioManager::IsRegisteredAudioEx(AudioExPtr audioex){
	return mImpl->IsRegisteredAudioEx(audioex);
}

void AudioManager::UnregisterEndCallbackForAudio(AudioId id){
	mImpl->UnregisterEndCallbackForAudio(id);
}

void AudioManager::UnregisterEndCallbackFunc(FunctionId funcId){
	mImpl->UnregisterEndCallbackFunc(funcId);
}

void AudioManager::DeleteManipulator(AudioId id, AudioManipulatorType::Enum type){
	mImpl->DeleteManipulator(id, type);
}