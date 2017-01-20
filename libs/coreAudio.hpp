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

#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "audio.hpp"

#ifndef __COREAUDIO_HPP__
#define __COREAUDIO_HPP__

/******************************************************************************
 Audio functions (Generic/CoreAudio)
******************************************************************************/
//
// Shared memory map:
// 0x0000          : Control Registers (Read/Write Pointers)
// 0x1000          : Upstream buffer (Driver -> Application)
// 0x1000+STRBUFSZ : Downstream buffer (Application -> Driver)
//
#define AUDIO_SAMPLE_SIZE (sizeof(sample_t))

#define STRBUFNUM           (MAX_FRAME_SIZE*4*2) // stereo * 4buffers
#define STRBUFSZ            (STRBUFNUM*AUDIO_SAMPLE_SIZE) // 4buffers * stereo
#define JACK_SHMSIZE        (0x1000+STRBUFSZ*2) // control + down/upstream buffers

class coreAudioStream {
private:
    sample_t *buf;
    int shm_fd;
    unsigned short *uwp, *urp, *dwp, *drp;
    sample_t *buf_up;
    sample_t *buf_down;

    int attach_shm() {
        struct stat stat;

        shm_fd = shm_open("/jackrouter", O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
        if (shm_fd < 0) {
            fprintf(stderr, "shm_open() failed with %s\n", strerror(errno));
            return -1;
        }

        if (fstat(shm_fd, &stat) < 0) {
            fprintf(stderr, "fstat() failed with %s\n", strerror(errno));
            return -1;
        } else {
            if (stat.st_size == 0) { 
                fprintf(stderr, "shm has not be created yet\n");
                return -1;
            }
        }
            
        char* shm_base = (char*)mmap(NULL, JACK_SHMSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shm_base == MAP_FAILED) {
            fprintf(stderr, "mmap() failed with %s\n", strerror(errno));
            return -1;
        }

        uwp = (unsigned short*)shm_base;
        urp = ((unsigned short*)shm_base)+1;
        dwp = ((unsigned short*)shm_base)+2;
        drp = ((unsigned short*)shm_base)+3;
        buf_up = (sample_t*)(shm_base + 0x1000);
        buf_down = buf_up + STRBUFNUM;
        return 0;
    }

    int dataAvailableOnUpstream(int nframes) {
        unsigned int diff;
        static int ncalls=0;

        ncalls++;        
        diff = (*uwp-*urp) & (STRBUFNUM-1);
        if (diff < nframes*2) {
            return 0;
        }
        if (diff > MAX_DELAY*2) {
            fprintf(stderr, "%d: WP: %d RP: %d\n", ncalls, *uwp, *urp);
            *urp = ((*uwp - nframes*2) & ~(nframes*2 - 1)) & (STRBUFNUM-1);
        }
        return 1;
    }

    int dataAvailableOnDownstream(int nframes) {
        unsigned int diff;
        
        diff = (*dwp-*drp) & (STRBUFNUM-1);
        if (diff < nframes*2) {
            return 0;
        }
        if (diff > MAX_DELAY*2) {
            *drp = ((*dwp - nframes*2) & ~(nframes*2 - 1)) & (STRBUFNUM-1);
        }
        return 1;
    }

public:
    coreAudioStream(audioFormat inFormat) {
        if(attach_shm() < 0) {
            fprintf(stderr, "attach_shm() failed\n");
            return; 
        }
    }

    ~coreAudioStream() {
        free(buf);
    }

    void reset() {
        *uwp = *urp = *dwp = *drp = 0;
    }

    // To be fixed (Input is 32bit float stereo sigle stream)
    int sendToUpstream(sample_t** in, int nframes) {
        for(int i=0; i<nframes; i++) {
            *(buf_up+(*uwp)) = in[0][i];
            *(buf_up+(*uwp)+1) = in[1][i];
            *uwp = (*uwp+2)&(STRBUFNUM-1);
        }
        return nframes;
    }

    int receiveFromUpstream(sample_t** out, int nframes) {
        if (dataAvailableOnUpstream(nframes)) {
            for (int i=0; i<nframes; i++) {
                out[0][i] = *(buf_up+(*urp));
                out[1][i] = *(buf_up+(*urp)+1);
                *urp = (*urp+2)&(STRBUFNUM-1);
            }
            return nframes;
        } 
        return 0;
    }

    int sendToDownstream(sample_t** in, int nframes) {
        for(int i=0; i<nframes; i++) {
            *(buf_down+(*dwp)) = in[0][i];
            *(buf_down+(*dwp)+1) = in[1][i];
            *dwp = (*dwp+2)&(STRBUFNUM-1);
        }
        return nframes;
    }

    // To be fixed (Output is 32bit float stereo sigle stream)
    int receiveFromDownstream(sample_t** out, int nframes) {
        if (dataAvailableOnDownstream(nframes)) {
            for (int i=0; i<nframes; i++) {
                out[0][i] = *(buf_down+(*drp));
                out[1][i] = *(buf_down+(*drp)+1);
                *drp = (*drp+2)&(BUFNUM-1);
            }
            return nframes;
        }
        return 0;
    }
};
#endif
