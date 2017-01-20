/*
     File: SA_Device.cpp 
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
	SA_Device.cpp
==================================================================================================*/

//==================================================================================================
//	Includes
//==================================================================================================

//	Self Include
#include "SA_Device.h"

//	Local Includes
#include "SA_PlugIn.h"

//	PublicUtility Includes
#include "CADebugMacros.h"
#include "CADispatchQueue.h"
#include "CAException.h"

#include <mach/mach_time.h>

//==================================================================================================
//	SA_Device
//==================================================================================================
#pragma mark Construction/Destruction

SA_Device::SA_Device(AudioObjectID inObjectID)
:
	SA_Object(inObjectID, kAudioDeviceClassID, kAudioObjectClassID, kAudioObjectPlugInObject),
	mStateMutex("Device State"),
	mIOMutex("Device IO"),
	mStartCount(0),
	mSampleRateShadow(48000),
	mRingBufferFrameSize(0),
	mInputStreamObjectID(SA_ObjectMap::GetNextObjectID()),
	mInputStreamIsActive(true),
	mInputStreamRingBuffer(NULL),
	mOutputStreamObjectID(SA_ObjectMap::GetNextObjectID()),
	mOutputStreamIsActive(true),
	mOutputStreamRingBuffer(NULL),
	mInputMasterVolumeControlObjectID(SA_ObjectMap::GetNextObjectID()),
	mInputMasterVolumeControlRawValueShadow(kSimpleAudioDriver_Control_MinRawVolumeValue),
	mOutputMasterVolumeControlObjectID(SA_ObjectMap::GetNextObjectID()),
	mOutputMasterVolumeControlRawValueShadow(kSimpleAudioDriver_Control_MinRawVolumeValue),
	mVolumeCurve()
{
	//	Setup the volume curve with the one range
	mVolumeCurve.AddRange(kSimpleAudioDriver_Control_MinRawVolumeValue, kSimpleAudioDriver_Control_MaxRawVolumeValue, kSimpleAudioDriver_Control_MinDBVolumeValue, kSimpleAudioDriver_Control_MaxDbVolumeValue);
}

void	SA_Device::Activate()
{
	//	Open the connection to the driver and initialize things.
	_HW_Open();
    JR_JackInit();
	
	//	map the subobject IDs to this object
	SA_ObjectMap::MapObject(mInputStreamObjectID, this);
	SA_ObjectMap::MapObject(mOutputStreamObjectID, this);
	SA_ObjectMap::MapObject(mInputMasterVolumeControlObjectID, this);
	SA_ObjectMap::MapObject(mOutputMasterVolumeControlObjectID, this);
	
	//	call the super-class, which just marks the object as active
	SA_Object::Activate();

    //  calculate the host ticks per frame
    struct mach_timebase_info theTimeBaseInfo;
    mach_timebase_info(&theTimeBaseInfo);
    Float64 theHostClockFrequency = theTimeBaseInfo.denom / theTimeBaseInfo.numer;
    theHostClockFrequency *= 1000000000.0;
    gDevice_HostTicksPerFrame = theHostClockFrequency / mSampleRateShadow;
}

void	SA_Device::Deactivate()
{
	//	When this method is called, the obejct is basically dead, but we still need to be thread
	//	safe. In this case, we also need to be safe vs. any IO threads, so we need to take both
	//	locks.
	CAMutex::Locker theStateLocker(mStateMutex);
	CAMutex::Locker theIOLocker(mIOMutex);
	
	//	mark the object inactive by calling the super-class
	SA_Object::Deactivate();
	
	//	unmap the subobject IDs
	SA_ObjectMap::UnmapObject(mInputStreamObjectID, this);
	SA_ObjectMap::UnmapObject(mOutputStreamObjectID, this);
	SA_ObjectMap::UnmapObject(mInputMasterVolumeControlObjectID, this);
	SA_ObjectMap::UnmapObject(mOutputMasterVolumeControlObjectID, this);
	
	//	close the connection to the driver
	_HW_Close();
}

SA_Device::~SA_Device()
{
}

#pragma mark Property Operations

bool	SA_Device::HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	This object implements several API-level objects. So the first thing to do is to figure out
	//	which object this request is really for. Note that mSubObjectID is an invariant as this
	//	driver's structure does not change dynamically. It will always have the parts it has.
	bool theAnswer = false;
	if(inObjectID == mObjectID)
	{
		theAnswer = Device_HasProperty(inObjectID, inClientPID, inAddress);
	}
	else if((inObjectID == mInputStreamObjectID) || (inObjectID == mOutputStreamObjectID))
	{
		theAnswer = Stream_HasProperty(inObjectID, inClientPID, inAddress);
	}
	else if((inObjectID == mInputMasterVolumeControlObjectID) || (inObjectID == mOutputMasterVolumeControlObjectID))
	{
		theAnswer = Control_HasProperty(inObjectID, inClientPID, inAddress);
	}
	else
	{
		Throw(CAException(kAudioHardwareBadObjectError));
	}
	return theAnswer;
}

bool	SA_Device::IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	bool theAnswer = false;
	if(inObjectID == mObjectID)
	{
		theAnswer = Device_IsPropertySettable(inObjectID, inClientPID, inAddress);
	}
	else if((inObjectID == mInputStreamObjectID) || (inObjectID == mOutputStreamObjectID))
	{
		theAnswer = Stream_IsPropertySettable(inObjectID, inClientPID, inAddress);
	}
	else if((inObjectID == mInputMasterVolumeControlObjectID) || (inObjectID == mOutputMasterVolumeControlObjectID))
	{
		theAnswer = Control_IsPropertySettable(inObjectID, inClientPID, inAddress);
	}
	else
	{
		Throw(CAException(kAudioHardwareBadObjectError));
	}
	return theAnswer;
}

UInt32	SA_Device::GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	UInt32 theAnswer = 0;
	if(inObjectID == mObjectID)
	{
		theAnswer = Device_GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}
	else if((inObjectID == mInputStreamObjectID) || (inObjectID == mOutputStreamObjectID))
	{
		theAnswer = Stream_GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}
	else if((inObjectID == mInputMasterVolumeControlObjectID) || (inObjectID == mOutputMasterVolumeControlObjectID))
	{
		theAnswer = Control_GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}
	else
	{
		Throw(CAException(kAudioHardwareBadObjectError));
	}
	return theAnswer;
}

void	SA_Device::GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	if(inObjectID == mObjectID)
	{
		Device_GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
	else if((inObjectID == mInputStreamObjectID) || (inObjectID == mOutputStreamObjectID))
	{
		Stream_GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
	else if((inObjectID == mInputMasterVolumeControlObjectID) || (inObjectID == mOutputMasterVolumeControlObjectID))
	{
		Control_GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
	else
	{
		Throw(CAException(kAudioHardwareBadObjectError));
	}
}

void	SA_Device::SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	if(inObjectID == mObjectID)
	{
		Device_SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
	}
	else if((inObjectID == mInputStreamObjectID) || (inObjectID == mOutputStreamObjectID))
	{
		Stream_SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
	}
	else if((inObjectID == mInputMasterVolumeControlObjectID) || (inObjectID == mOutputMasterVolumeControlObjectID))
	{
		Control_SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
	}
	else
	{
		Throw(CAException(kAudioHardwareBadObjectError));
	}
}

#pragma mark Device Property Operations

bool	SA_Device::Device_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyManufacturer:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
		case kAudioDevicePropertyTransportType:
		case kAudioDevicePropertyRelatedDevices:
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyDeviceIsAlive:
		case kAudioDevicePropertyDeviceIsRunning:
		case kAudioObjectPropertyControlList:
		case kAudioDevicePropertyNominalSampleRate:
		case kAudioDevicePropertyAvailableNominalSampleRates:
		case kAudioDevicePropertyIsHidden:
		case kAudioDevicePropertyZeroTimeStampPeriod:
		case kAudioDevicePropertyStreams:
			theAnswer = true;
			break;
			
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			theAnswer = (inAddress.mScope == kAudioObjectPropertyScopeInput) || (inAddress.mScope == kAudioObjectPropertyScopeOutput);
			break;
			
		default:
			theAnswer = SA_Object::HasProperty(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

bool	SA_Device::Device_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyName:
		case kAudioObjectPropertyManufacturer:
		case kAudioDevicePropertyDeviceUID:
		case kAudioDevicePropertyModelUID:
		case kAudioDevicePropertyTransportType:
		case kAudioDevicePropertyRelatedDevices:
		case kAudioDevicePropertyClockDomain:
		case kAudioDevicePropertyDeviceIsAlive:
		case kAudioDevicePropertyDeviceIsRunning:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertyStreams:
		case kAudioObjectPropertyControlList:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyAvailableNominalSampleRates:
		case kAudioDevicePropertyIsHidden:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyZeroTimeStampPeriod:
			theAnswer = false;
			break;
		
		case kAudioDevicePropertyNominalSampleRate:
			theAnswer = true;
			break;
		
		default:
			theAnswer = SA_Object::IsPropertySettable(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

UInt32	SA_Device::Device_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	UInt32 theAnswer = 0;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyName:
			theAnswer = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			theAnswer = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					theAnswer = kNumberOfSubObjects * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeInput:
					theAnswer = kNumberOfInputSubObjects * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeOutput:
					theAnswer = kNumberOfOutputSubObjects * sizeof(AudioObjectID);
					break;
			};
			break;

		case kAudioDevicePropertyDeviceUID:
			theAnswer = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			theAnswer = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyTransportType:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyRelatedDevices:
			theAnswer = sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyClockDomain:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsAlive:
			theAnswer = sizeof(AudioClassID);
			break;

		case kAudioDevicePropertyDeviceIsRunning:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyLatency:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyStreams:
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					theAnswer = kNumberOfStreams * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeInput:
					theAnswer = kNumberOfInputStreams * sizeof(AudioObjectID);
					break;
					
				case kAudioObjectPropertyScopeOutput:
					theAnswer = kNumberOfOutputStreams * sizeof(AudioObjectID);
					break;
			};
			break;

		case kAudioObjectPropertyControlList:
			theAnswer = kNumberOfControls * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			theAnswer = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			theAnswer = 2 * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			theAnswer = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			theAnswer = offsetof(AudioChannelLayout, mChannelDescriptions) + (2 * sizeof(AudioChannelDescription));
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			theAnswer = sizeof(UInt32);
			break;

		default:
			theAnswer = SA_Object::GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
			break;
	};
	return theAnswer;
}

void	SA_Device::Device_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required.
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.

	UInt32 theNumberItemsToFetch;
	UInt32 theItemIndex;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyName:
			//	This is the human readable name of the device. Note that in this case we return a
			//	value that is a key into the localizable strings in this bundle. This allows us to
			//	return a localized name for the device.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the device");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR("DeviceName");
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the plug-in. Note that in this case
			//	we return a value that is a key into the localizable strings in this bundle. This
			//	allows us to return a localized name for the manufacturer.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the device");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR("ManufacturerName");
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	The device owns its streams and controls. Note that what is returned here
			//	depends on the scope requested.
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all objects
					if(theNumberItemsToFetch > kNumberOfSubObjects)
					{
						theNumberItemsToFetch = kNumberOfSubObjects;
					}
					
					//	fill out the list with as many objects as requested, which is everything
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStreamObjectID;
					}
					if(theNumberItemsToFetch > 1)
					{
						reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputStreamObjectID;
					}
					if(theNumberItemsToFetch > 2)
					{
						reinterpret_cast<AudioObjectID*>(outData)[2] = mInputMasterVolumeControlObjectID;
					}
					if(theNumberItemsToFetch > 3)
					{
						reinterpret_cast<AudioObjectID*>(outData)[3] = mOutputMasterVolumeControlObjectID;
					}
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > kNumberOfInputSubObjects)
					{
						theNumberItemsToFetch = kNumberOfInputSubObjects;
					}
					
					//	fill out the list with the right objects
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStreamObjectID;
					}
					if(theNumberItemsToFetch > 1)
					{
						reinterpret_cast<AudioObjectID*>(outData)[1] = mInputMasterVolumeControlObjectID;
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
					if(theNumberItemsToFetch > kNumberOfOutputSubObjects)
					{
						theNumberItemsToFetch = kNumberOfOutputSubObjects;
					}
					
					//	fill out the list with the right objects
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mOutputStreamObjectID;
					}
					if(theNumberItemsToFetch > 1)
					{
						reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputMasterVolumeControlObjectID;
					}
					break;
			};
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			//	This is a CFString that is a persistent token that can identify the same
			//	audio device across boot sessions. Note that two instances of the same
			//	device must have different values for this property.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceUID for the device");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR(kDeviceUID);
			outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			//	This is a CFString that is a persistent token that can identify audio
			//	devices that are the same kind of device. Note that two instances of the
			//	save device must have the same value for this property.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyModelUID for the device");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR(kDeviceModelUID);
			outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyTransportType:
			//	This value represents how the device is attached to the system. This can be
			//	any 32 bit integer, but common values for this property are defined in
			//	<CoreAudio/AudioHardwareBase.h>
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyTransportType for the device");
			*reinterpret_cast<UInt32*>(outData) = kAudioDeviceTransportTypeVirtual;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyRelatedDevices:
			//	The related devices property identifies device objects that are very closely
			//	related. Generally, this is for relating devices that are packaged together
			//	in the hardware such as when the input side and the output side of a piece
			//	of hardware can be clocked separately and therefore need to be represented
			//	as separate AudioDevice objects. In such case, both devices would report
			//	that they are related to each other. Note that at minimum, a device is
			//	related to itself, so this list will always be at least one item long.

			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	we only have the one device...
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	Write the devices' object IDs into the return value
			if(theNumberItemsToFetch > 0)
			{
				reinterpret_cast<AudioObjectID*>(outData)[0] = GetObjectID();
			}
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyClockDomain:
			//	This property allows the device to declare what other devices it is
			//	synchronized with in hardware. The way it works is that if two devices have
			//	the same value for this property and the value is not zero, then the two
			//	devices are synchronized in hardware. Note that a device that either can't
			//	be synchronized with others or doesn't know should return 0 for this
			//	property.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyClockDomain for the device");
			*reinterpret_cast<UInt32*>(outData) = 0;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsAlive:
			//	This property returns whether or not the device is alive. Note that it is
			//	note uncommon for a device to be dead but still momentarily availble in the
			//	device list. In the case of this device, it will always be alive.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsAlive for the device");
			*reinterpret_cast<UInt32*>(outData) = 1;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceIsRunning:
			//	This property returns whether or not IO is running for the device.
			{
				ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsRunning for the device");
				
				//	The IsRunning state is protected by the state lock
				CAMutex::Locker theStateLocker(mStateMutex);
				
				//	return the state and how much data we are touching
				*reinterpret_cast<UInt32*>(outData) = mStartCount > 0;
				outDataSize = sizeof(UInt32);
			}
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
			//	This property returns whether or not the device wants to be able to be the
			//	default device for content. This is the device that iTunes and QuickTime
			//	will use to play their content on and FaceTime will use as it's microhphone.
			//	Nearly all devices should allow for this.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceCanBeDefaultDevice for the device");
			*reinterpret_cast<UInt32*>(outData) = 1;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			//	This property returns whether or not the device wants to be the system
			//	default device. This is the device that is used to play interface sounds and
			//	other incidental or UI-related sounds on. Most devices should allow this
			//	although devices with lots of latency may not want to.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceCanBeDefaultSystemDevice for the device");
			*reinterpret_cast<UInt32*>(outData) = 1;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyLatency:
			//	This property returns the presentation latency of the device. For this,
			//	device, the value is 0 due to the fact that it always vends silence.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyLatency for the device");
			*reinterpret_cast<UInt32*>(outData) = 0;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyStreams:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Note that what is returned here depends on the scope requested.
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all streams
					if(theNumberItemsToFetch > kNumberOfStreams)
					{
						theNumberItemsToFetch = kNumberOfStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStreamObjectID;
					}
					if(theNumberItemsToFetch > 1)
					{
						reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputStreamObjectID;
					}
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > kNumberOfInputStreams)
					{
						theNumberItemsToFetch = kNumberOfInputStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStreamObjectID;
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
					if(theNumberItemsToFetch > kNumberOfOutputStreams)
					{
						theNumberItemsToFetch = kNumberOfOutputStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mOutputStreamObjectID;
					}
					break;
			};
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			if(theNumberItemsToFetch > kNumberOfControls)
			{
				theNumberItemsToFetch = kNumberOfControls;
			}
			
			//	fill out the list with as many objects as requested, which is everything
			if(theNumberItemsToFetch > 0)
			{
				reinterpret_cast<AudioObjectID*>(outData)[0] = mInputMasterVolumeControlObjectID;
			}
			if(theNumberItemsToFetch > 1)
			{
				reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputMasterVolumeControlObjectID;
			}
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertySafetyOffset:
			//	This property returns the how close to now the HAL can read and write. For
			//	this, device, the value is 0 due to the fact that it always vends silence.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertySafetyOffset for the device");
			*reinterpret_cast<UInt32*>(outData) = 0;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyNominalSampleRate:
			//	This property returns the nominal sample rate of the device. Note that we
			//	only need to take the state lock to get this value.
			{
				ThrowIf(inDataSize < sizeof(Float64), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyNominalSampleRate for the device");
			
				//	The sample rate is protected by the state lock
				CAMutex::Locker theStateLocker(mStateMutex);
					
				//	need to lock around fetching the sample rate
				*reinterpret_cast<Float64*>(outData) = static_cast<Float64>(_HW_GetSampleRate());
				outDataSize = sizeof(Float64);
			}
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			//	This returns all nominal sample rates the device supports as an array of
			//	AudioValueRangeStructs. Note that for discrete sampler rates, the range
			//	will have the minimum value equal to the maximum value.
			
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioValueRange);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > 2)
			{
				theNumberItemsToFetch = 2;
			}
			
			//	fill out the return array
			if(theNumberItemsToFetch > 0)
			{
				((AudioValueRange*)outData)[0].mMinimum = 44100.0;
				((AudioValueRange*)outData)[0].mMaximum = 44100.0;
			}
			if(theNumberItemsToFetch > 1)
			{
				((AudioValueRange*)outData)[1].mMinimum = 48000.0;
				((AudioValueRange*)outData)[1].mMaximum = 48000.0;
			}
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioValueRange);
			break;
		
		case kAudioDevicePropertyIsHidden:
			//	This returns whether or not the device is visible to clients.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyIsHidden for the device");
			*reinterpret_cast<UInt32*>(outData) = 0;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			//	This property returns which two channesl to use as left/right for stereo
			//	data by default. Note that the channel numbers are 1-based.
			ThrowIf(inDataSize < (2 * sizeof(UInt32)), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelsForStereo for the device");
			((UInt32*)outData)[0] = 1;
			((UInt32*)outData)[1] = 2;
			outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			//	This property returns the default AudioChannelLayout to use for the device
			//	by default. For this device, we return a stereo ACL.
			{
				//	calcualte how big the
				UInt32 theACLSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (2 * sizeof(AudioChannelDescription));
				ThrowIf(inDataSize < theACLSize, CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelLayout for the device");
				((AudioChannelLayout*)outData)->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
				((AudioChannelLayout*)outData)->mChannelBitmap = 0;
				((AudioChannelLayout*)outData)->mNumberChannelDescriptions = 2;
				for(theItemIndex = 0; theItemIndex < 2; ++theItemIndex)
				{
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelLabel = kAudioChannelLabel_Left + theItemIndex;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelFlags = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[0] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[1] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[2] = 0;
				}
				outDataSize = theACLSize;
			}
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			//	This property returns how many frames the HAL should expect to see between
			//	successive sample times in the zero time stamps this device provides.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyZeroTimeStampPeriod for the device");
			*reinterpret_cast<UInt32*>(outData) = mRingBufferFrameSize;
			outDataSize = sizeof(UInt32);
			break;

		default:
			SA_Object::GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
	};
}

void	SA_Device::Device_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	switch(inAddress.mSelector)
	{
		case kAudioDevicePropertyNominalSampleRate:
			//	Changing the sample rate needs to be handled via the RequestConfigChange/PerformConfigChange machinery.
			{
				//	check the arguments
				ThrowIf(inDataSize != sizeof(Float64), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Device_SetPropertyData: wrong size for the data for kAudioDevicePropertyNominalSampleRate");
				ThrowIf((*((const Float64*)inData) != 44100.0) && (*((const Float64*)inData) != 48000.0), CAException(kAudioHardwareIllegalOperationError), "SA_Device::Device_SetPropertyData: unsupported value for kAudioDevicePropertyNominalSampleRate");
				
				//	we need to lock around getting the current sample rate to compare against the new rate
				UInt64 theOldSampleRate = 0;
				{
					CAMutex::Locker theStateLocker(mStateMutex);
					theOldSampleRate = _HW_GetSampleRate();
				}
				
				//	make sure that the new value is different than the old value
				UInt64 theNewSampleRate = static_cast<UInt64>(*reinterpret_cast<const Float64*>(inData));
				if(theNewSampleRate != theOldSampleRate)
				{
					//	we dispatch this so that the change can happen asynchronously
					AudioObjectID theDeviceObjectID = GetObjectID();
					CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
																				SA_PlugIn::Host_RequestDeviceConfigurationChange(theDeviceObjectID, theNewSampleRate, NULL);
																			});
				}
			}
			break;
		
		default:
			SA_Object::SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			break;
	};
}

#pragma mark Stream Property Operations

bool	SA_Device::Stream_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Stream_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioStreamPropertyIsActive:
		case kAudioStreamPropertyDirection:
		case kAudioStreamPropertyTerminalType:
		case kAudioStreamPropertyStartingChannel:
		case kAudioStreamPropertyLatency:
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			theAnswer = true;
			break;
			
		default:
			theAnswer = SA_Object::HasProperty(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

bool	SA_Device::Stream_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Stream_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioStreamPropertyDirection:
		case kAudioStreamPropertyTerminalType:
		case kAudioStreamPropertyStartingChannel:
		case kAudioStreamPropertyLatency:
		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			theAnswer = false;
			break;
		
		case kAudioStreamPropertyIsActive:
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			theAnswer = true;
			break;
		
		default:
			theAnswer = SA_Object::IsPropertySettable(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

UInt32	SA_Device::Stream_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Stream_GetPropertyData() method.
	
	UInt32 theAnswer = 0;
	switch(inAddress.mSelector)
	{
		case kAudioStreamPropertyIsActive:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioStreamPropertyDirection:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioStreamPropertyTerminalType:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioStreamPropertyStartingChannel:
			theAnswer = sizeof(UInt32);
			break;
		
		case kAudioStreamPropertyLatency:
			theAnswer = sizeof(UInt32);
			break;

		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			theAnswer = sizeof(AudioStreamBasicDescription);
			break;

		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			theAnswer = 2 * sizeof(AudioStreamRangedDescription);
			break;

		default:
			theAnswer = SA_Object::GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
			break;
	};
	return theAnswer;
}

void	SA_Device::Stream_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required.
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	
	UInt32 theNumberItemsToFetch;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			//	The base class for kAudioStreamClassID is kAudioObjectClassID
			ThrowIf(inDataSize < sizeof(AudioClassID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the volume control");
			*reinterpret_cast<AudioClassID*>(outData) = kAudioObjectClassID;
			outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			//	Streams are of the class, kAudioStreamClassID
			ThrowIf(inDataSize < sizeof(AudioClassID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the volume control");
			*reinterpret_cast<AudioClassID*>(outData) = kAudioStreamClassID;
			outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			//	The stream's owner is the device object
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the volume control");
			*reinterpret_cast<AudioObjectID*>(outData) = GetObjectID();
			outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioStreamPropertyIsActive:
			//	This property tells the device whether or not the given stream is going to
			//	be used for IO. Note that we need to take the state lock to examine this
			//	value.
			{
				ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioStreamPropertyIsActive for the stream");
				
				//	lock the state mutex
				CAMutex::Locker theStateLocker(mStateMutex);
				
				//	return the requested value
				*reinterpret_cast<UInt32*>(outData) = (inAddress.mScope == kAudioObjectPropertyScopeInput) ? mInputStreamIsActive : mOutputStreamIsActive;
				outDataSize = sizeof(UInt32);
			}
			break;

		case kAudioStreamPropertyDirection:
			//	This returns whether the stream is an input stream or an output stream.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioStreamPropertyDirection for the stream");
			*reinterpret_cast<UInt32*>(outData) = (inObjectID == mInputStreamObjectID) ? 1 : 0;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyTerminalType:
			//	This returns a value that indicates what is at the other end of the stream
			//	such as a speaker or headphones, or a microphone. Values for this property
			//	are defined in <CoreAudio/AudioHardwareBase.h>
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioStreamPropertyTerminalType for the stream");
			*reinterpret_cast<UInt32*>(outData) = (inObjectID == mInputStreamObjectID) ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyStartingChannel:
			//	This property returns the absolute channel number for the first channel in
			//	the stream. For exmaple, if a device has two output streams with two
			//	channels each, then the starting channel number for the first stream is 1
			//	and ths starting channel number fo the second stream is 3.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioStreamPropertyStartingChannel for the stream");
			*reinterpret_cast<UInt32*>(outData) = 1;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyLatency:
			//	This property returns any additonal presentation latency the stream has.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioStreamPropertyStartingChannel for the stream");
			*reinterpret_cast<UInt32*>(outData) = 0;
			outDataSize = sizeof(UInt32);
			break;

		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			//	This returns the current format of the stream in an AudioStreamBasicDescription.
			//	For devices that don't override the mix operation, the virtual format has to be the
			//	same as the physical format.
			{
				ThrowIf(inDataSize < sizeof(AudioStreamBasicDescription), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_GetPropertyData: not enough space for the return value of kAudioStreamPropertyVirtualFormat for the stream");
				
				//	lock the state mutex
				CAMutex::Locker theStateLocker(mStateMutex);
				
				//	This particular device always vends  16 bit native endian signed integers
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mSampleRate = static_cast<Float64>(_HW_GetSampleRate());
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mFormatID = kAudioFormatLinearPCM;
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mBytesPerPacket = 8;
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mFramesPerPacket = 1;
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mBytesPerFrame = 8;
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mChannelsPerFrame = 2;
				reinterpret_cast<AudioStreamBasicDescription*>(outData)->mBitsPerChannel = 32;
				outDataSize = sizeof(AudioStreamBasicDescription);
			}
			break;

		case kAudioStreamPropertyAvailableVirtualFormats:
		case kAudioStreamPropertyAvailablePhysicalFormats:
			//	This returns an array of AudioStreamRangedDescriptions that describe what
			//	formats are supported.

			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioStreamRangedDescription);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > 2)
			{
				theNumberItemsToFetch = 2;
			}
			
			//	fill out the return array
			if(theNumberItemsToFetch > 0)
			{
				((AudioStreamRangedDescription*)outData)[0].mFormat.mSampleRate = 44100.0;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mFormatID = kAudioFormatLinearPCM;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mBytesPerPacket = 8;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mFramesPerPacket = 1;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mBytesPerFrame = 8;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mChannelsPerFrame = 2;
				((AudioStreamRangedDescription*)outData)[0].mFormat.mBitsPerChannel = 32;
				((AudioStreamRangedDescription*)outData)[0].mSampleRateRange.mMinimum = 44100.0;
				((AudioStreamRangedDescription*)outData)[0].mSampleRateRange.mMaximum = 44100.0;
			}
			if(theNumberItemsToFetch > 1)
			{
				((AudioStreamRangedDescription*)outData)[1].mFormat.mSampleRate = 48000.0;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mFormatID = kAudioFormatLinearPCM;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mBytesPerPacket = 8;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mFramesPerPacket = 1;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mBytesPerFrame = 8;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mChannelsPerFrame = 2;
				((AudioStreamRangedDescription*)outData)[1].mFormat.mBitsPerChannel = 32;
				((AudioStreamRangedDescription*)outData)[1].mSampleRateRange.mMinimum = 48000.0;
				((AudioStreamRangedDescription*)outData)[1].mSampleRateRange.mMaximum = 48000.0;
			}
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioStreamRangedDescription);
			break;

		default:
			SA_Object::GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
	};
}

void	SA_Device::Stream_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Stream_GetPropertyData() method.
	
	switch(inAddress.mSelector)
	{
		case kAudioStreamPropertyIsActive:
			{
				//	Changing the active state of a stream doesn't affect IO or change the structure
				//	so we can just save the state and send the notification.
				ThrowIf(inDataSize != sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_SetPropertyData: wrong size for the data for kAudioDevicePropertyNominalSampleRate");
				bool theNewIsActive = *reinterpret_cast<const UInt32*>(inData) != 0;
				
				CAMutex::Locker theStateLocker(mStateMutex);
				if(inObjectID == mInputStreamObjectID)
				{
					if(mInputStreamIsActive != theNewIsActive)
					{
						mInputStreamIsActive = theNewIsActive;
					}
				}
				else
				{
					if(mOutputStreamIsActive != theNewIsActive)
					{
						mOutputStreamIsActive = theNewIsActive;
					}
				}
			}
			break;
			
		case kAudioStreamPropertyVirtualFormat:
		case kAudioStreamPropertyPhysicalFormat:
			{
				//	Changing the stream format needs to be handled via the
				//	RequestConfigChange/PerformConfigChange machinery. Note that because this
				//	device only supports 2 channel 32 bit float data, the only thing that can
				//	change is the sample rate.
				ThrowIf(inDataSize != sizeof(AudioStreamBasicDescription), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Stream_SetPropertyData: wrong size for the data for kAudioStreamPropertyPhysicalFormat");
				
				const AudioStreamBasicDescription* theNewFormat = reinterpret_cast<const AudioStreamBasicDescription*>(inData);
				ThrowIf(theNewFormat->mFormatID != kAudioFormatLinearPCM, CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported format ID for kAudioStreamPropertyPhysicalFormat");
				ThrowIf(theNewFormat->mFormatFlags != (kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked), CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported format flags for kAudioStreamPropertyPhysicalFormat");
				ThrowIf(theNewFormat->mBytesPerPacket != 8, CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported bytes per packet for kAudioStreamPropertyPhysicalFormat");
				ThrowIf(theNewFormat->mFramesPerPacket != 1, CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported frames per packet for kAudioStreamPropertyPhysicalFormat");
				ThrowIf(theNewFormat->mBytesPerFrame != 8, CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported bytes per frame for kAudioStreamPropertyPhysicalFormat");
				ThrowIf(theNewFormat->mChannelsPerFrame != 2, CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported channels per frame for kAudioStreamPropertyPhysicalFormat");
				ThrowIf(theNewFormat->mBitsPerChannel != 32, CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported bits per channel for kAudioStreamPropertyPhysicalFormat");
				ThrowIf((theNewFormat->mSampleRate != 44100.0) && (theNewFormat->mSampleRate != 48000.0), CAException(kAudioDeviceUnsupportedFormatError), "SA_Device::Stream_SetPropertyData: unsupported sample rate for kAudioStreamPropertyPhysicalFormat");
			
				//	we need to lock around getting the current sample rate to compare against the new rate
				UInt64 theOldSampleRate = 0;
				{
					CAMutex::Locker theStateLocker(mStateMutex);
					theOldSampleRate = _HW_GetSampleRate();
				}
				
				//	make sure that the new value is different than the old value
				UInt64 theNewSampleRate = static_cast<UInt64>(*reinterpret_cast<const Float64*>(inData));
				if(theNewSampleRate != theOldSampleRate)
				{
					//	we dispatch this so that the change can happen asynchronously
					AudioObjectID theDeviceObjectID = GetObjectID();
					CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
																				SA_PlugIn::Host_RequestDeviceConfigurationChange(theDeviceObjectID, theNewSampleRate, NULL);
																			});
				}
			}
			break;
		
		default:
			SA_Object::SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			break;
	};
}

#pragma mark Control Property Operations

bool	SA_Device::Control_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Control_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioControlPropertyScope:
		case kAudioControlPropertyElement:
		case kAudioLevelControlPropertyScalarValue:
		case kAudioLevelControlPropertyDecibelValue:
		case kAudioLevelControlPropertyDecibelRange:
		case kAudioLevelControlPropertyConvertScalarToDecibels:
		case kAudioLevelControlPropertyConvertDecibelsToScalar:
			theAnswer = true;
			break;
		
		default:
			theAnswer = SA_Object::HasProperty(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

bool	SA_Device::Control_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Control_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioControlPropertyScope:
		case kAudioControlPropertyElement:
		case kAudioLevelControlPropertyDecibelRange:
		case kAudioLevelControlPropertyConvertScalarToDecibels:
		case kAudioLevelControlPropertyConvertDecibelsToScalar:
			theAnswer = false;
			break;
		
		case kAudioLevelControlPropertyScalarValue:
		case kAudioLevelControlPropertyDecibelValue:
			theAnswer = true;
			break;
		
		default:
			theAnswer = SA_Object::IsPropertySettable(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

UInt32	SA_Device::Control_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Control_GetPropertyData() method.
	
	UInt32 theAnswer = 0;
	switch(inAddress.mSelector)
	{
		case kAudioControlPropertyScope:
			theAnswer = sizeof(AudioObjectPropertyScope);
			break;

		case kAudioControlPropertyElement:
			theAnswer = sizeof(AudioObjectPropertyElement);
			break;

		case kAudioLevelControlPropertyScalarValue:
			theAnswer = sizeof(Float32);
			break;

		case kAudioLevelControlPropertyDecibelValue:
			theAnswer = sizeof(Float32);
			break;

		case kAudioLevelControlPropertyDecibelRange:
			theAnswer = sizeof(AudioValueRange);
			break;

		case kAudioLevelControlPropertyConvertScalarToDecibels:
			theAnswer = sizeof(Float32);
			break;

		case kAudioLevelControlPropertyConvertDecibelsToScalar:
			theAnswer = sizeof(Float32);
			break;

		default:
			theAnswer = SA_Object::GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
			break;
	};
	return theAnswer;
}

void	SA_Device::Control_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required.
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.
	
	SInt32 theControlRawValue;
	Float32 theVolumeValue;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyBaseClass:
			//	The base class for kAudioVolumeControlClassID is kAudioLevelControlClassID
			ThrowIf(inDataSize < sizeof(AudioClassID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioObjectPropertyBaseClass for the volume control");
			*reinterpret_cast<AudioClassID*>(outData) = kAudioLevelControlClassID;
			outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyClass:
			//	Volume controls are of the class, kAudioVolumeControlClassID
			ThrowIf(inDataSize < sizeof(AudioClassID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioObjectPropertyClass for the volume control");
			*reinterpret_cast<AudioClassID*>(outData) = kAudioVolumeControlClassID;
			outDataSize = sizeof(AudioClassID);
			break;
			
		case kAudioObjectPropertyOwner:
			//	The control's owner is the device object
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioObjectPropertyOwner for the volume control");
			*reinterpret_cast<AudioObjectID*>(outData) = GetObjectID();
			outDataSize = sizeof(AudioObjectID);
			break;
			
		case kAudioControlPropertyScope:
			//	This property returns the scope that the control is attached to.
			ThrowIf(inDataSize < sizeof(AudioObjectPropertyScope), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioControlPropertyScope for the volume control");
			*reinterpret_cast<AudioObjectPropertyScope*>(outData) = (inObjectID == mInputMasterVolumeControlObjectID) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
			outDataSize = sizeof(AudioObjectPropertyScope);
			break;

		case kAudioControlPropertyElement:
			//	This property returns the element that the control is attached to.
			ThrowIf(inDataSize < sizeof(AudioObjectPropertyElement), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioControlPropertyElement for the volume control");
			*reinterpret_cast<AudioObjectPropertyElement*>(outData) = kAudioObjectPropertyElementMaster;
			outDataSize = sizeof(AudioObjectPropertyElement);
			break;

		case kAudioLevelControlPropertyScalarValue:
			//	This returns the value of the control in the normalized range of 0 to 1.
			{
				ThrowIf(inDataSize < sizeof(Float32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioLevelControlPropertyScalarValue for the volume control");
				CAMutex::Locker theStateLocker(mStateMutex);
				theControlRawValue = _HW_GetVolumeControlValue((inObjectID == mInputMasterVolumeControlObjectID) ? kSimpleAudioDriver_Control_MasterInputVolume : kSimpleAudioDriver_Control_MasterOutputVolume);
				*reinterpret_cast<Float32*>(outData) = mVolumeCurve.ConvertRawToScalar(theControlRawValue);
				outDataSize = sizeof(Float32);
			}
			break;

		case kAudioLevelControlPropertyDecibelValue:
			//	This returns the dB value of the control.
			{
				ThrowIf(inDataSize < sizeof(Float32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
				CAMutex::Locker theStateLocker(mStateMutex);
				theControlRawValue = _HW_GetVolumeControlValue((inObjectID == mInputMasterVolumeControlObjectID) ? kSimpleAudioDriver_Control_MasterInputVolume : kSimpleAudioDriver_Control_MasterOutputVolume);
				*reinterpret_cast<Float32*>(outData) = mVolumeCurve.ConvertRawToDB(theControlRawValue);
				outDataSize = sizeof(Float32);
			}
			break;

		case kAudioLevelControlPropertyDecibelRange:
			//	This returns the dB range of the control.
			ThrowIf(inDataSize < sizeof(AudioValueRange), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelRange for the volume control");
			reinterpret_cast<AudioValueRange*>(outData)->mMinimum = mVolumeCurve.GetMinimumDB();
			reinterpret_cast<AudioValueRange*>(outData)->mMaximum = mVolumeCurve.GetMaximumDB();
			outDataSize = sizeof(AudioValueRange);
			break;

		case kAudioLevelControlPropertyConvertScalarToDecibels:
			//	This takes the scalar value in outData and converts it to dB.
			ThrowIf(inDataSize < sizeof(Float32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
			
			//	clamp the value to be between 0 and 1
			theVolumeValue = *reinterpret_cast<Float32*>(outData);
			theVolumeValue = std::min(1.0f, std::max(0.0f, theVolumeValue));
			
			//	do the conversion
			*reinterpret_cast<Float32*>(outData) = mVolumeCurve.ConvertScalarToDB(theVolumeValue);
			
			//	report how much we wrote
			outDataSize = sizeof(Float32);
			break;

		case kAudioLevelControlPropertyConvertDecibelsToScalar:
			//	This takes the dB value in outData and converts it to scalar.
			ThrowIf(inDataSize < sizeof(Float32), CAException(kAudioHardwareBadPropertySizeError), "SA_Device::Control_GetPropertyData: not enough space for the return value of kAudioLevelControlPropertyDecibelValue for the volume control");
			
			//	clamp the value to be between kVolume_MinDB and kVolume_MaxDB
			theVolumeValue = *reinterpret_cast<Float32*>(outData);
			theVolumeValue = std::min(kSimpleAudioDriver_Control_MaxDbVolumeValue, std::max(kSimpleAudioDriver_Control_MinDBVolumeValue, theVolumeValue));
			
			//	do the conversion
			*reinterpret_cast<Float32*>(outData) = mVolumeCurve.ConvertDBToScalar(theVolumeValue);
			
			//	report how much we wrote
			outDataSize = sizeof(Float32);
			break;

		default:
			SA_Object::GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
	};
}

void	SA_Device::Control_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Control_GetPropertyData() method.
	
	bool sendNotifications = false;
	kern_return_t theError = 0;
	Float32 theNewVolumeValue;
	SInt32 theNewRawVolumeValue;
	switch(inAddress.mSelector)
	{
		case kAudioLevelControlPropertyScalarValue:
			//	For the scalar volume, we clamp the new value to [0, 1]. Note that if this
			//	value changes, it implies that the dB value changed too.
			{
				ThrowIf(inDataSize != sizeof(Float32), CAException(kAudioHardwareBadPropertySizeError), "NullAudio_SetControlPropertyData: wrong size for the data for kAudioLevelControlPropertyScalarValue");
				theNewVolumeValue = *((const Float32*)inData);
				theNewVolumeValue = std::min(1.0f, std::max(0.0f, theNewVolumeValue));
				theNewRawVolumeValue = mVolumeCurve.ConvertScalarToRaw(theNewVolumeValue);
				CAMutex::Locker theStateLocker(mStateMutex);
				theError = _HW_SetVolumeControlValue((inObjectID == mInputMasterVolumeControlObjectID) ? kSimpleAudioDriver_Control_MasterInputVolume : kSimpleAudioDriver_Control_MasterOutputVolume, theNewRawVolumeValue);
				sendNotifications = theError == 0;
			}
			break;
		
		case kAudioLevelControlPropertyDecibelValue:
			//	For the dB value, we first convert it to a scalar value since that is how
			//	the value is tracked. Note that if this value changes, it implies that the
			//	scalar value changes as well.
			{
				ThrowIf(inDataSize != sizeof(Float32), CAException(kAudioHardwareBadPropertySizeError), "NullAudio_SetControlPropertyData: wrong size for the data for kAudioLevelControlPropertyScalarValue");
				theNewVolumeValue = *((const Float32*)inData);
				theNewVolumeValue = std::min(kSimpleAudioDriver_Control_MaxDbVolumeValue, std::max(kSimpleAudioDriver_Control_MinDBVolumeValue, theNewVolumeValue));
				theNewRawVolumeValue = mVolumeCurve.ConvertDBToRaw(theNewVolumeValue);
				CAMutex::Locker theStateLocker(mStateMutex);
				theError = _HW_SetVolumeControlValue((inObjectID == mInputMasterVolumeControlObjectID) ? kSimpleAudioDriver_Control_MasterInputVolume : kSimpleAudioDriver_Control_MasterOutputVolume, theNewRawVolumeValue);
				sendNotifications = theError == 0;
			}
			break;
		
		default:
			SA_Object::SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			break;
	};
	
	if(sendNotifications)
	{
		CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
																	AudioObjectPropertyAddress theChangedProperties[] = { { kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster }, { kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster } };
																	SA_PlugIn::Host_PropertiesChanged(inObjectID, 2, theChangedProperties);
																});
	}
}

#pragma mark IO Operations

void	SA_Device::StartIO()
{
	//	Starting/Stopping IO needs to be reference counted due to the possibility of multiple clients starting IO
	CAMutex::Locker theStateLocker(mStateMutex);
	
	//	make sure we can start
	ThrowIf(mStartCount == UINT64_MAX, CAException(kAudioHardwareIllegalOperationError), "SA_Device::StartIO: failed to start because the ref count was maxxed out already");
	
	//	we only tell the hardware to start if this is the first time IO has been started
	if(mStartCount == 0)
	{
        gDevice_NumberTimeStamps = 0;
        gDevice_AnchorSampleTime = 0.0;
        gDevice_AnchorHostTime = mach_absolute_time();

		kern_return_t theError = _HW_StartIO();
		ThrowIfKernelError(theError, CAException(theError), "SA_Device::StartIO: failed to start because of an error calling down to the driver");
	}
	++mStartCount;
}

void	SA_Device::StopIO()
{
	//	Starting/Stopping IO needs to be reference counted due to the possibility of multiple clients starting IO
	CAMutex::Locker theStateLocker(mStateMutex);
	
	//	we tell the hardware to stop if this is the last stop call
	if(mStartCount == 1)
	{
		_HW_StopIO();
		mStartCount = 0;
	}
	else if(mStartCount > 1)
	{
		--mStartCount;
	}
}

void	SA_Device::GetZeroTimeStamp(Float64& outSampleTime, UInt64& outHostTime, UInt64& outSeed)
{
    //  calculate the next host time
    Float64 theHostTicksPerRingBuffer = gDevice_HostTicksPerFrame * ((Float64)mRingBufferFrameSize);
    Float64 theHostTickOffset = ((Float64)(gDevice_NumberTimeStamps + 1)) * theHostTicksPerRingBuffer;
    UInt64 theNextHostTime = gDevice_AnchorHostTime + ((UInt64)theHostTickOffset);

    //  go to the next time if the next host time is less than the current time
    if(theNextHostTime <= mach_absolute_time())
    {
        ++gDevice_NumberTimeStamps;
    }

    //  set the return values
    outSampleTime = gDevice_NumberTimeStamps * mRingBufferFrameSize;
    outHostTime = gDevice_AnchorHostTime + (((Float64)gDevice_NumberTimeStamps) * theHostTicksPerRingBuffer);
    outSeed = 1;
}

void	SA_Device::WillDoIOOperation(UInt32 inOperationID, bool& outWillDo, bool& outWillDoInPlace) const
{
	switch(inOperationID)
	{
		case kAudioServerPlugInIOOperationReadInput:
		case kAudioServerPlugInIOOperationWriteMix:
			outWillDo = true;
			outWillDoInPlace = true;
			break;
			
		case kAudioServerPlugInIOOperationThread:
		case kAudioServerPlugInIOOperationCycle:
		case kAudioServerPlugInIOOperationConvertInput:
		case kAudioServerPlugInIOOperationProcessInput:
		case kAudioServerPlugInIOOperationProcessOutput:
		case kAudioServerPlugInIOOperationMixOutput:
		case kAudioServerPlugInIOOperationProcessMix:
		case kAudioServerPlugInIOOperationConvertMix:
		default:
			outWillDo = false;
			outWillDoInPlace = true;
			break;
			
	};
}

void	SA_Device::BeginIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo)
{
	#pragma unused(inOperationID, inIOBufferFrameSize, inIOCycleInfo)
}

void	SA_Device::DoIOOperation(AudioObjectID inStreamObjectID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
	#pragma unused(inStreamObjectID, ioSecondaryBuffer)
	switch(inOperationID)
	{
		case kAudioServerPlugInIOOperationReadInput:
			//ReadInputData(inIOBufferFrameSize, inIOCycleInfo.mInputTime.mSampleTime, ioMainBuffer);
			break;
			
		case kAudioServerPlugInIOOperationWriteMix:
			//WriteOutputData(inIOBufferFrameSize, inIOCycleInfo.mOutputTime.mSampleTime, ioMainBuffer);
            SA_WriteTo(inIOBufferFrameSize, inIOCycleInfo.mOutputTime.mSampleTime, ioMainBuffer);
			break;
	};
}

void	SA_Device::EndIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo)
{
	#pragma unused(inOperationID, inIOBufferFrameSize, inIOCycleInfo)
}

void	SA_Device::ReadInputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, void* outBuffer)
{
	//	we need to be holding the IO lock to do this
	CAMutex::Locker theIOLocker(mIOMutex);
	
	//	figure out where we are starting
	UInt64 theSampleTime = static_cast<UInt64>(inSampleTime);
	UInt32 theStartFrameOffset = theSampleTime % mRingBufferFrameSize;
	
	//	figure out how many frames we need to copy
	UInt32 theNumberFramesToCopy1 = inIOBufferFrameSize;
	UInt32 theNumberFramesToCopy2 = 0;
	if((theStartFrameOffset + theNumberFramesToCopy1) > mRingBufferFrameSize)
	{
		theNumberFramesToCopy1 = mRingBufferFrameSize - theStartFrameOffset;
		theNumberFramesToCopy2 = inIOBufferFrameSize - theNumberFramesToCopy1;
	}
	
	//	do the copying (the byte sizes here assume a 16 bit stereo sample format)
	Byte* theDestination = reinterpret_cast<Byte*>(outBuffer);
	memcpy(theDestination, mInputStreamRingBuffer + (theStartFrameOffset * 4), theNumberFramesToCopy1 * 4);
	if(theNumberFramesToCopy2 > 0)
	{
		memcpy(theDestination + (theNumberFramesToCopy1 * 4), mInputStreamRingBuffer, theNumberFramesToCopy2 * 4);
	}
}

void	SA_Device::WriteOutputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer)
{
	//	we need to be holding the IO lock to do this
	CAMutex::Locker theIOLocker(mIOMutex);
	
	//	figure out where we are starting
	UInt64 theSampleTime = static_cast<UInt64>(inSampleTime);
	UInt32 theStartFrameOffset = theSampleTime % mRingBufferFrameSize;
	
	//	figure out how many frames we need to copy
	UInt32 theNumberFramesToCopy1 = inIOBufferFrameSize;
	UInt32 theNumberFramesToCopy2 = 0;
	if((theStartFrameOffset + theNumberFramesToCopy1) > mRingBufferFrameSize)
	{
		theNumberFramesToCopy1 = mRingBufferFrameSize - theStartFrameOffset;
		theNumberFramesToCopy2 = inIOBufferFrameSize - theNumberFramesToCopy1;
	}
	
	//	do the copying (the byte sizes here assume a 16 bit stereo sample format)
	const Byte* theSource = reinterpret_cast<const Byte*>(inBuffer);
	memcpy(mOutputStreamRingBuffer + (theStartFrameOffset * 4), theSource, theNumberFramesToCopy1 * 4);
	if(theNumberFramesToCopy2 > 0)
	{
		memcpy(mOutputStreamRingBuffer, theSource + (theNumberFramesToCopy1 * 4), theNumberFramesToCopy2 * 4);
	}
}

void SA_Device::SA_WriteTo(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer)
{
	//	we need to be holding the IO lock to do this
	CAMutex::Locker theIOLocker(mIOMutex);
	
    //  figure out where we are starting
    UInt64 theSampleTime = static_cast<UInt64>(inSampleTime);
    UInt32 theStartFrameOffset = (theSampleTime) % (STRBUFNUM/2);

    //  figure out how many frames we need to copy
    UInt32 theNumberFramesToCopy1 = inIOBufferFrameSize;
    UInt32 theNumberFramesToCopy2 = 0;
    if((theStartFrameOffset + theNumberFramesToCopy1) > (STRBUFNUM/2))
    {
        theNumberFramesToCopy1 = (STRBUFNUM/2) - theStartFrameOffset;
        theNumberFramesToCopy2 = inIOBufferFrameSize - theNumberFramesToCopy1;
    }

    //  do the copying (the byte sizes here assume a 16 bit stereo sample format)
    const Byte* theSource = reinterpret_cast<const Byte*>(inBuffer);
    memcpy(buf_up+theStartFrameOffset*2, theSource, theNumberFramesToCopy1 * 8);
    if(theNumberFramesToCopy2 > 0)
    {
        memcpy(buf_up, theSource + (theNumberFramesToCopy1 * 8), theNumberFramesToCopy2 * 8);
    }
    (*uwp) = ((theStartFrameOffset + inIOBufferFrameSize)*2) & (STRBUFNUM-1);
}

#pragma mark Hardware Accessors

CFStringRef	SA_Device::HW_CopyDeviceUID()
{
	CFStringRef theAnswer = CFSTR(kDeviceUID);
	return theAnswer;
}

void	SA_Device::_HW_Open()
{
	//	get the sample rate, ring buffer size, and control values to prime the shadows
    mRingBufferFrameSize = STRBUFNUM / 2;
	_HW_GetSampleRate();
	_HW_GetVolumeControlValue(kSimpleAudioDriver_Control_MasterInputVolume);
	_HW_GetVolumeControlValue(kSimpleAudioDriver_Control_MasterOutputVolume);
}

void	SA_Device::_HW_Close()
{
    return;
}

kern_return_t	SA_Device::_HW_StartIO()
{
	return 0;
}

void	SA_Device::_HW_StopIO()
{
	return;
}

UInt64	SA_Device::_HW_GetSampleRate() const
{
	return mSampleRateShadow;
}

kern_return_t	SA_Device::_HW_SetSampleRate(UInt64 inNewSampleRate)
{
    mSampleRateShadow = inNewSampleRate;
	return 0;
}

SInt32	SA_Device::_HW_GetVolumeControlValue(int inControlID) const
{
    switch(inControlID)
    {
        case kSimpleAudioDriver_Control_MasterInputVolume:
            return mInputMasterVolumeControlRawValueShadow;

        case kSimpleAudioDriver_Control_MasterOutputVolume:
            return mOutputMasterVolumeControlRawValueShadow;
    }
    return 0;
}

kern_return_t	SA_Device::_HW_SetVolumeControlValue(int inControlID, SInt32 inNewControlValue)
{
    switch(inControlID)
    {
        case kSimpleAudioDriver_Control_MasterInputVolume:
            mInputMasterVolumeControlRawValueShadow = inNewControlValue;
            break;

        case kSimpleAudioDriver_Control_MasterOutputVolume:
            mOutputMasterVolumeControlRawValueShadow = inNewControlValue;
            break;
    }
    return 0;
}

#pragma mark Implementation

void	SA_Device::PerformConfigChange(UInt64 inChangeAction, void* inChangeInfo)
{
	#pragma unused(inChangeInfo)
	
	//	this device only supports chagning the sample rate, which is stored in inChangeAction
	UInt64 theNewSampleRate = inChangeAction;
	
	//	make sure we support the new sample rate
	if((theNewSampleRate == 44100) || (theNewSampleRate == 48000))
	{
		//	we need to lock the state lock around telling the hardware about the new sample rate
		CAMutex::Locker theStateLocker(mStateMutex);
		_HW_SetSampleRate(theNewSampleRate);

        //  calculate the host ticks per frame
        struct mach_timebase_info theTimeBaseInfo;
        mach_timebase_info(&theTimeBaseInfo);
        Float64 theHostClockFrequency = theTimeBaseInfo.denom / theTimeBaseInfo.numer;
        theHostClockFrequency *= 1000000000.0;
        gDevice_HostTicksPerFrame = theHostClockFrequency / theNewSampleRate;
	}
}

void	SA_Device::AbortConfigChange(UInt64 inChangeAction, void* inChangeInfo)
{
	#pragma unused(inChangeAction, inChangeInfo)
	
	//	this device doesn't need to do anything special if a change request gets aborted
}

int SA_Device::JR_JackInit() {
    struct stat stat;

    syslog(LOG_INFO, "JackRouter: Initializing shared memory to communicate with jack.");
    shm_fd = shm_open("/jackrouter", O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
    if (shm_fd < 0) {
        return -1;
    }

    if (fstat(shm_fd, &stat) < 0) {
        return -1;
    }

    if (stat.st_size == 0) {
        if (ftruncate(shm_fd, JACK_SHMSIZE) == -1) {
            return -1;
        }
    }

    char* shm_base = (char*)mmap(NULL, JACK_SHMSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) {
        return -1;
    }

    uwp = (unsigned short*)shm_base;
    urp = ((unsigned short*)shm_base)+1;
    dwp = ((unsigned short*)shm_base)+2;
    drp = ((unsigned short*)shm_base)+3;
    buf_up = (sample_t*)(shm_base + 0x1000);
    buf_down = buf_up + STRBUFNUM;

    syslog(LOG_WARNING, "JackRouter: Share Memory initialized. (%016llx:%016llx:%016llx:%016llx)",
           dwp, uwp, buf_up, buf_down);
    return 0;
}

