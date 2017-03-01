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
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <sstream>
#include <jack/jack.h>

#ifndef __AUDIO_HPP__
#define __AUDIO_HPP__
typedef jack_default_audio_sample_t sample_t;

class audioFormat {
public:
    int SampleRate;
    int bitsPerSample;
    int SamplesPerFrame;
};

#define MAX_FRAME_SIZE 4096
#define STR_BUFSZ (MAX_FRAME_SIZE*sizeof(sample_t)*4) // 4buffers
#define MAX_DELAY 1024
#define MAX_CHANNELS 256
#define BUFNUM          (MAX_FRAME_SIZE*4) // 4buffers

class audioStream {
private:
    sample_t *buf;
    unsigned short wptr;
    unsigned short rptr;

public:
    audioStream() {
        buf = (sample_t*)malloc(STR_BUFSZ);
        wptr = rptr = 0;
    }

    ~audioStream() {
        free(buf);
    }

    int dataAvailable(int nframes) {
        int diff;
        
        diff = (wptr-rptr) & (BUFNUM-1);
        if (diff < nframes) {
            return 0;
        }
        if (diff > MAX_DELAY) {
            rptr = ((wptr - nframes) & ~(nframes - 1)) & (BUFNUM-1);
        }
        return 1;
    }

    int sendToStream(sample_t* in, int nframes) {
        for(int i=0; i<nframes; i++) {
            *(buf+wptr) = in[i];
            wptr = (wptr+1)&(BUFNUM-1);
        }
        return nframes;
    }

    int receiveFromStream(sample_t* out, int nframes) {
        if (dataAvailable(nframes)) {
            for (int i=0; i<nframes; i++) {
                out[i] = *(buf+rptr);
                rptr = (rptr+1)&(BUFNUM-1);
            }
            return nframes;
        }
        return 0;
    }
    
    void sendData(sample_t data) {
        *(buf+wptr) = data;
        wptr = (wptr+1)&(BUFNUM-1);
        return;
    }

    sample_t receiveData() {
        sample_t data;

        data = *(buf+rptr);
        rptr = (rptr+1)&(BUFNUM-1);
        return data;
    }
};
#endif
