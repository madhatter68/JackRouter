# JackRouter

## About
This is yet another jackrouter implementation for MacOS. Note that it's just
early prototype for reference at this moment. Please be careful to use it.

## Limitation
- Supports only 44.1/48kHz mode.

## Build
JackRouter consists of two parts, a daemon and a user-space Core Audio driver.

- jackrouter daemon

  libjack and [rtmidi](http://www.music.mcgill.ca/~gary/rtmidi/) libraries are required.
  Please install before build. To build the jackrouter, just run 'build.sh' under the directory.

```
cd daemon
./build.sh
```

- SimpleAudio driver

  Build the project named "AudioDriverExamples.xcodeproj" with Xcode.

## Installation
- jackrouter daemon

  Locate wherever you like. Just execute after jackd.

- SimpleAudio driver

  Copy all contents to '/Library/Audio/Plug-Ins/HAL' and restart coreaudiod.

```
sudo cp -r SimpleAudioPlugIn.driver /Library/Audio/Plug-Ins/HAL
sudo -u _coreaudiod killall coreaudiod
```

## TODO
- Multi instance support
