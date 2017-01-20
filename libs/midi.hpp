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
#include <queue>
#include <jack/jack.h>
#include <jack/midiport.h>

#ifndef __MIDI_HPP__
#define __MIDI_HPP__

typedef struct midiData {
    unsigned char data[4];
    int size;
    int time;
} midiData_t;

#define MAX_EVENTS 512
#define MIDI_BUFSZ (MAX_EVENTS*sizeof(midiData_t))

class midiStream {
public:
    midiData_t *buf;
    unsigned short wptr;
    unsigned short rptr;

    midiStream() {
        buf = (midiData_t*)malloc(MIDI_BUFSZ);
        wptr = rptr = 0;
    }

    ~midiStream() {
        free(buf);
    }
    
    int dataAvailable() {
        return (wptr != rptr);
    }
    
    midiData_t* getNextBuffer() {
        return (buf+wptr);
    }
    
    void sendToStream(midiData_t* data) {
        wptr = (wptr+1)&(MAX_EVENTS-1);
    }

    midiData_t* receiveFromStream() {
        return (buf+rptr);
    }

    void receiveNext() {
        rptr = (rptr+1)&(MAX_EVENTS-1);
    }
};
#endif
