/*
     File: SA_Device.h 
 Abstract:  Part of SimpleAudioDriver Plug-In Example  
  Version: 1.0.1 
  
 Disclaimer: IMPORTANT:  This Apple software is supplied to you by Apple 
 Inc. ("Apple") in consideration of your agreement to the following 
 terms, and your use, installation, modification or redistribution of 
 this Apple software constitutes acceptance of these terms.  If you do 
 not agree with these terms, please do not use, install, modify or 
 redistribute this Apple software. 
  
 In consideration of your agreement to abide by the following terms, and 
 subject to these terms, Apple grants you a personal, non-exclusive 
 license, under Apple's copyrights in this original Apple software (the 
 "Apple Software"), to use, reproduce, modify and redistribute the Apple 
 Software, with or without modifications, in source and/or binary forms; 
 provided that if you redistribute the Apple Software in its entirety and 
 without modifications, you must retain this notice and the following 
 text and disclaimers in all such redistributions of the Apple Software. 
 Neither the name, trademarks, service marks or logos of Apple Inc. may 
 be used to endorse or promote products derived from the Apple Software 
 without specific prior written permission from Apple.  Except as 
 expressly stated in this notice, no other rights or licenses, express or 
 implied, are granted by Apple herein, including but not limited to any 
 patent rights that may be infringed by your derivative works or by other 
 works in which the Apple Software may be incorporated. 
  
 The Apple Software is provided by Apple on an "AS IS" basis.  APPLE 
 MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION 
 THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS 
 FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND 
 OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS. 
  
 IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL 
 OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, 
 MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED 
 AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE), 
 STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE 
 POSSIBILITY OF SUCH DAMAGE. 
  
 Copyright (C) 2013 Apple Inc. All Rights Reserved. 
  
*/
/*==================================================================================================
	SA_Device.h
==================================================================================================*/
#if !defined(__SA_Device_h__)
#define __SA_Device_h__

//==================================================================================================
//	Includes
//==================================================================================================

//	SuperClass Includes
#include "SA_Object.h"

//	PublicUtility Includes
#include "CACFString.h"
#include "CAMutex.h"
#include "CAVolumeCurve.h"

// For Jack
#include <syslog.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef float sample_t;
#define AUDIO_SAMPLE_SIZE (sizeof(sample_t)) // 32bit float PCM

#define MAX_FRAME_SIZE      4096UL
#define STRBUFNUM           (MAX_FRAME_SIZE*4*2) // stereo * 4buffers
#define STRBUFSZ            (STRBUFNUM*AUDIO_SAMPLE_SIZE)
#define JACK_SHMSIZE        (0x1000+STRBUFSZ*2) // control + down/upstream buffers

//  control IDs
enum
{
    kSimpleAudioDriver_Control_MasterInputVolume,
    kSimpleAudioDriver_Control_MasterOutputVolume
};

//  volume control ranges
#define kSimpleAudioDriver_Control_MinRawVolumeValue    0
#define kSimpleAudioDriver_Control_MaxRawVolumeValue    96
#define kSimpleAudioDriver_Control_MinDBVolumeValue     -96.0f
#define kSimpleAudioDriver_Control_MaxDbVolumeValue     0.0f

//==================================================================================================
//	Types
//==================================================================================================

struct	SimpleAudioDriverStatus;

//==================================================================================================
//	SA_Device
//==================================================================================================

class SA_Device
:
	public SA_Object
{

#pragma mark Construction/Destruction
public:
								SA_Device(AudioObjectID inObjectID);
					
	virtual void				Activate();
	virtual void				Deactivate();

protected:
	virtual						~SA_Device();
	
#pragma mark Property Operations
public:
	virtual bool				HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	virtual bool				IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	virtual UInt32				GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const;
	virtual void				GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const;
	virtual void				SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

#pragma mark Device Property Operations
private:
	bool						Device_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	bool						Device_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	UInt32						Device_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const;
	void						Device_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const;
	void						Device_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

#pragma mark Stream Property Operations
private:
	bool						Stream_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	bool						Stream_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	UInt32						Stream_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const;
	void						Stream_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const;
	void						Stream_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

#pragma mark Control Property Operations
private:
	bool						Control_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	bool						Control_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	UInt32						Control_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const;
	void						Control_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const;
	void						Control_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

#pragma mark IO Operations
public:
	void						StartIO();
	void						StopIO();
	void						GetZeroTimeStamp(Float64& outSampleTime, UInt64& outHostTime, UInt64& outSeed);
	
	void						WillDoIOOperation(UInt32 inOperationID, bool& outWillDo, bool& outWillDoInPlace) const;
	void						BeginIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo);
	void						DoIOOperation(AudioObjectID inStreamObjectID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
	void						EndIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo);

private:
	void						ReadInputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, void* outBuffer);
	void						WriteOutputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer);
    void						SA_WriteTo(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer);

#pragma mark Hardware Accessors
public:
	static CFStringRef			HW_CopyDeviceUID();
	
private:
	void						_HW_Open();
	void						_HW_Close();
	kern_return_t				_HW_StartIO();
	void						_HW_StopIO();
	UInt64						_HW_GetSampleRate() const;
	kern_return_t				_HW_SetSampleRate(UInt64 inNewSampleRate);
	SInt32						_HW_GetVolumeControlValue(int inControlID) const;
	kern_return_t				_HW_SetVolumeControlValue(int inControlID, SInt32 inNewControlValue);

#pragma mark Implementation
#define kDeviceUIDPattern   "JackRouterDevice-%d"
#define kDeviceUID          "JackRouterDeviceUID"
#define kDeviceModelUID     "JackRouterDeviceModelUID"
    
public:
    CFStringRef					CopyDeviceUID() const	{ return CFSTR(kDeviceUID); };
	void						PerformConfigChange(UInt64 inChangeAction, void* inChangeInfo);
	void						AbortConfigChange(UInt64 inChangeAction, void* inChangeInfo);

private:
	enum
	{
								kNumberOfSubObjects					= 4,
								kNumberOfInputSubObjects			= 2,
								kNumberOfOutputSubObjects			= 2,
								
								kNumberOfStreams					= 2,
								kNumberOfInputStreams				= 1,
								kNumberOfOutputStreams				= 1,
								
								kNumberOfControls					= 2
	};
	
	CAMutex						mStateMutex;
	CAMutex						mIOMutex;
	UInt64						mStartCount;
	UInt64						mSampleRateShadow;
	UInt32						mRingBufferFrameSize;
	SimpleAudioDriverStatus*	mDriverStatus;
	
	AudioObjectID				mInputStreamObjectID;
	bool						mInputStreamIsActive;
	Byte*						mInputStreamRingBuffer;
	
	AudioObjectID				mOutputStreamObjectID;
	bool						mOutputStreamIsActive;
	Byte*						mOutputStreamRingBuffer;
	
	AudioObjectID				mInputMasterVolumeControlObjectID;
	SInt32						mInputMasterVolumeControlRawValueShadow;
	AudioObjectID				mOutputMasterVolumeControlObjectID;
	SInt32						mOutputMasterVolumeControlRawValueShadow;
	CAVolumeCurve				mVolumeCurve;

#pragma mark jackrouter interfaces
private:
    int             JR_JackInit();
    int             shm_fd;
    unsigned short  *uwp, *urp, *dwp, *drp;
    sample_t        *buf_up;
    sample_t        *buf_down;

    Float64                  gDevice_HostTicksPerFrame;
    UInt64                   gDevice_NumberTimeStamps;
    Float64                  gDevice_AnchorSampleTime;
    UInt64                   gDevice_AnchorHostTime;
};

#endif	//	__SA_Device_h__
