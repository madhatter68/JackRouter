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
#include <jack/jack.h>
#include "jackClient.hpp"
#include "JackBridge.h"

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
        isVerbose = false; // FIXME: should be parameterized
        FrameNumber = 0;
        FramesPerBuffer = STRBUFNUM/2;
        *shmBufferSize = STRBUFSZ;
        *shmSyncMode = 0;

        register_ports(nameAin, nameAout, NULL, NULL);
    }

    ~JackBridge() { }

    int process_callback(jack_nframes_t nframes) override {
        sample_t *ain[4];
        sample_t *aout[4];

        if (*shmDriverStatus != JB_DRV_STATUS_STARTED) {
            // Driver isn't working. Just return zero buffer;
            return 0;
        }

        if (isVerbose && ((ncalls++) % 500) == 0) {
            printf("JackBridge#%d: FRAME %llu : Write0: %llu Read0: %llu Write1: %llu Read0: %llu\n",
                 instance, FrameNumber,
                 *shmWriteFrameNumber[0], *shmReadFrameNumber[0],
                 *shmWriteFrameNumber[1], *shmReadFrameNumber[1]);
        }

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
