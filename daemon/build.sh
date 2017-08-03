# Buld jackrouter
g++ -Wall -std=c++11 -D__MACOSX_CORE__ -o jackrouter jackrouter.cpp jackClient.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -ljack -lrtmidi
