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

#ifndef __JACKCLIENT_HPP__
#define __JACKCLIENT_HPP__
#define MAX_PORT_NUM 16
typedef jack_default_audio_sample_t sample_t;

#define JACK_PROCESS_CALLBACK      0x0001
#define JACK_FREEWHEEL_CALLBACK    0x0002
#define JACK_BUFFER_SIZE_CALLBACK  0x0004
#define JACK_SAMPLE_RATE_CALLBACK  0x0008
#define JACK_CLIENT_REG_CALLBACK   0x0010
#define JACK_PORT_REG_CALLBACK     0x0020
#define JACK_PORT_RENAME_CALLBACK  0x0040
#define JACK_PORT_CONNECT_CALLBACK 0x0080
#define JACK_GRAPH_ORDER_CALLBACK  0x0100
#define JACK_XRUN_CALLBACK         0x0200
#define JACK_LATENCY_CALLBACK      0x0400
#define JACK_SYNC_CALLBACK         0x0800
#define JACK_TIMEBASE_CALLBACK     0x1000

/**********************************************************************
 Jack functions
**********************************************************************/
class JackClient {
protected:
    jack_client_t* client;
    jack_port_t *audioIn[MAX_PORT_NUM], *audioOut[MAX_PORT_NUM];
    jack_port_t *midiIn[MAX_PORT_NUM], *midiOut[MAX_PORT_NUM];

    int nAudioIn, nAudioOut, nMidiIn, nMidiOut;
    int SampleRate;
    jack_nframes_t BufSize;
    bool is_master;

    virtual int process_callback(jack_nframes_t nframes);
    virtual int sync_callback(jack_transport_state_t state, jack_position_t *pos);
    virtual void timebase_callback(jack_transport_state_t state, jack_nframes_t nframes,
                                       jack_position_t *pos, int new_pos);

    // Transport API
    void transport_start();
    void transport_stop();
    jack_transport_state_t transport_query(jack_position_t* pos);
    int transport_reposition(const jack_position_t* pos);

private:
    uint32_t cb_flags;
    static int _process_callback(jack_nframes_t nframes, void *arg);
    static int _sync_callback(jack_transport_state_t state, jack_position_t *pos, void *arg);
    static void _timebase_callback(jack_transport_state_t state, jack_nframes_t nframes,
                          jack_position_t *pos, int new_pos, void *arg);

public:
    JackClient(const char* name, uint32_t cb_flags);
    ~JackClient();
    void activate();

    int register_ports(const char* nameAin[], const char* nameAout[],
                   const char* nameMin[], const char* nameMout[]);
};
#endif
