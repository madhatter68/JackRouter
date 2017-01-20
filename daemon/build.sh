# Buld jackrouter
g++ -Wall -D__MACOSX_CORE__ -o jackrouter jackrouter.cpp jackClient.cpp -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -ljack -lrtmidi
