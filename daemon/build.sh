# Build JackBridge
g++ -Wall -std=c++11 -D__MACOSX_CORE__ -o JackBridge JackBridge.cpp jackClient.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -ljack
