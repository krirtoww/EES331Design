#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define DEVICE_PATH "/dev/dip_sw"

int main() {
    int fd;
    uint8_t switch_state;
    
    // 打开设备
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }
    
    // 读取开关状态
    
    while(1)
    {
    if (read(fd, &switch_state, sizeof(switch_state)) < 0) {
        perror("Failed to read switch state");
        close(fd);
        return -1;
    }
    
    // 打印每个开关状态
    for (int i = 0; i < 8; i++) {
        printf("SW%d: %s\n", i+1, (switch_state & (1 << i)) ? "ON" : "OFF");
    }
    sleep(1);
    }
    close(fd);
    return 0;
}
