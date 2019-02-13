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

// For Jack
#include <syslog.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#define _ERROR_SYSLOG_ 1
#include "JackBridge.h"

//==================================================================================================
//	SA_Device
//==================================================================================================

class SA_Device
:
	public SA_Object, JackBridgeDriverIF
{

#pragma mark Construction/Destruction
public:
								SA_Device(AudioObjectID inObjectID, UInt32 instance);
					
	virtual void				Activate();
	virtual void				Deactivate();

protected:
	virtual						~SA_Device();

private:	
    bool	IsStreamObjectID(AudioObjectID inObjectID) const;
    bool 	IsInputStreamID(AudioObjectID inObjectID) const;
    int 	getStreamID(AudioObjectID inObjectID) const;

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
	void						ReadInputData(int streadmd, UInt32 inIOBufferFrameSize, Float64 inSampleTime, void* outBuffer);
	void						WriteOutputData(int streamId, UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer);

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

#pragma mark Implementation
#define kDeviceUIDPattern   "JackBridgeDevice-%d"
#define kDeviceUID          "JackBridgeDeviceUID"
#define kDeviceModelUID     "JackBridgeDeviceModelUID"
    
public:
    CFStringRef					CopyDeviceUID() const	{ return CFSTR(kDeviceUID); };
	void						PerformConfigChange(UInt64 inChangeAction, void* inChangeInfo);
	void						AbortConfigChange(UInt64 inChangeAction, void* inChangeInfo);

private:
	enum
	{
								kNumberOfSubObjects					= NUM_INPUT_STREAMS + NUM_OUTPUT_STREAMS,
								kNumberOfInputSubObjects			= NUM_INPUT_STREAMS,
								kNumberOfOutputSubObjects			= NUM_OUTPUT_STREAMS,
								
								kNumberOfStreams					= NUM_INPUT_STREAMS + NUM_OUTPUT_STREAMS,
								kNumberOfInputStreams				= NUM_INPUT_STREAMS,
								kNumberOfOutputStreams				= NUM_OUTPUT_STREAMS,
								
								kNumberOfControls					= 0
	};
	
	CAMutex						mStateMutex;
	CAMutex						mIOMutex;
	UInt64						mStartCount;
	UInt64						mSampleRateShadow;
	UInt32						mRingBufferFrameSize;
	UInt32                  	mDriverStatus;
	
	AudioObjectID				mInputStreamObjectID[NUM_INPUT_STREAMS];
	bool						mInputStreamIsActive[NUM_INPUT_STREAMS];
	
	AudioObjectID				mOutputStreamObjectID[NUM_OUTPUT_STREAMS];
	bool						mOutputStreamIsActive[NUM_OUTPUT_STREAMS];

#pragma mark jackrouter interfaces
private:
    Float64                  gDevice_HostTicksPerFrame;
    UInt64                   gDevice_NumberTimeStamps;
    Float64                  gDevice_AnchorSampleTime;
    UInt64                   gDevice_AnchorHostTime;
};

#endif	//	__SA_Device_h__
