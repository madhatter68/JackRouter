# Build JackBridge
g++ -Wall -std=c++11 -D__MACOSX_CORE__ -o JackBridge JackBridge.cpp jackClient.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -ljack

# Buld JackBridge with MIDI support
g++ -Wall -std=c++11 -D__MACOSX_CORE__ -D_WITH_MIDI_BRIDGE_ -o JackBridgeWithMidi JackBridge.cpp jackClient.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -ljack -lrtmidi
