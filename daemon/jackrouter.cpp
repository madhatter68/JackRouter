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
#include "audio.hpp"
#include "midi.hpp"
#include "coreAudio.hpp"
#include "jackClient.hpp"

static const char* nameAin[]= {"input_L", "input_R", NULL};
static const char* nameAout[] = {"output_L", "output_R", NULL};

#define MAX_MIDI_PORTS 256

class JackRouterIn : public JackClient {
private:
    RtMidiOut  **midiout;
    coreAudioStream *audioStream;
    const char** ports;
    int nPorts;
    char** nameMin;

    int get_num_ports() {
        int num;
        ports = jack_get_ports(client, "system", ".*raw midi", JackPortIsOutput);
        if (!ports) {
            return 0;
        }
        for(num=0;*ports != NULL; ports++,num++) {
            //jack_port_t* p = jack_port_by_name(client, *ports);
            //std::cout << ";" << *ports << ";" << jack_port_short_name(p) << ";" << jack_port_type(p) << std::endl;
        }
        return num;
    }

    void connect_ports() {
    }

public:
    audioFormat format;

    JackRouterIn(const char* name) : JackClient(name, JACK_PROCESS_CALLBACK) {
        char buf[256];
        nPorts = get_num_ports();
        midiout = (RtMidiOut**)malloc(sizeof(RtMidiOut*)*nPorts);
        nameMin = (char**)malloc(sizeof(char*)*(nPorts+1));

        for(int n=0; n<nPorts; n++) {
            try {
                midiout[n] = new RtMidiOut(RtMidi::MACOSX_CORE);
                snprintf(buf, 256, "%s %d", name, n+1);
                midiout[n]->openVirtualPort(buf);
            } catch ( RtMidiError &error ) {
                error.printMessage();
                exit( EXIT_FAILURE );
            }

            nameMin[n] = (char*)malloc(256);
            snprintf(nameMin[n], 256, "event_in_%d", n+1);
        }
        nameMin[nPorts] = NULL;
 
        // Register Audio/MIDI ports depending on the number of system MIDI ports
        register_ports(nameAin, NULL, (const char**)nameMin, NULL);
        connect_ports();
    }

    ~JackRouterIn() {
        for(int n=0; n<nPorts; n++) {
            delete midiout[n];
            free(nameMin[n]);
        }
        free(midiout);
        free(nameMin);
    }

    int process_callback(jack_nframes_t nframes) override {
        sample_t *ain[2];    // for Audio
        void *min;              // for MIDI
        int count;
        jack_midi_event_t event;
        std::vector< unsigned char > message;

        if (audioStream) {
            ain[0] = (sample_t*)jack_port_get_buffer(audioIn[0], nframes);
            ain[1] = (sample_t*)jack_port_get_buffer(audioIn[1], nframes);
            audioStream->sendToDownstream(ain, nframes);
        }

        for(int n=0; n<nPorts; n++) {
            min = jack_port_get_buffer(midiIn[n], nframes);
            count = jack_midi_get_event_count(min);
            for(int i=0; i<count; i++) {
                jack_midi_event_get(&event, min, i);
                message.clear();
                for (int j=0; j<event.size; j++) {
                    message.push_back(event.buffer[j]);
                }
                if (message.size() > 0) {
                    midiout[n]->sendMessage(&message);
                }
            }
        }
        return 0;
    }

    int bindPorts(coreAudioStream *ain) {
        audioStream = ain;
        return 0;
    }

    void setAudioFormat() {
        format.SampleRate = SampleRate;
        format.bitsPerSample = 32;
        format.SamplesPerFrame = 256;
    }
};

class JackRouterOut : public JackClient {
private:
    RtMidiIn  **midiin;
    coreAudioStream *audioStream;
    const char** ports;
    int nPorts;
    char** nameMout;

    int get_num_ports() {
        int num;
        ports = jack_get_ports(client, "system", ".*raw midi", JackPortIsInput);
        if (!ports) {
            return 0;
        }
        for(num=0;*ports != NULL; ports++,num++) {
            //jack_port_t* p = jack_port_by_name(client, *ports);
            //std::cout << ";" << *ports << ";" << jack_port_short_name(p) << ";" << jack_port_type(p) << std::endl;
        }
        return num;
    }

    void connect_ports() {
    }

public:
    JackRouterOut(const char* name) : JackClient(name, JACK_PROCESS_CALLBACK) {
        char buf[256];
        nPorts = get_num_ports();
        midiin = (RtMidiIn**)malloc(sizeof(RtMidiIn*)*nPorts);
        nameMout = (char**)malloc(sizeof(char*)*(nPorts+1));

        for(int n=0; n<nPorts; n++) {
            try {
                midiin[n] = new RtMidiIn(RtMidi::MACOSX_CORE);
                snprintf(buf, 256, "%s %d", name, n+1);
                midiin[n]->openVirtualPort(buf);
                midiin[n]->ignoreTypes(false, false, false);
            } catch ( RtMidiError &error ) {
                error.printMessage();
                exit( EXIT_FAILURE );
            }

            nameMout[n] = (char*)malloc(256);
            snprintf(nameMout[n], 256, "event_out_%d", n+1);
        }
        nameMout[nPorts] = NULL;

        // Register Audio/MIDI ports depending on the number of system MIDI ports
        register_ports(NULL, nameAout, NULL, (const char**)nameMout);
        connect_ports();
    }

    ~JackRouterOut() {
        for(int n=0; n<nPorts; n++) {
            delete midiin[n];
            free(nameMout[n]);
        }
        free(midiin);
        free(nameMout);
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

        for(int n=0; n<nPorts; n++) {
            mout = jack_port_get_buffer(midiOut[n], nframes);
            jack_midi_clear_buffer(mout);
            midiin[n]->getMessage(&message);
            while(message.size() > 0) {
                buf = jack_midi_event_reserve(mout, 0, message.size());
                if (buf != NULL) {
                    for(int i=0; i<message.size(); i++) {
                        buf[i] = message[i];
                    }
                } else {
                    fprintf(stderr, "ERROR: jack_midi_event_reserve failed()\n");
                }
                midiin[n]->getMessage(&message);
            }
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
