/*
MIT License

Copyright (c) 2016 Shunji Uno <madhatter68@linux-dtm.ivory.ne.jp>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <jack/jack.h>
#include <jack/midiport.h>
#include "audio.hpp"
#include "midi.hpp"

#ifndef __JACKCLIENT_HPP__
#define __JACKCLIENT_HPP__
#define MAX_PORT_NUM 16

/**********************************************************************
 Jack functions
**********************************************************************/
class JackClient {
private:
    void finalize_audio_ports();
    void setAudioFormat();
    int nAudioIn, nAudioOut, nMidiIn, nMidiOut;
    audioStream *audioInStream[MAX_PORT_NUM], *audioOutStream[MAX_PORT_NUM];
    midiStream *midiInStream[MAX_PORT_NUM], *midiOutStream[MAX_PORT_NUM];

public:
    audioFormat format;
    jack_client_t* client;
    jack_port_t *audioIn[MAX_PORT_NUM], *audioOut[MAX_PORT_NUM];
    jack_port_t *midiIn[MAX_PORT_NUM], *midiOut[MAX_PORT_NUM];

    JackClient(const char* name, const char* nameAin[], const char* nameAout[],
        const char* nameMin[], const char* nameMout[]);
    ~JackClient();
    void activate();
    int bindPorts(audioStream**, audioStream**, midiStream**, midiStream**);
    virtual int process_callback(jack_nframes_t nframes);
};
#endif
