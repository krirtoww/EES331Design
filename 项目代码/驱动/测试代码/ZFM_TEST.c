#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdbool.h>
#include <time.h>

#define SW_DEVICE   "/dev/dip_sw"
#define ZFM_DEVICE  "/dev/zfm_uart"

#define MAX_NAME_LENGTH 50
#define TEMPLATE_FILE "fingerID_Name_mapping.txt"

// 指令码
enum {
    ZFM_GEN_IMG      = 0x01,
    ZFM_IMG2TZ       = 0x02,
    ZFM_MATCH        = 0x03,
    ZFM_SEARCH       = 0x04,
    ZFM_REG_MODEL    = 0x05,
    ZFM_STORE        = 0x06,
    ZFM_LOAD_CHAR    = 0x07,
    ZFM_TEMPLATE_NUM = 0x1D,
    ZFM_EMPTY        = 0x0D
};

static int zfm_fd = -1;
static int sw_fd = -1;
static uint8_t last_sw_state = 0xFF;  // 上次的SW状态，用于判断状态变化

// 保存ID和名称的映射
static int save_name_to_file(int id, const char* name) {
    FILE* file = fopen(TEMPLATE_FILE, "a");
    if (file == NULL) {
        printf("无法打开文件保存映射\n");
        return -1;
    }

    // 保存ID和名称到文件
    fprintf(file, "ID=%d, Name=%s\n", id, name);
    fclose(file);

    return 0;
}

static int get_name_from_file(int id, char* name) {
    FILE* file = fopen(TEMPLATE_FILE, "r");
    if (file == NULL) {
        printf("无法打开文件读取映射\n");
        return -1;
    }

    char line[100];
    while (fgets(line, sizeof(line), file)) {
        int file_id;
        char file_name[MAX_NAME_LENGTH];

        // 解析每一行内容，提取ID和名称
        if (sscanf(line, "ID=%d, Name=%49[^\n]", &file_id, file_name) == 2) {
            if (file_id == id) {
                strcpy(name, file_name);  // 找到匹配的ID，返回名称
                fclose(file);
                return 0;
            }
        }
    }

    printf("没有找到对应的ID=%d\n", id);
    fclose(file);
    return -1;  // 未找到匹配ID
}

// 串口配置为原始模式
static void configure_uart(int fd) {
    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
}

// 发送命令包
static int send_cmd(uint8_t code, uint8_t *params, int plen) {
    uint8_t buf[32];
    int idx = 0, sum = 0;
    uint16_t pkg_len = 1 + plen + 2;
    buf[idx++] = 0xEF; buf[idx++] = 0x01;
    buf[idx++] = 0xFF; buf[idx++] = 0xFF;
    buf[idx++] = 0xFF; buf[idx++] = 0xFF;
    buf[idx++] = 0x01;  // 命令包
    buf[idx++] = (pkg_len >> 8) & 0xFF;
    buf[idx++] = pkg_len & 0xFF;
    buf[idx++] = code;
    sum += 0x01 + ((pkg_len >> 8) & 0xFF) + (pkg_len & 0xFF) + code;
    for (int i = 0; i < plen; i++) {
        buf[idx++] = params[i];
        sum += params[i];
    }
    buf[idx++] = (sum >> 8) & 0xFF;
    buf[idx++] = sum & 0xFF;
    tcflush(zfm_fd, TCIFLUSH);
    return write(zfm_fd, buf, idx) == idx ? 0 : -1;
}

// 带超时读取指定字节
static int read_bytes(uint8_t *out, int len, int tmo_ms) {
    int rec = 0, r;
    fd_set rfds;
    struct timeval tv;
    while (rec < len) {
        FD_ZERO(&rfds);
        FD_SET(zfm_fd, &rfds);
        tv.tv_sec  = tmo_ms / 1000;
        tv.tv_usec = (tmo_ms % 1000) * 1000;
        if (select(zfm_fd+1, &rfds, NULL, NULL, &tv) <= 0) return -1;
        r = read(zfm_fd, out + rec, len - rec);
        if (r <= 0) return -1;
        rec += r;
    }
    return rec;
}

// 一次性读取应答包并返回 status + payload
static int read_ack(uint8_t *status, uint8_t *payload_out, int *payload_len) {
    uint8_t ch, hdr[7], payload[256];
    int state = 0;
    // 同步 EF01
    while (state < 2) {
        if (read_bytes(&ch, 1, 500) != 1) return -1;
        if (state == 0 && ch == 0xEF) state = 1;
        else if (state == 1 && ch == 0x01) state = 2;
        else state = 0;
    }
    // 读剩余头部
    if (read_bytes(hdr, 7, 500) != 7) return -1;
    uint16_t len = (hdr[5] << 8) | hdr[6];
    if (len < 3 || len > 256) return -1;
    if (read_bytes(payload, len, 500) != len) return -1;
    if (status) *status = payload[0];
    if (payload_out && payload_len) {
        int actual = len - 3;  // 去掉 status + checksum(2B)
        memcpy(payload_out, &payload[1], actual);
        *payload_len = actual;
    }
    return 0;
}


// 获取指纹库模板数量
static uint16_t get_template_count() {
    uint8_t status, payload[16];
    int payload_len;
    send_cmd(ZFM_TEMPLATE_NUM, NULL, 0);
    if (read_ack(&status, payload, &payload_len) != 0 || status != 0x00 || payload_len < 2)
        return 0;
    return (payload[0] << 8) | payload[1];
}

// 注册指纹
static void enroll_fingerprint() {
    uint8_t status, params[3], buf1 = 1, buf2 = 2;
    int count;
    printf("=== 指纹注册 ===\n");
    // 第一次采集
    printf("请放置手指...\n");
    do {
        send_cmd(ZFM_GEN_IMG, NULL, 0);
    } while (read_ack(&status, NULL, NULL) == 0 && status == 0x02);
    if (status != 0x00)  return; 
    send_cmd(ZFM_IMG2TZ, &buf1, 1);
    if (read_ack(&status, NULL, NULL) != 0 || status != 0x00)return;
    // 第二次采集
    printf("请移开并再次放置手指...\n"); sleep(1);
    do {
        send_cmd(ZFM_GEN_IMG, NULL, 0);
    } while (read_ack(&status, NULL, NULL) == 0 && status == 0x02);
    if (status != 0x00) { printf("采集失败 %02X\n", status); return; }
    send_cmd(ZFM_IMG2TZ, &buf2, 1);
    if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) { printf("特征2失败 %02X\n", status); return; }
    send_cmd(ZFM_REG_MODEL, NULL, 0);
    if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) { printf("合并失败 %02X\n", status); return; }
    // 去重
    count = get_template_count();
    printf("当前模板数: %d\n", count);
    for (int i = 0; i < count; i++) {
        params[0]=2; params[1]=(i>>8)&0xFF; params[2]=i&0xFF;
        send_cmd(ZFM_LOAD_CHAR, params, 3);
        if (read_ack(&status, NULL, NULL)==0 && status==0x00) {
            uint8_t mp[2]={1,2};
            send_cmd(ZFM_MATCH, mp,2);
            if (read_ack(&status,NULL,NULL)==0 && status==0x00) {
                printf("已存在模板ID %d\n", i);
                return;
            }
        }
    }
    // 存储新模板
    params[0]=1; params[1]=(count>>8)&0xFF; params[2]=count&0xFF;
    send_cmd(ZFM_STORE, params,3);
    if (read_ack(&status,NULL,NULL)==0 && status==0x00)
        printf("注册成功，ID=%d\n", count);
    else
        printf("存储失败 %02X\n", status);

    	char user_name[MAX_NAME_LENGTH];
	 // 提示用户输入名称
        printf("请输入用户名称：");
        fgets(user_name, MAX_NAME_LENGTH, stdin);
        user_name[strcspn(user_name, "\n")] = 0;  // 移除换行符

        // 保存ID和名称映射
        if (save_name_to_file(count, user_name) == 0) {
            printf("用户名称已保存。\n");
        }
     else {
        printf("存储失败 %02X\n", status);
    }
}
//识别指纹
static void search_fingerprint() {
    uint8_t status, buf1 = 1;
    int count;
    time_t start_time = 0;
    int is_no_finger = 0;  // 用于判断是否没有手指
    printf("=== 指纹搜索 ===\n");
    count = get_template_count();
    // 执行指纹匹配
    while (1) {
        send_cmd(ZFM_GEN_IMG, NULL, 0);
        if (read_ack(&status, NULL, NULL) != 0) {
            printf("读取确认码失败\n");
            continue;
        }

        // 如果没有手指（确认码0x02）
        if (status == 0x02) {
            if (!is_no_finger) {
                start_time = time(NULL);  // 记录开始时间
                is_no_finger = 1;  // 标记没有手指
            }
            printf("time:%d",time(NULL) - start_time);
            // 如果持续超过10秒，则退出搜索
            if (is_no_finger && (time(NULL) - start_time) > 10) {
                printf("超过10秒未检测到手指，退出搜索。\n");
                return;  // 超时退出
            }
        }
        else if (status == 0x00) {
		is_no_finger = 0;
		send_cmd(ZFM_IMG2TZ, &buf1, 1);
	        if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) {
	            continue;  // 继续重新采集
	        }

            for (int i = 0; i < count; i++) {
                uint8_t p[3] = { 2, (i >> 8) & 0xFF, i & 0xFF };
                send_cmd(ZFM_LOAD_CHAR, p, 3);
                if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) {
                    continue;  // 跳过不成功的模板
                }

                uint8_t mp[2] = { 1, 2 };
                send_cmd(ZFM_MATCH, mp, 2);
                if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
                    printf("匹配ID=%d\n", i);
                    char name[MAX_NAME_LENGTH];

                    if (get_name_from_file(i, name) == 0) {
                        printf("用户名称是：%s\n", name);
                    } else {
                        printf("未找到ID=%d的用户名称。\n", i);
                    }
                    return;  // 匹配成功，退出
                }
            }
        }

        printf("未匹配，继续搜索...\n");
    }
}
// 清空数据库
static void clear_database() {
    int count = get_template_count();
    printf("=== 清空数据库 ===\n当前模板数: %d\n", count);
    if (!count) { printf("库已空\n"); return; }
    uint8_t status;
    send_cmd(ZFM_EMPTY, NULL,0);
    if (read_ack(&status,NULL,NULL)==0 && status==0x00)
        printf("清空成功\n");
    else
        printf("清空失败 %02X\n", status);
}

// 获取拨码开关状态变化
static uint8_t get_sw_state() {
    uint8_t sw;
    lseek(sw_fd, 0, SEEK_SET);
    if (read(sw_fd, &sw, 1) != 1) return 0xFF;
    return sw;
}

int main() {
    sw_fd    = open(SW_DEVICE, O_RDONLY);
    zfm_fd   = open(ZFM_DEVICE, O_RDWR | O_NOCTTY);
    if (sw_fd < 0 || zfm_fd < 0) {
        perror("打开设备失败");
        return 1;
    }
    configure_uart(zfm_fd);

    printf("拨码 SW1=1 搜索, SW2=2 注册, SW3=4 清空\n");
    while (1) {
        uint8_t sw_state = get_sw_state();
        if (sw_state != last_sw_state) {
            last_sw_state = sw_state;
            if (sw_state & 0x01) {  // SW1 被拉高
                search_fingerprint();  // 立即搜索
            }
            if (sw_state & 0x02) {  // SW2 状态变化
                enroll_fingerprint(); // 注册
            }
            if (sw_state & 0x04) {  // SW3 状态变化
                clear_database();     // 清空
            }
        }
        usleep(100000);  // 防止CPU过度占用
    }

    close(zfm_fd);
    close(sw_fd);
    return 0;
}

