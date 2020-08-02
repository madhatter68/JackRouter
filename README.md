# JackBridge (former "JackRouter")
## About
This is an alternative to jackrouter for MacOS. JackBridge acts as a virtual
audio interface (currently 2in-4out) connected to Jackaudio server directly.
Even though the master clock of JackBridge becomes synchronized with Jack 
server, Core Audio Applications connected via JackBridge is out of jackaudio
connection graph scope. Therefore, I changed the name from "Router" to "Bridge".

NOTE: This is still experimental prototype implementation. Please be careful using it.

## Changes
- Master clock synchronization with Jack server

## Limitation
- Supports only 44.1/48kHz mode.

## Build
Checkout the codes in "JackBridge" branch.

```
git checkout JackBridge
```

JackBridge consists of two parts, a daemon and a user-space Core Audio driver.

- JackBridge daemon

  You can build two versions of daemon.

  JackBridge: bridge only audio (RtMidi library not required)
  JackBridgeWithMidi: bridge audio and MIDI (RtMidi library required)

  [rtmidi](http://www.music.mcgill.ca/~gary/rtmidi/) libraries are required to build
  JackBridgeWithMidi. Please install before build.
  To build the JackBridge, just run 'build.sh' under the directory.

```
cd daemon
./build.sh
```

- JackBridge driver

  Build the project named "JackBridgePlugIn.xcodeproj" with Xcode.

## Installation
- JackBridge daemon

  Locate wherever you like. Just execute after jackd.

- JackBridgePlugIn driver

  Copy all contents to '/Library/Audio/Plug-Ins/HAL' and restart coreaudiod.

```
sudo cp -r JackBridgePlugIn.driver /Library/Audio/Plug-Ins/HAL
sudo -u _coreaudiod killall coreaudiod
```

  Then you can see JackBridge device on your application. And you can
  also change configuration with Audio MIDI setup application.

## TODO
- Multi instance support

## Download
The pre-built binaries can be downloaded from http://linux-dtm.ivory.ne.jp/downloads/MacOS/JackBridge.zip
