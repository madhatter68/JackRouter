AudioDriverExamples

This project has two examples of writing user land audio drivers that conform to the plug-in API in <CoreAudio/AudioServerPlugIn.h>. Each example is documented with commentary inline with the code.

The first example, NullAudio, creates a driver that supports single audio device. Written in C, this example shows what it takes to write a drive that achieves the bare minimum of support while still being fully functional as an AudioDevice.

The Installation Directory in Xcode for the NullAudio target is set to:
$(LOCAL_LIBRARY_DIR)/Audio/Plug-Ins/HAL

This translates to /Library/Audio/Plug-Ins/HAL and should be the location of the build .driver

The second example, SimpleAudio, is a more functional driver. Written in C++, this driver is written for a dynamic environment where it has to support potentially many instances of the same device getting plugged into the system. This example also shows how a user-land driver interacts with hardware that requires a kernel extension to talk to. As such, it shows dealing with IOKit matching notifications as well as dealing with calls into the IOKit driver.

The SimpleAudio example contains two parts: The SimpleAudioPlugin and the SimpleAudioDriver kext.

The Installation Directory in Xcode for the SimpleAudioPlugIn target is set to:
$(LOCAL_LIBRARY_DIR)/Audio/Plug-Ins/HAL

This translates to /Library/Audio/Plug-Ins/HAL and should be the location of the built .driver

The Installation Directory in Xcode for the SimpleAudioDriver target is set to:
$(SYSTEM_LIBRARY_DIR)/Extensions

This translates to /System/Library/Extensions and should be the location of the built .kext

Use kextload to load the .kext:

audiobox:Extensions stumpyjoe$ sudo chown -R root:wheel SimpleAudioDriver.kextPassword:audiobox:Extensions stumpyjoe$ sudo kextload SimpleAudioDriver.kext

Note: To build the SimpleAudioDriver target for deployment on OS X 10.8 you will need to change the Base SDK setting in the target build settings to "OS X 10.8".