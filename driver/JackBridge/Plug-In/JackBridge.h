/*
 File: JackBridge.h

 MIT License
 
 Copyright (c) 2018 Shunji Uno <madhatter68@linux-dtm.ivory.ne.jp>
 
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
#pragma once
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <sstream>

#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>
#include <mach/mach_time.h>

/******************************************************************************
 Audio functions (Generic/CoreAudio)
******************************************************************************/
// Shared memory map: (mapped every 1MB boundary for each instance)
// 0x0000      : Control Registers (Read/Write Pointers)
// 0x0000      :    upstream write pointer
// 0x0002      :    upstream read  pointer
// 0x0004      :    downstream write pointer
// 0x0006      :    downstream read pointer
// 0x0080      :    RingBufferSize(Default: 4K*2ch)
// 0x0100      :    TimeStamp number
// 0x0108      :    HostTime at recent TimeZero
// 0x0110      :    Seed
// 0x0118      :    SyncMode
// 0x0120      :    RingBufferSize
// 0x0128      :    Driver status
// 0x0180      :    Current Frame Number(coreAudio read)
// 0x0188      :    Current Frame Number(coreAudio write)
// 0x0190      :    Current Frame Number(coreAudio read)
// 0x0198      :    Current Frame Number(coreAudio write)
// 0x10000     : Upstream buffer #0 (Driver -> Application)
// 0x18000     : Downstream buffer #0 (Application -> Driver)
// 0x20000     : Upstream buffer #0 (Driver -> Application)
// 0x28000     : Downstream buffer #0 (Application -> Driver)

typedef float sample_t;
#define AUDIO_SAMPLE_SIZE (sizeof(sample_t))

#define STRBUFSZ            (0x8000) // 32KB Ring buffer
#define STRBUFNUM           (STRBUFSZ/AUDIO_SAMPLE_SIZE) // 1024 entries
#define REGSMAP_SIZE        (0x30000)
#define REGSMAP_BOUNDARY    (0x30000)
#define JACK_SHMSIZE        (0x60000)
#define STRBUF_U0           (0x10000)
#define STRBUF_UP(i)        (0x10000*(i)+0x10000)
#define STRBUF_DOWN(i)      (0x10000*(i)+0x18000)

#define JACK_SHMPATH        "/JackBridge"

#ifdef _ERROR_SYSLOG_
#define ERROR(pri, str, code) syslog(pri, str, code);
#else
#define ERROR(pri, str, code) fprintf(stderr, str, code);
#endif

class JackBridgeDriverIF {
protected:
    uint32_t instance;
    int shm_fd;
    sample_t *buf_up[2];
    sample_t *buf_down[2];
    uint64_t   FrameNumber;
    int        FramesPerBuffer;
    volatile uint64_t     *shmNumberTimeStamps;
    volatile uint64_t     *shmZeroHostTime;
    volatile uint64_t     *shmSeed;
    volatile uint64_t     *shmSyncMode;
    volatile uint64_t     *shmBufferSize;
    volatile uint64_t     *shmDriverStatus;
#define JB_DRV_STATUS_INIT      0
#define JB_DRV_STATUS_ACTIVE    1
#define JB_DRV_STATUS_STARTED   2
    volatile uint64_t     *shmReadFrameNumber[2];
    volatile uint64_t     *shmWriteFrameNumber[2];

    int create_shm() {
        struct stat stat;
        ERROR(LOG_INFO, "JackBridge: Initializing shared memory to communicate with jack(%d).", 0);
        shm_fd = shm_open(JACK_SHMPATH, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
        if (shm_fd < 0) {
            ERROR(LOG_ERR, "shm cannot be opened with %s.\n", strerror(errno));
            return -1;
        }
        
        if (fstat(shm_fd, &stat) < 0) {
            ERROR(LOG_ERR, "Couldn't get shm stat with %s.\n", strerror(errno));
            close(shm_fd);
            return -1;
        }
        
        if (stat.st_size != JACK_SHMSIZE) {
            if (ftruncate(shm_fd, JACK_SHMSIZE) == -1) {
                ERROR(LOG_INFO, "shm cannot be truncated with %s. Try to recreate shm.\n", strerror(errno));
                close(shm_fd);
                shm_unlink(JACK_SHMPATH);
                shm_fd = shm_open(JACK_SHMPATH, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
                if (shm_fd < 0) {
                    ERROR(LOG_ERR, "shm cannot be recreated with %s.\n", strerror(errno));
                    return -1;
                }
            }
            ERROR(LOG_INFO, "Recreated shm because shm size is not matched as expected. (%d)\n", 0);
        }
        close(shm_fd);
        return 0;
    }
    
    int attach_shm() {
        struct stat stat;
        
        shm_fd = shm_open(JACK_SHMPATH, O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
        if (shm_fd < 0) {
            ERROR(LOG_ERR, "shm_open() failed with %s.\n", strerror(errno));
            return -1;
        }
        
        if (fstat(shm_fd, &stat) < 0) {
            ERROR(LOG_ERR, "fstat() failed with %s.\n", strerror(errno));
            return -1;
        } else {
            if (stat.st_size != JACK_SHMSIZE) {
                ERROR(LOG_ERR, "does not match shmsize(%lld). May be driver version mismatch\n", stat.st_size);
            }
        }
        
        char* shm_base = (char*)mmap(NULL, REGSMAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, instance*REGSMAP_BOUNDARY);
        //char* shm_base = (char*)mmap(NULL, JACK_SHMSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shm_base == MAP_FAILED) {
            ERROR(LOG_ERR, "mmap() failed with %s\n", strerror(errno));
            return -1;
        }
        
        buf_up[0]   = (sample_t*)(shm_base + STRBUF_UP(0));
        buf_down[0] = (sample_t*)(shm_base + STRBUF_DOWN(0));
        buf_up[1]   = (sample_t*)(shm_base + STRBUF_UP(1));
        buf_down[1] = (sample_t*)(shm_base + STRBUF_DOWN(1));
        
        shmNumberTimeStamps = (uint64_t*)(shm_base+0x100);
        shmZeroHostTime = (uint64_t*)(shm_base+0x108);
        shmSeed = (uint64_t*)(shm_base+0x110);
        shmSyncMode = (uint64_t*)(shm_base+0x118);
        shmBufferSize = (uint64_t*)(shm_base+0x120);
        shmDriverStatus = (uint64_t*)(shm_base+0x128);
        shmReadFrameNumber[0] = (uint64_t*)(shm_base+0x180);
        shmWriteFrameNumber[0] = (uint64_t*)(shm_base+0x188);
        shmReadFrameNumber[1] = (uint64_t*)(shm_base+0x190);
        shmWriteFrameNumber[1] = (uint64_t*)(shm_base+0x198);
        return 0;
    }
    
public:
    JackBridgeDriverIF(uint32_t _instance) : instance(_instance) {
    }

    ~JackBridgeDriverIF() {
    }
};
