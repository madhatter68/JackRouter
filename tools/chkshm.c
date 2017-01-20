#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
	int fd;
	struct stat stat;

	fd = shm_open("/jackrouter", O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "shm_open failed with %s\n", strerror(errno));
		return -1;
	}

	if (fstat(fd, &stat) < 0) {
		printf("/jackrouter is not exist\n");
	} else {
		printf("mode:0%o, uid:%d, gid:%d, size:%lld\n",
			stat.st_mode, stat.st_uid, stat.st_gid, stat.st_size);
	}

	close(fd);
}
