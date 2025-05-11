#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

int main()
{
    int fd;
    int val;

    fd = open("/dev/ccd_pir", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /dev/ccd_pir");
        return -1;
    }

    while (1) {
        ssize_t len = read(fd, &val, sizeof(val));
        if (len < 0) {
            perror("Read error");
            break;
        }

        if (val)
            printf("PIR: Motion Detected (val=%d)\n", val);
        else
            printf("PIR: No Motion (val=%d)\n", val);

        sleep(1);
    }

    close(fd);
    return 0;
}

