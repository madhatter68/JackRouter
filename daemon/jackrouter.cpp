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

#include <iostream>
#include <unistd.h>
#include <string>
#include <sstream>
#include <cstdlib>
#include <rtmidi/RtMidi.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include "coreAudio.hpp"
#include "jackClient.hpp"

static const char* nameAin[]= {"input_L", "input_R", NULL};
static const char* nameAout[] = {"output_L", "output_R", NULL};
static const char* nameMin[] = {"event_in", NULL};
static const char* nameMout[] = {"event_out", NULL};

class JackRouterIn : public JackClient {
private:
    RtMidiOut  *midiout;
    coreAudioStream *audioStream;

public:
    JackRouterIn(const char* name) : JackClient(name, nameAin, NULL, nameMin, NULL) {
        try {
            midiout = new RtMidiOut(RtMidi::MACOSX_CORE);
            midiout->openVirtualPort(name);
        } catch ( RtMidiError &error ) {
            error.printMessage();
            exit( EXIT_FAILURE );
        }
    }

    ~JackRouterIn() {
    }

    int process_callback(jack_nframes_t nframes) override {
        sample_t *ain[2];    // for Audio
        void *min;              // for MIDI
        int count;
        jack_midi_event_t event;
        std::vector< unsigned char > message;
#if 0
        if (audioStream) {
            ain[0] = (sample_t*)jack_port_get_buffer(audioIn[0], nframes);
            ain[1] = (sample_t*)jack_port_get_buffer(audioIn[1], nframes);
            audioStream->sendToDownstream(ain, nframes);
        }
#endif
        min = jack_port_get_buffer(midiIn[0], nframes);
        count = jack_midi_get_event_count(min);
        for(int i=0; i<count; i++) {
            jack_midi_event_get(&event, min, i);
            message.clear();
            for (int j=0; j<event.size; j++) {
                message.push_back(event.buffer[j]);
            }
            if (message.size() > 0) {
                midiout->sendMessage(&message);
            }
        }
        return 0;
    }

    int bindPorts(coreAudioStream *ain) {
        audioStream = ain;
        return 0;
    }

};

class JackRouterOut : public JackClient {
private:
    RtMidiIn  *midiin;
    coreAudioStream *audioStream;

public:
    JackRouterOut(const char* name) : JackClient(name, NULL, nameAout, NULL, nameMout) {
        try {
            midiin = new RtMidiIn(RtMidi::MACOSX_CORE);
            midiin->openVirtualPort(name);
            midiin->ignoreTypes(false, false, false);
        } catch ( RtMidiError &error ) {
            error.printMessage();
            exit( EXIT_FAILURE );
        }
    }

    ~JackRouterOut() {
    }

    int process_callback(jack_nframes_t nframes) override {
        sample_t *aout[2];    // for Audio
        void *mout;             // for MIDI
        std::vector< unsigned char > message;
        jack_midi_data_t* buf;

        if (audioStream) {
            aout[0] = (sample_t*)jack_port_get_buffer(audioOut[0], nframes);
            aout[1] = (sample_t*)jack_port_get_buffer(audioOut[1], nframes);
            audioStream->receiveFromUpstream(aout, nframes);
        }
        mout = jack_port_get_buffer(midiOut[0], nframes);
        jack_midi_clear_buffer(mout);
        midiin->getMessage(&message);
        while(message.size() > 0) {
            buf = jack_midi_event_reserve(mout, 0, message.size());
            if (buf != NULL) {
                for(int i=0; i<message.size(); i++) {
                    buf[i] = message[i];
                }
            } else {
                fprintf(stderr, "ERROR: jack_midi_event_reserve failed()\n");
            }
            midiin->getMessage(&message);
        }
        return 0;
    }

    int bindPorts(coreAudioStream *aout) {
        audioStream = aout;
        return 0;
    }
};

JackRouterIn*  jackIn;
JackRouterOut* jackOut;

int
main(int argc, char** argv)
{
    // Create instances of jack client
    jackIn = new JackRouterIn("JackRouter input");
    jackOut = new JackRouterOut("JackRouter output");

    // exchange buffer for audio
    coreAudioStream* audioStream = new coreAudioStream(jackIn->format);

    jackIn->bindPorts(audioStream);
    jackOut->bindPorts(audioStream);

    // activate gateway from/to jack ports
    jackIn->activate();
    jackOut->activate();

    // Infinite loop until daemon is killed.
    while(1) {
        sleep(600);
    }

    return 0;
}
