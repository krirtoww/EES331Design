#include <stdio.h>
#include <unistd.h>  // 用于sleep()函数
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <linux/i2c-dev.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdbool.h>
#include "mqtt.h"
#include <ctype.h>
#include <time.h>


#define DEV_LIGHT "/sys/bus/iio/devices/iio:device0/in_voltage13_vaux11_raw"
#define SW_DEVICE   "/dev/dip_sw"
#define ZFM_DEVICE  "/dev/zfm_uart"
#define I2C_DEV "/dev/i2c-0"





//上传数据准备
unsigned char light_msg[200] = { 0 };
unsigned char temp_msg[200] = { 0 };
unsigned char humidity_msg[200] = { 0 };
unsigned char door_msg[200] = { 0 };
unsigned char warn_msg[200] = { 0 };
unsigned char user_msg[200] = { 0 };

// 环境参数
int light_val = 0;
float temp = 0;
float humidity = 0;

// 门状态相关
int last_distance = 0;
bool door_state = false;
bool door_operation_in_progress = false;
time_t door_opened_time = 0;

// 指纹验证相关
bool fingerprint_active = false;
time_t fingerprint_start_time = 0;
int verify_fail_count = 0;
const int MAX_FAIL_ATTEMPTS = 5;

// 运动检测相关
time_t motion_start_time = 0;
time_t last_motion_time = 0;

// 光照检测相关
int light_history[5] = { 0 };
int light_index = 0;

// 指令码
enum {
    ZFM_GEN_IMG = 0x01,
    ZFM_IMG2TZ = 0x02,
    ZFM_MATCH = 0x03,
    ZFM_SEARCH = 0x04,
    ZFM_REG_MODEL = 0x05,
    ZFM_STORE = 0x06,
    ZFM_LOAD_CHAR = 0x07,
    ZFM_TEMPLATE_NUM = 0x1D,
    ZFM_EMPTY = 0x0D
};

#define MAX_NAME_LENGTH 50
#define MAX_LINE_LENGTH 100
#define TEMPLATE_FILE "fingerID_Name_mapping.txt"

static int zfm_fd = -1;
static int sw_fd = -1;
static uint8_t last_sw_state = 0xFF;  // 上次的SW状态，用于判断状态变化

#define SLAVE_ADDR 0x76

#define P_reg      0xF7
#define H_reg      0xFD
#define T_reg      0xFA
#define BME280_REG_DIG_P1    0x8E
#define BME280_REG_DIG_P2    0x90
#define BME280_REG_DIG_P3    0x92
#define BME280_REG_DIG_P4    0x94
#define BME280_REG_DIG_P5    0x96
#define BME280_REG_DIG_P6    0x98
#define BME280_REG_DIG_P7    0x9A
#define BME280_REG_DIG_P8    0x9C
#define BME280_REG_DIG_P9    0x9E
#define BME280_REG_DIG_T1    0x88
#define BME280_REG_DIG_T2    0x8A
#define BME280_REG_DIG_T3    0x8C
#define BME280_REG_DIG_H1    0xA1
#define BME280_REG_DIG_H2    0xE1
#define BME280_REG_DIG_H3    0xE3
#define BME280_REG_DIG_H4    0xE4
#define BME280_REG_DIG_H5    0xE5
#define BME280_REG_DIG_H6    0xE7
#define BME280_REG_CONTROLHUMID 0xF2
#define BME280_REG_CONTROL   0xF4

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
u16 dig_T1, dig_P1;
u16 dig_H1, dig_H2, dig_H3, dig_H4, dig_H5, dig_H6;
u64 t_fine;

int i2c_fd;







int light() 
{
    int fd_light;
    char buf_light[32] = { 0 };
    ssize_t ret_light;
    fd_light = open(DEV_LIGHT, O_RDWR);
    ret_light = read(fd_light, buf_light, 32);
    if (ret_light < 0) {
        printf("read is error\n");
        return 0;
    }
    //Convert String to value(int)
    char* strtolPEnd;
    long int light_data;
    light_data = strtol(buf_light, &strtolPEnd, 10);
    //Convert to real voltage
    float light_remap = light_data / 2700.0 * 100.0;
    light_data = (int)light_remap;
    //printf("Voltage:%ld\r\n", light_data);
    return light_data;
    close(fd_light);
}

int pir()
{
    int fd_pir;
    int len_p;
    char buffer_p[5];
    int val_p[1] = { 0 };
    fd_pir = open("/dev/ccd_pir", O_RDWR);
    if (fd_pir < 0)
    {
        printf("open device error\n");
        return 0;
    }
    val_p[0] = 0;
    for (int i = 0; i < 5; i++)
        buffer_p[i] = '\0';
    len_p = read(fd_pir, buffer_p, sizeof(buffer_p) - 1);
    buffer_p[len_p] = '\0';
    memcpy(val_p, buffer_p, len_p);
    if (val_p[0])
    {
        //printf("1\n");
        return 1;//检测到有人返回1
        sleep(1);
    }
    else
        return 0;//无人时返回0
    close(fd_pir);
}

int ult()
{
    int fd_ult;
    int len;
    int val[2] = { 0,0 };
    char buffer[9];
    //unsigned long len = 0;
    fd_ult = open("/dev/ccd_ult", O_RDWR);
    if (fd_ult < 0)
    {
        printf("error\n");
        return 0;
    }
    sleep(1);
    len = read(fd_ult, buffer, sizeof(buffer) - 1);
    buffer[len] = '\0';
    memcpy(val, buffer, len);
    if (val[1] == -1)
    {
        //printf("out of range\n");
        return 0;
    }
    else
    {
        //printf("dis:%d state:%d\n",val[0]/58,val[1]);
        return val[0] / 58;
    }
    close(fd_ult);
}

// 获取拨码开关状态变化
static uint8_t get_sw_state() {
    uint8_t sw;
    lseek(sw_fd, 0, SEEK_SET);
    if (read(sw_fd, &sw, 1) != 1) return 0xFF;
    return sw;
}

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
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
}

// 发送命令包
static int send_cmd(uint8_t code, uint8_t* params, int plen) {
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
static int read_bytes(uint8_t* out, int len, int tmo_ms) {
    int rec = 0, r;
    fd_set rfds;
    struct timeval tv;
    while (rec < len) {
        FD_ZERO(&rfds);
        FD_SET(zfm_fd, &rfds);
        tv.tv_sec = tmo_ms / 1000;
        tv.tv_usec = (tmo_ms % 1000) * 1000;
        if (select(zfm_fd + 1, &rfds, NULL, NULL, &tv) <= 0) return -1;
        r = read(zfm_fd, out + rec, len - rec);
        if (r <= 0) return -1;
        rec += r;
    }
    return rec;
}

// 一次性读取应答包并返回 status + payload
static int read_ack(uint8_t* status, uint8_t* payload_out, int* payload_len) {
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
        params[0] = 2; params[1] = (i >> 8) & 0xFF; params[2] = i & 0xFF;
        send_cmd(ZFM_LOAD_CHAR, params, 3);
        if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
            uint8_t mp[2] = { 1,2 };
            send_cmd(ZFM_MATCH, mp, 2);
            if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
                printf("已存在模板ID %d\n", i);
                return;
            }
        }
    }
    // 存储新模板
    params[0] = 1; params[1] = (count >> 8) & 0xFF; params[2] = count & 0xFF;
    send_cmd(ZFM_STORE, params, 3);
    if (read_ack(&status, NULL, NULL) == 0 && status == 0x00)
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
static int search_fingerprint() {
    uint8_t status, buf1 = 1;
    int count;
    time_t start_time = 0;
    int is_no_finger = 0;  // 用于判断是否没有手指
    printf("=== 指纹搜索 ===\n");
    count = get_template_count();
    // 执行指纹匹配
    while (1) {
        uint8_t sw_state = get_sw_state();
        if (sw_state != 0)
            return -2;
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
            printf("time:%d", time(NULL) - start_time);
            // 如果持续超过10秒，则退出搜索
            if (is_no_finger && (time(NULL) - start_time) > 10) {
                printf("超过10秒未检测到手指，退出搜索。\n");
                return -2;  // 超时退出
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
                    }
                    else {
                        printf("未找到ID=%d的用户名称。\n", i);
                    }
                    //上传识别到的用户id
                    sprintf(user_msg, "{\'id\':5,\'dp\':{\'user\':[{\'v':\'id: %d 名称：%s\'}]}}", i, name);
                    mqtt_publish(user_msg);
                    
                    return i;  // 匹配成功，退出
                }
            }
        }

        printf("未匹配，继续搜索...\n");
    }
}

static int remove_user_from_file(const char* keyword, int* out_id) {
    FILE* file = fopen(TEMPLATE_FILE, "r");
    if (!file) {
        printf("无法打开映射文件\n");
        return -1;
    }

    FILE* temp = fopen("temp.txt", "w");
    if (!temp) {
        fclose(file);
        printf("无法创建临时文件\n");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    int found = 0;

    while (fgets(line, sizeof(line), file)) {
        int id;
        char name[MAX_NAME_LENGTH];
        if (sscanf(line, "ID=%d, Name=%49[^\n]", &id, name) == 2) {
            if (strcmp(name, keyword) == 0 || (isdigit(keyword[0]) && atoi(keyword) == id)) {
                found = 1;
                *out_id = id;
                continue;  // 不写入 temp 文件，实现删除
            }
        }
        fputs(line, temp);
    }

    fclose(file);
    fclose(temp);

    if (!found) {
        remove("temp.txt");
        return -1;
    }

    remove(TEMPLATE_FILE);
    rename("temp.txt", TEMPLATE_FILE);
    return 0;
}

// 发送删除指定模板命令
static int delete_fingerprint_template(int page_id) {
    uint8_t payload[4];
    payload[0] = (page_id >> 8) & 0xFF;
    payload[1] = page_id & 0xFF;
    payload[2] = 0x00;  // N=1 高字节
    payload[3] = 0x01;  // N=1 低字节

    uint8_t status;
    send_cmd(0x0C, payload, 4);
    if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
        return 0;
    }
    else {
        printf("删除失败，确认码: %02X\n", status);
        return -1;
    }
}

// 主控制函数：输入关键词，删除匹配用户
void delete_user_by_name_or_id() {

    char input[MAX_NAME_LENGTH];
    printf("请输入要删除的用户名称或ID：");
    fgets(input, MAX_NAME_LENGTH, stdin);
    input[strcspn(input, "\n")] = 0;  // 移除换行符

    int id = -1;
    if (remove_user_from_file(input, &id) == 0) {
        printf("找到用户，ID=%d，开始删除模板...\n", id);
        if (delete_fingerprint_template(id) == 0)
            printf("模板删除成功。\n");
        else
            printf("模板删除失败。\n");
    }
    else {
        printf("未找到对应的用户或ID。\n");
    }
}
// 清空数据库
static void clear_database() {
    int count = get_template_count();
    printf("=== 清空数据库 ===\n当前模板数: %d\n", count);
    if (!count) { printf("库已空\n"); return; }
    uint8_t status;
    send_cmd(ZFM_EMPTY, NULL, 0);
    if (read_ack(&status, NULL, NULL) == 0 && status == 0x00)
        printf("清空成功\n");
    else
        printf("清空失败 %02X\n", status);
}

// 重命名后的 I2C 写函数
int i2c_write_reg(u8 reg, u8 Data) {
    u8 buf[2] = { reg, Data };
    if (write(i2c_fd, buf, 2) != 2) {
        perror("I2C write failed");
        return -1;
    }
    return 0;
}

// I2C 读函数
u8 Read(u8 reg) {
    u8 val;
    if (write(i2c_fd, &reg, 1) != 1) {
        perror("I2C reg select failed");
        return 0;
    }
    if (read(i2c_fd, &val, 1) != 1) {
        perror("I2C read failed");
        return 0;
    }
    return val;
}

u16 BME280Read16LE(u8 reg) {
    u8 msb = Read(reg);
    u8 lsb = Read(reg + 1);
    return (lsb << 8) | msb;
}

int16_t BME280ReadS16LE(u8 reg) {
    return (int16_t)BME280Read16LE(reg);
}

u16 BME280Read8(u8 reg) {
    return Read(reg);
}

// 修正补偿参数读取
void init() {
    i2c_write_reg(BME280_REG_CONTROLHUMID, 0x05);
    i2c_write_reg(BME280_REG_CONTROL, 0xB7);

    dig_T1 = BME280Read16LE(BME280_REG_DIG_T1);
    dig_T2 = BME280ReadS16LE(BME280_REG_DIG_T2);
    dig_T3 = BME280ReadS16LE(BME280_REG_DIG_T3);

    dig_P1 = BME280Read16LE(BME280_REG_DIG_P1);
    dig_P2 = BME280ReadS16LE(BME280_REG_DIG_P2);
    dig_P3 = BME280ReadS16LE(BME280_REG_DIG_P3);
    dig_P4 = BME280ReadS16LE(BME280_REG_DIG_P4);
    dig_P5 = BME280ReadS16LE(BME280_REG_DIG_P5);
    dig_P6 = BME280ReadS16LE(BME280_REG_DIG_P6);
    dig_P7 = BME280ReadS16LE(BME280_REG_DIG_P7);
    dig_P8 = BME280ReadS16LE(BME280_REG_DIG_P8);
    dig_P9 = BME280ReadS16LE(BME280_REG_DIG_P9);

    dig_H1 = BME280Read8(BME280_REG_DIG_H1);
    dig_H2 = BME280ReadS16LE(BME280_REG_DIG_H2);
    dig_H3 = BME280Read8(BME280_REG_DIG_H3);
    // 修正H4和H5的读取方式
    int8_t h4_msb = (int8_t)BME280Read8(BME280_REG_DIG_H4);
    uint8_t h4_lsb = BME280Read8(BME280_REG_DIG_H4 + 1);
    dig_H4 = (h4_msb << 4) | (h4_lsb & 0x0F);

    int8_t h5_msb = (int8_t)BME280Read8(BME280_REG_DIG_H5 + 1);
    dig_H5 = (h5_msb << 4) | (BME280Read8(BME280_REG_DIG_H5) >> 4);

    dig_H6 = (int8_t)BME280Read8(BME280_REG_DIG_H6);
}

// 修正温度计算
float getTemperatureData() {
    int32_t adc_T = Read(T_reg);
    adc_T = (adc_T << 8) | Read(T_reg + 1);
    adc_T = (adc_T << 8) | Read(T_reg + 2);
    adc_T >>= 4;

    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    float T = (t_fine * 5 + 128) >> 8;
    return T / 100.0;
}

// 修正湿度计算
float getHumidityData() {
    int32_t adc_H = Read(H_reg);
    adc_H = (adc_H << 8) | Read(H_reg + 1);

    int32_t v_x1_u32r = (t_fine - ((int32_t)76800));

    v_x1_u32r = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) -
        (((int32_t)dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
        (((((((v_x1_u32r * ((int32_t)dig_H6)) >> 10) *
            (((v_x1_u32r * ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
        ((int32_t)dig_H1)) >> 4));

    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (float)(v_x1_u32r >> 12) / 1024.0;
}

// 修正压力计算
float getPressureData() {
    int64_t var1, var2, p;
    int32_t adc_P = Read(P_reg);
    adc_P = (adc_P << 8) | Read(P_reg + 1);
    adc_P = (adc_P << 8) | Read(P_reg + 2);
    adc_P >>= 4;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

    if (var1 == 0) {
        return 0; // 避免除以零
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);

    return (float)p / 256.0;
}

void barometer() {
    if ((i2c_fd = open(I2C_DEV, O_RDWR)) < 0) {
        perror("Open I2C failed");
        exit(1);
    }
    if (ioctl(i2c_fd, I2C_SLAVE, SLAVE_ADDR) < 0) {
        perror("Set slave failed");
        exit(1);
    }

    init();
    temp = getTemperatureData();
    humidity = getHumidityData();
    return;
}

//设备初始化函数
void open_devices()
{
    sw_fd = open(SW_DEVICE, O_RDONLY);
    zfm_fd = open(ZFM_DEVICE, O_RDWR | O_NOCTTY);
    if (sw_fd < 0 || zfm_fd < 0) {
        perror("打开设备失败");
        return;
    }
    configure_uart(zfm_fd);
    return;
}

// 报警函数
void trigger_alarm(const char* reason) {
    //printf("ALERT: %s\n", reason);
    sprintf(warn_msg, "{\'id\':6,\'dp\':{\'warn\':[{\'v':\'[WARN] %s\'}]}}", reason);
    mqtt_publish(warn_msg);
    
}

//获取环境参量
void environment_monitor() {
    barometer(); // 更新温湿度

    // 温度检查
    if (temp < 14 || temp > 24) {
        trigger_alarm("温度异常");
    }

    // 湿度检查
    if (humidity < 45 || humidity > 60) {
        trigger_alarm("湿度异常");
    }

    // 光照强度检查
    int light_current = light();
    if (light_current > 0) { // 忽略读取失败
        light_val = light_current;
        if (light_val > 100) { // 假设100为最大允许值
            trigger_alarm("光照强度过大");
        }
    }
    printf("temp: %.2f °C, humidity : %.2f %%, light : %d\n", temp, humidity, light_val);
    
    //上传光照强度
    sprintf(light_msg, "{\'id\':1,\'dp\':{\'light\':[{\'v':\'%d\'}]}}", light_val);
    mqtt_publish(light_msg);
    //上传温度数据
    sprintf(temp_msg, "{\'id\':2,\'dp\':{\'temp\':[{\'v':\'%.2f\'}]}}", temp);
    mqtt_publish(temp_msg);
    //上传湿度数据
    sprintf(humidity_msg, "{\'id\':3,\'dp\':{\'humidity\':[{\'v':\'%.2f\'}]}}", humidity);
    mqtt_publish(humidity_msg);
    
}

//光照剧烈波动检测函数
void light_fluctuation_check() {
    light_history[light_index] = light_val;
    light_index = (light_index + 1) % 5;

    int fluctuations = 0;
    for (int i = 1; i < 5; i++) {
        int diff = abs(light_history[i] - light_history[i - 1]);
        if (diff > 50) { // 阈值设为50单位
            fluctuations++;
        }
    }

    if (fluctuations >= 3) {
        trigger_alarm("检测到光照剧烈变化");
    }
}

//柜门状态检测函数
void handle_door_control() {
    int current_dist = ult();

    static bool initial = true;
    if (initial) { // 初始化保留首次测量值
        last_distance = current_dist;
        initial = false;
        return;
    }
    // 距离从闭合阈值下变为阈值上视为门打开
    if (last_distance <= 50
        && current_dist > 50) {
        door_state = true;
        door_operation_in_progress = true;
        door_opened_time = time(NULL);
        printf("Door opened detected\n");
        //上传门状态
        
        sprintf(door_msg, "{\'id\':4,\'dp\':{\'door\':[{\'v':\'柜门已打开\'}]}}");
        mqtt_publish(door_msg);
        
    }
    // 距离从阈值上降低到阈值下视为门关闭
    else if (last_distance > 50
        && current_dist <= 50) {
        door_state = false;
        door_operation_in_progress = false;
        door_opened_time = 0;
        printf("Door closed detected\n");
        //上传门状态
        
        sprintf(door_msg, "{\'id\':4,\'dp\':{\'door\':[{\'v':\'柜门已关闭\'}]}}");
        mqtt_publish(door_msg);
        
    }
    last_distance = current_dist;
    // 门未关紧检测逻辑（开门超过1分钟且无人移动）
    if (door_state) {
        time_t now = time(NULL);
        bool timeout = (now - door_opened_time) > 30;
        bool no_motion = pir() == 0;

        if (timeout && no_motion) {
            trigger_alarm("柜门未关紧");
        }
    }
}

//主流程函数
void normal_operation() {
    environment_monitor();
    light_fluctuation_check();
    handle_door_control();
    
    if (!door_operation_in_progress) {
        // 运动检测处理
        printf("已启用红外运动传感\n");
        /*
        int motion = pir();
        if (motion) {
            if (motion_start_time == 0) {
                motion_start_time = time(NULL);
            }

            // 3秒持续运动检测
            if ((time(NULL) - motion_start_time) >= 2) {
                fingerprint_active = true;
            }
            last_motion_time = time(NULL);
        }
        else {
            motion_start_time = 0;
        }
        */
        if (pir()) {
            fingerprint_active = true;
        }
    }
   
    // 指纹验证处理
    if (fingerprint_active) {

        int result = search_fingerprint();
        if (result != -2) {
            fingerprint_start_time = time(NULL);
            fingerprint_active = false;
            door_operation_in_progress = true;
        }
        else{
            fingerprint_active = false;
            return;
        }
    }
    handle_door_control();
}

int main() {
    
    mqtt_init();
    mqtt_connect();
    mqtt_subscribe();
    
    open_devices();

    while (1) {
        
        memset(light_msg, 0, 200);
        memset(temp_msg, 0, 200);
        memset(humidity_msg, 0, 200);
        memset(door_msg, 0, 200);
        memset(warn_msg, 0, 200);
        memset(user_msg, 0, 200);
        

        uint8_t sw_state = get_sw_state();
        if (sw_state != last_sw_state) {
            last_sw_state = sw_state;
            switch (sw_state)
            {
            case 0x01:
                delete_user_by_name_or_id();
                break;
            case 0x02:
                enroll_fingerprint();
                break;
            case 0x04:
                clear_database();
            }
        }

        normal_operation();

        usleep(100000); // 100ms间隔
    }
    return 0;
}