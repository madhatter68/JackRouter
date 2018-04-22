#include <sys/mman.h>

int
main(int argc, char** argv) {
    shm_unlink("/JackBridge");
    shm_unlink("/jackrouter");
    shm_unlink("/jackrouter2");
}
