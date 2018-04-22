/*
 File: JackBridge.h

MIT License

Copyright (c) 2016-2018 Shunji Uno <madhatter68@linux-dtm.ivory.ne.jp>

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
#include "jackClient.hpp"
#include "JackBridge.h"
#ifdef _WITH_MIDI_BRIDGE_
#include <rtmidi/RtMidi.h>
#define MAX_MIDI_PORTS 256
#endif // _WITH_MIDI_BRIDGE_

/*
 * JackBridge.cpp
 */

static const char* nameAin[]= {"input_0", "input_1", "input_2", "input_3", NULL};
static const char* nameAout[] = {"output_0", "output_1", "output_2", "output_3", NULL};

class JackBridge : public JackClient, public JackBridgeDriverIF {
public:
    JackBridge(const char* name, int id) : JackClient(name, JACK_PROCESS_CALLBACK), JackBridgeDriverIF(id) {
        if (attach_shm() < 0) {
            fprintf(stderr, "Attaching shared memory failed (id=%d)\n", id);
            exit(1);
        }

        isActive = false;
        isSyncMode = true; // FIXME: should be parameterized
        isVerbose = (getenv("JACKBRIDGE_DEBUG")) ? true : false;
        FrameNumber = 0;
        FramesPerBuffer = STRBUFNUM/2;
        *shmBufferSize = STRBUFSZ;
        *shmSyncMode = 0;

#ifdef _WITH_MIDI_BRIDGE_
        create_midi_ports(name);
        register_ports(nameAin, nameAout, (const char**)nameMin, (const char**)nameMout);
#else
        register_ports(nameAin, nameAout, NULL, NULL);
#endif // _WITH_MIDI_BRIDGE_

        // For DEBUG
        lastHostTime = 0;
        struct mach_timebase_info theTimeBaseInfo;
        mach_timebase_info(&theTimeBaseInfo);
        double theHostClockFrequency = theTimeBaseInfo.denom / theTimeBaseInfo.numer;
        theHostClockFrequency *= 1000000000.0;
        HostTicksPerFrame = theHostClockFrequency / SampleRate;
        if (isVerbose) {
            printf("JackBridge#%d: Start with samplerate:%d Hz, buffersize:%d bytes\n",
                instance, SampleRate, BufSize);
        }
    }

    ~JackBridge() {
#ifdef _WITH_MIDI_BRIDGE_
        release_midi_ports();
#endif // _WITH_MIDI_BRIDGE_
    }

    int process_callback(jack_nframes_t nframes) override {
        sample_t *ain[4];
        sample_t *aout[4];

#ifdef _WITH_MIDI_BRIDGE_
        process_midi_message(nframes);
#endif // _WITH_MIDI_BRIDGE_

        if (*shmDriverStatus != JB_DRV_STATUS_STARTED) {
            // Driver isn't working. Just return zero buffer;
            for(int i=0; i<4; i++) {
                aout[i] = (sample_t*)jack_port_get_buffer(audioOut[i], nframes);
                bzero(aout[i], STRBUFSZ);
            }
            return 0;
        }

        // For DEBUG
        check_progress();

        if (!isActive) {
            ncalls = 0;
            FrameNumber = 0;

            if (isSyncMode) {
                *shmSyncMode = 1;
                *shmNumberTimeStamps = 0; 
                (*shmSeed)++;
            }

            isActive = true;
            printf("JackBridge#%d: Activated with SyncMode = %s, ZeroHostTime = %llx\n",
                instance, isSyncMode ? "Yes" : "No", *shmZeroHostTime);
        }

        if ((FrameNumber % FramesPerBuffer) == 0) {
            // FIXME: Should be atomic operation and do memory barrier
            if(*shmSyncMode == 1) {
                *shmZeroHostTime = mach_absolute_time();
                *shmNumberTimeStamps = FrameNumber / FramesPerBuffer;
                //(*shmNumberTimeStamps)++;
            } 

            if ((!isSyncMode) && isVerbose && ((ncalls++) % 100) == 0) {
                printf("JackBridge#%d: ZeroHostTime: %llx, %lld, diff:%d\n",
                    instance,  *shmZeroHostTime, *shmNumberTimeStamps,
                    ((int)(mach_absolute_time()+1000000-(*shmZeroHostTime)))-1000000);
            }
        }

        ain[0] = (sample_t*)jack_port_get_buffer(audioIn[0], nframes);
        ain[1] = (sample_t*)jack_port_get_buffer(audioIn[1], nframes);
        ain[2] = (sample_t*)jack_port_get_buffer(audioIn[2], nframes);
        ain[3] = (sample_t*)jack_port_get_buffer(audioIn[3], nframes);
        sendToCoreAudio(ain, nframes);


        aout[0] = (sample_t*)jack_port_get_buffer(audioOut[0], nframes);
        aout[1] = (sample_t*)jack_port_get_buffer(audioOut[1], nframes);
        aout[2] = (sample_t*)jack_port_get_buffer(audioOut[2], nframes);
        aout[3] = (sample_t*)jack_port_get_buffer(audioOut[3], nframes);
        receiveFromCoreAudio(aout, nframes);

        FrameNumber += nframes;

        return 0;
    }

private:
    bool isActive, isSyncMode, isVerbose;
    bool showmsg;
    uint64_t lastHostTime;
    double HostTicksPerFrame;
    int64_t ncalls;

    int sendToCoreAudio(float** in,int nframes) {
        unsigned int offset = FrameNumber % FramesPerBuffer;
        // FIXME: should be consider buffer overwrapping
        for(int i=0; i<nframes; i++) {
            *(buf_down[0]+(offset+i)*2) = in[0][i];
            *(buf_down[0]+(offset+i)*2+1) = in[1][i];
            *(buf_down[1]+(offset+i)*2) = in[2][i];
            *(buf_down[1]+(offset+i)*2+1) = in[3][i];
        }
        return nframes;
    }

    int receiveFromCoreAudio(float** out, int nframes) {
        //unsigned int offset = FrameNumber % FramesPerBuffer;
        unsigned int offset = (FrameNumber - nframes) % FramesPerBuffer;
        // FIXME: should be consider buffer overwrapping
        for(int i=0; i<nframes; i++) {
            out[0][i] = *(buf_up[0]+(offset+i)*2);
            out[1][i] = *(buf_up[0]+(offset+i)*2+1);
            out[2][i] = *(buf_up[1]+(offset+i)*2);
            out[3][i] = *(buf_up[1]+(offset+i)*2+1);
            *(buf_up[0]+(offset+i)*2) = 0.0f;
            *(buf_up[0]+(offset+i)*2+1) = 0.0f;
            *(buf_up[1]+(offset+i)*2) = 0.0f;
            *(buf_up[1]+(offset+i)*2+1) = 0.0f;
        }
        return nframes;
    }

#ifdef _WITH_MIDI_BRIDGE_
    RtMidiOut  **midiout;
    RtMidiIn   **midiin;
    int nOutPorts, nInPorts;
    char** nameMin;
    char** nameMout;

    int get_num_ports(unsigned long flags) {
        int num;
        const char** ports = jack_get_ports(client, "system", ".*raw midi", flags);
        if (!ports) {
            return 0;
        }

        for(num=0;*ports != NULL; ports++,num++) {
#if 0 // For DEBUG
            jack_port_t* p = jack_port_by_name(client, *ports);
            std::cout << ";" << *ports << ";" << jack_port_short_name(p) << ";" << jack_port_type(p) << std::endl;
#endif
        }
        return num;
    }

    void create_midi_ports(const char* name) {
        char buf[256];

        // create bridge from Jack to CoreMIDI
        nOutPorts = get_num_ports(JackPortIsOutput);
        midiout = (RtMidiOut**)malloc(sizeof(RtMidiOut*)*nOutPorts);
        nameMin = (char**)malloc(sizeof(char*)*(nOutPorts+1));

        for(int n=0; n<nOutPorts; n++) {
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
        nameMin[nOutPorts] = NULL;

        // create bridge from CoreMIDI to Jack
        nInPorts = get_num_ports(JackPortIsInput);
        midiin = (RtMidiIn**)malloc(sizeof(RtMidiIn*)*nInPorts);
        nameMout = (char**)malloc(sizeof(char*)*(nInPorts+1));

        for(int n=0; n<nInPorts; n++) {
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
        nameMout[nInPorts] = NULL;
    }

    void release_midi_ports() {
        // release bridge from Jack to CoreMIDI
        for(int n=0; n<nOutPorts; n++) {
            delete midiout[n];
            free(nameMin[n]);
        }
        free(midiout);
        free(nameMin);

        // release bridge from CoreMIDI to Jack
        for(int n=0; n<nInPorts; n++) {
            delete midiin[n];
            free(nameMout[n]);
        }
        free(midiin);
        free(nameMout);
    }

    void process_midi_message(jack_nframes_t nframes) {
        void *min, *mout;
        int count;
        jack_midi_event_t event;
        std::vector< unsigned char > message;
        jack_midi_data_t* buf;

        // process bridge from Jack to CoreMIDI
        for(int n=0; n<nOutPorts; n++) {
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

        // process bridge from CoreMIDI to Jack
        for(int n=0; n<nInPorts; n++) {
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
    }
#endif // _WITH_MIDI_BRIDGE_

    void check_progress() {
#if 0
        if (isVerbose && ((ncalls++) % 500) == 0) {
            printf("JackBridge#%d: FRAME %llu : Write0: %llu Read0: %llu Write1: %llu Read0: %llu\n",
                 instance, FrameNumber,
                 *shmWriteFrameNumber[0], *shmReadFrameNumber[0],
                 *shmWriteFrameNumber[1], *shmReadFrameNumber[1]);
        }
#endif

        int diff = *shmWriteFrameNumber[0] - FrameNumber;
        int interval = (mach_absolute_time() - lastHostTime) / HostTicksPerFrame;
        if (showmsg) {
            if ((diff >= (STRBUFNUM/2))||(interval >= BufSize*2))  {
                if (isVerbose) {
                    printf("WARNING: miss synchronization detected at FRAME %llu (diff=%d, interval=%d)\n",
                        FrameNumber, diff, interval);
                    fflush(stdout);
                }
                showmsg = false;
            }
        } else {
            if (diff < (STRBUFNUM/2)) {
                showmsg = true;
            }
        }
        lastHostTime = mach_absolute_time();
    }
};

int
main(int argc, char** argv)
{
    JackBridge*  jackBridge[4];

    // Create instances of jack client
    jackBridge[0] = new JackBridge("JackBridge #1", 0);
    //jackBridge[1] = new JackBridge("JackBridge #2", 1);

    // activate gateway from/to jack ports
    jackBridge[0]->activate();
    //jackBridge[1]->activate();

    // Infinite loop until daemon is killed.
    while(1) {
        sleep(600);
    }

    return 0;
}
