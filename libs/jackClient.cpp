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
#include "jackClient.hpp"

/**********************************************************************
 private functions
**********************************************************************/
void JackClient::finalize_audio_ports() {
    jack_client_close(client);
}

void JackClient::setAudioFormat() {
    format.SampleRate = jack_get_sample_rate(client);
    format.bitsPerSample = 32;
    format.SamplesPerFrame = 256;
}

int JackClient::process_callback(jack_nframes_t nframes) {
    sample_t *ain, *aout;    // for Audio
    void *min, *mout;    // for Audio
    int i;

    // exchage audio data
    if (nAudioIn > 0) {
        for(i=0; i<nAudioIn; i++) {
            if (audioInStream[i]) {
                ain = (sample_t*)jack_port_get_buffer(audioIn[i], nframes);
                audioInStream[i]->sendToStream(ain, nframes);
            }
        }
    }

    if (nAudioOut > 0) {
        for(i=0; i<nAudioOut; i++) {
            if (audioOutStream[i]) {
                aout = (sample_t*)jack_port_get_buffer(audioOut[i], nframes);
                audioOutStream[i]->receiveFromStream(aout, nframes);
            }
        }
    }

    // exchage MIDI data
    jack_midi_data_t* buf;
    jack_midi_event_t event;

    if (nMidiIn > 0) {
        for(i=0; i<nMidiIn; i++) {
            if (midiInStream[i]) {
                // MIDI-In has not been supported yet.
                min = jack_port_get_buffer(midiIn[0], nframes);
                int count = jack_midi_get_event_count(min);
                for(int j=0; j<count; j++) {
                    jack_midi_event_get(&event, min, j);
                    midiData_t *data = midiInStream[i]->getNextBuffer();
                    data->size = event.size;
                    data->time = 0;
                    for (unsigned int k=0; k<event.size; k++) {
                        data->data[k] = event.buffer[k];
                    }
                    midiInStream[i]->sendToStream(data);
                }
            }
        }
    }

    if (nMidiOut > 0) {
        for(i=0; i<nMidiOut; i++) {
            if (midiOutStream[i]) {
                mout = jack_port_get_buffer(midiOut[i], nframes);
                jack_midi_clear_buffer(mout);
                while (midiOutStream[i]->dataAvailable()) {
                    midiData_t *data = midiOutStream[i]->receiveFromStream();
                    if ((data->size > 0)&&(data->size < 4)) {
                        buf = jack_midi_event_reserve(mout, data->time, data->size);
                        for(int j=0; j<data->size; j++) {
                            buf[j] = data->data[j];
                        }
                    }    
                    midiOutStream[i]->receiveNext();
                }
            }
        }
    }
    return 0;
}

static int
_process_callback(jack_nframes_t nframes, void *arg) {
    JackClient* obj= (JackClient*)arg;
    return obj->process_callback(nframes);
}

/**********************************************************************
 public functions
**********************************************************************/
JackClient::JackClient(const char* name, const char* nameAin[], const char* nameAout[],
    const char* nameMin[], const char* nameMout[]) {
    jack_status_t jst;
    int i;

    client = jack_client_open(name, JackNullOption, &jst);
    if (!client) {
        //fprintf(stderr, "jack_client_open failed with %x\n", jst);
        return;
    }
    setAudioFormat();

    //create Audio input ports
    nAudioIn=0;
    if (nameAin) {
        for(i=0; (nameAin[i] != NULL)&&(i<MAX_PORT_NUM); i++) {
            audioIn[i] = jack_port_register(client, nameAin[i],
                JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        }
        nAudioIn=i;
    }

    //create Audio output ports
    nAudioOut=0;
    if (nameAout) {
        for(i=0; (nameAout[i] != NULL)&&(i<MAX_PORT_NUM); i++) {
            audioOut[i] = jack_port_register(client, nameAout[i],
                JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        }
        nAudioOut=i;
    }

    //create MIDI input ports
    nMidiIn=0;
    if (nameMin) {
        for(i=0; (nameMin[i] != NULL)&&(i<MAX_PORT_NUM); i++) {
            midiIn[i] = jack_port_register(client, nameMin[i],
                JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        }
        nMidiIn=i;
    }

    //create MIDI output ports
    nMidiOut=0;
    if (nameMout) {
        for(i=0; (nameMout[i] != NULL)&&(i<MAX_PORT_NUM); i++) {
            midiOut[i] = jack_port_register(client, nameMout[i],
                JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
        }
        nMidiOut=i;
    }
}

JackClient::~JackClient() {
    finalize_audio_ports();
}

int JackClient::bindPorts(audioStream **ain, audioStream **aout, midiStream **min, midiStream **mout) {
    int i;

    if (ain) {
        for(i=0; i<nAudioIn; i++) {
            audioInStream[i] = ain[i];
        }
    }

    if (aout) {
        for(i=0; i<nAudioOut; i++) {
            audioOutStream[i] = aout[i];
        }
    }

    if (min) {
        for(i=0; i<nMidiIn; i++) {
            midiInStream[i] = min[i];
        }
    }

    if (mout) {
        for(i=0; i<nMidiOut; i++) {
            midiOutStream[i] = mout[i];
        }
    }

    return 0;
}

void JackClient::activate() {
    jack_set_process_callback(client, _process_callback, this);
    //jack_set_freewheel_callback(client, _freewheel_callback, arg);
    //jack_on_shutdown(client, _on_shutdown, arg);

    jack_activate(client);
}
