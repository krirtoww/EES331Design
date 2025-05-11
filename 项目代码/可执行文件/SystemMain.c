#include <stdio.h>
#include <unistd.h>  // ����sleep()����
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





//�ϴ�����׼��
unsigned char light_msg[200] = { 0 };
unsigned char temp_msg[200] = { 0 };
unsigned char humidity_msg[200] = { 0 };
unsigned char door_msg[200] = { 0 };
unsigned char warn_msg[200] = { 0 };
unsigned char user_msg[200] = { 0 };

// ��������
int light_val = 0;
float temp = 0;
float humidity = 0;

// ��״̬���
int last_distance = 0;
bool door_state = false;
bool door_operation_in_progress = false;
time_t door_opened_time = 0;

// ָ����֤���
bool fingerprint_active = false;
time_t fingerprint_start_time = 0;
int verify_fail_count = 0;
const int MAX_FAIL_ATTEMPTS = 5;

// �˶�������
time_t motion_start_time = 0;
time_t last_motion_time = 0;

// ���ռ�����
int light_history[5] = { 0 };
int light_index = 0;

// ָ����
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
static uint8_t last_sw_state = 0xFF;  // �ϴε�SW״̬�������ж�״̬�仯

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
        return 1;//��⵽���˷���1
        sleep(1);
    }
    else
        return 0;//����ʱ����0
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

// ��ȡ���뿪��״̬�仯
static uint8_t get_sw_state() {
    uint8_t sw;
    lseek(sw_fd, 0, SEEK_SET);
    if (read(sw_fd, &sw, 1) != 1) return 0xFF;
    return sw;
}

// ����ID�����Ƶ�ӳ��
static int save_name_to_file(int id, const char* name) {
    FILE* file = fopen(TEMPLATE_FILE, "a");
    if (file == NULL) {
        printf("�޷����ļ�����ӳ��\n");
        return -1;
    }

    // ����ID�����Ƶ��ļ�
    fprintf(file, "ID=%d, Name=%s\n", id, name);
    fclose(file);

    return 0;
}

static int get_name_from_file(int id, char* name) {
    FILE* file = fopen(TEMPLATE_FILE, "r");
    if (file == NULL) {
        printf("�޷����ļ���ȡӳ��\n");
        return -1;
    }

    char line[100];
    while (fgets(line, sizeof(line), file)) {
        int file_id;
        char file_name[MAX_NAME_LENGTH];

        // ����ÿһ�����ݣ���ȡID������
        if (sscanf(line, "ID=%d, Name=%49[^\n]", &file_id, file_name) == 2) {
            if (file_id == id) {
                strcpy(name, file_name);  // �ҵ�ƥ���ID����������
                fclose(file);
                return 0;
            }
        }
    }

    printf("û���ҵ���Ӧ��ID=%d\n", id);
    fclose(file);
    return -1;  // δ�ҵ�ƥ��ID
}


// ��������Ϊԭʼģʽ
static void configure_uart(int fd) {
    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
}

// ���������
static int send_cmd(uint8_t code, uint8_t* params, int plen) {
    uint8_t buf[32];
    int idx = 0, sum = 0;
    uint16_t pkg_len = 1 + plen + 2;
    buf[idx++] = 0xEF; buf[idx++] = 0x01;
    buf[idx++] = 0xFF; buf[idx++] = 0xFF;
    buf[idx++] = 0xFF; buf[idx++] = 0xFF;
    buf[idx++] = 0x01;  // �����
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

// ����ʱ��ȡָ���ֽ�
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

// һ���Զ�ȡӦ��������� status + payload
static int read_ack(uint8_t* status, uint8_t* payload_out, int* payload_len) {
    uint8_t ch, hdr[7], payload[256];
    int state = 0;
    // ͬ�� EF01
    while (state < 2) {
        if (read_bytes(&ch, 1, 500) != 1) return -1;
        if (state == 0 && ch == 0xEF) state = 1;
        else if (state == 1 && ch == 0x01) state = 2;
        else state = 0;
    }
    // ��ʣ��ͷ��
    if (read_bytes(hdr, 7, 500) != 7) return -1;
    uint16_t len = (hdr[5] << 8) | hdr[6];
    if (len < 3 || len > 256) return -1;
    if (read_bytes(payload, len, 500) != len) return -1;
    if (status) *status = payload[0];
    if (payload_out && payload_len) {
        int actual = len - 3;  // ȥ�� status + checksum(2B)
        memcpy(payload_out, &payload[1], actual);
        *payload_len = actual;
    }
    return 0;
}

// ��ȡָ�ƿ�ģ������
static uint16_t get_template_count() {
    uint8_t status, payload[16];
    int payload_len;
    send_cmd(ZFM_TEMPLATE_NUM, NULL, 0);
    if (read_ack(&status, payload, &payload_len) != 0 || status != 0x00 || payload_len < 2)
        return 0;
    return (payload[0] << 8) | payload[1];
}

// ע��ָ��
static void enroll_fingerprint() {
    uint8_t status, params[3], buf1 = 1, buf2 = 2;
    int count;
    printf("=== ָ��ע�� ===\n");
    // ��һ�βɼ�
    printf("�������ָ...\n");
    do {
        send_cmd(ZFM_GEN_IMG, NULL, 0);
    } while (read_ack(&status, NULL, NULL) == 0 && status == 0x02);
    if (status != 0x00)  return;
    send_cmd(ZFM_IMG2TZ, &buf1, 1);
    if (read_ack(&status, NULL, NULL) != 0 || status != 0x00)return;
    // �ڶ��βɼ�
    printf("���ƿ����ٴη�����ָ...\n"); sleep(1);
    do {
        send_cmd(ZFM_GEN_IMG, NULL, 0);
    } while (read_ack(&status, NULL, NULL) == 0 && status == 0x02);
    if (status != 0x00) { printf("�ɼ�ʧ�� %02X\n", status); return; }
    send_cmd(ZFM_IMG2TZ, &buf2, 1);
    if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) { printf("����2ʧ�� %02X\n", status); return; }
    send_cmd(ZFM_REG_MODEL, NULL, 0);
    if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) { printf("�ϲ�ʧ�� %02X\n", status); return; }
    // ȥ��
    count = get_template_count();
    printf("��ǰģ����: %d\n", count);
    for (int i = 0; i < count; i++) {
        params[0] = 2; params[1] = (i >> 8) & 0xFF; params[2] = i & 0xFF;
        send_cmd(ZFM_LOAD_CHAR, params, 3);
        if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
            uint8_t mp[2] = { 1,2 };
            send_cmd(ZFM_MATCH, mp, 2);
            if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
                printf("�Ѵ���ģ��ID %d\n", i);
                return;
            }
        }
    }
    // �洢��ģ��
    params[0] = 1; params[1] = (count >> 8) & 0xFF; params[2] = count & 0xFF;
    send_cmd(ZFM_STORE, params, 3);
    if (read_ack(&status, NULL, NULL) == 0 && status == 0x00)
        printf("ע��ɹ���ID=%d\n", count);
    else
        printf("�洢ʧ�� %02X\n", status);

    char user_name[MAX_NAME_LENGTH];
    // ��ʾ�û���������
    printf("�������û����ƣ�");
    fgets(user_name, MAX_NAME_LENGTH, stdin);
    user_name[strcspn(user_name, "\n")] = 0;  // �Ƴ����з�

    // ����ID������ӳ��
    if (save_name_to_file(count, user_name) == 0) {
        printf("�û������ѱ��档\n");
    }
    else {
        printf("�洢ʧ�� %02X\n", status);
    }
}

//ʶ��ָ��
static int search_fingerprint() {
    uint8_t status, buf1 = 1;
    int count;
    time_t start_time = 0;
    int is_no_finger = 0;  // �����ж��Ƿ�û����ָ
    printf("=== ָ������ ===\n");
    count = get_template_count();
    // ִ��ָ��ƥ��
    while (1) {
        uint8_t sw_state = get_sw_state();
        if (sw_state != 0)
            return -2;
        send_cmd(ZFM_GEN_IMG, NULL, 0);
        if (read_ack(&status, NULL, NULL) != 0) {
            printf("��ȡȷ����ʧ��\n");
            continue;
        }

        // ���û����ָ��ȷ����0x02��
        if (status == 0x02) {
            if (!is_no_finger) {
                start_time = time(NULL);  // ��¼��ʼʱ��
                is_no_finger = 1;  // ���û����ָ
            }
            printf("time:%d", time(NULL) - start_time);
            // �����������10�룬���˳�����
            if (is_no_finger && (time(NULL) - start_time) > 10) {
                printf("����10��δ��⵽��ָ���˳�������\n");
                return -2;  // ��ʱ�˳�
            }
        }
        else if (status == 0x00) {
            is_no_finger = 0;
            send_cmd(ZFM_IMG2TZ, &buf1, 1);
            if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) {
                continue;  // �������²ɼ�
            }

            for (int i = 0; i < count; i++) {
                uint8_t p[3] = { 2, (i >> 8) & 0xFF, i & 0xFF };
                send_cmd(ZFM_LOAD_CHAR, p, 3);
                if (read_ack(&status, NULL, NULL) != 0 || status != 0x00) {
                    continue;  // �������ɹ���ģ��
                }

                uint8_t mp[2] = { 1, 2 };
                send_cmd(ZFM_MATCH, mp, 2);
                if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
                    printf("ƥ��ID=%d\n", i);
                    
                    char name[MAX_NAME_LENGTH];

                    if (get_name_from_file(i, name) == 0) {
                        printf("�û������ǣ�%s\n", name);
                    }
                    else {
                        printf("δ�ҵ�ID=%d���û����ơ�\n", i);
                    }
                    //�ϴ�ʶ�𵽵��û�id
                    sprintf(user_msg, "{\'id\':5,\'dp\':{\'user\':[{\'v':\'id: %d ���ƣ�%s\'}]}}", i, name);
                    mqtt_publish(user_msg);
                    
                    return i;  // ƥ��ɹ����˳�
                }
            }
        }

        printf("δƥ�䣬��������...\n");
    }
}

static int remove_user_from_file(const char* keyword, int* out_id) {
    FILE* file = fopen(TEMPLATE_FILE, "r");
    if (!file) {
        printf("�޷���ӳ���ļ�\n");
        return -1;
    }

    FILE* temp = fopen("temp.txt", "w");
    if (!temp) {
        fclose(file);
        printf("�޷�������ʱ�ļ�\n");
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
                continue;  // ��д�� temp �ļ���ʵ��ɾ��
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

// ����ɾ��ָ��ģ������
static int delete_fingerprint_template(int page_id) {
    uint8_t payload[4];
    payload[0] = (page_id >> 8) & 0xFF;
    payload[1] = page_id & 0xFF;
    payload[2] = 0x00;  // N=1 ���ֽ�
    payload[3] = 0x01;  // N=1 ���ֽ�

    uint8_t status;
    send_cmd(0x0C, payload, 4);
    if (read_ack(&status, NULL, NULL) == 0 && status == 0x00) {
        return 0;
    }
    else {
        printf("ɾ��ʧ�ܣ�ȷ����: %02X\n", status);
        return -1;
    }
}

// �����ƺ���������ؼ��ʣ�ɾ��ƥ���û�
void delete_user_by_name_or_id() {

    char input[MAX_NAME_LENGTH];
    printf("������Ҫɾ�����û����ƻ�ID��");
    fgets(input, MAX_NAME_LENGTH, stdin);
    input[strcspn(input, "\n")] = 0;  // �Ƴ����з�

    int id = -1;
    if (remove_user_from_file(input, &id) == 0) {
        printf("�ҵ��û���ID=%d����ʼɾ��ģ��...\n", id);
        if (delete_fingerprint_template(id) == 0)
            printf("ģ��ɾ���ɹ���\n");
        else
            printf("ģ��ɾ��ʧ�ܡ�\n");
    }
    else {
        printf("δ�ҵ���Ӧ���û���ID��\n");
    }
}
// ������ݿ�
static void clear_database() {
    int count = get_template_count();
    printf("=== ������ݿ� ===\n��ǰģ����: %d\n", count);
    if (!count) { printf("���ѿ�\n"); return; }
    uint8_t status;
    send_cmd(ZFM_EMPTY, NULL, 0);
    if (read_ack(&status, NULL, NULL) == 0 && status == 0x00)
        printf("��ճɹ�\n");
    else
        printf("���ʧ�� %02X\n", status);
}

// ��������� I2C д����
int i2c_write_reg(u8 reg, u8 Data) {
    u8 buf[2] = { reg, Data };
    if (write(i2c_fd, buf, 2) != 2) {
        perror("I2C write failed");
        return -1;
    }
    return 0;
}

// I2C ������
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

// ��������������ȡ
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
    // ����H4��H5�Ķ�ȡ��ʽ
    int8_t h4_msb = (int8_t)BME280Read8(BME280_REG_DIG_H4);
    uint8_t h4_lsb = BME280Read8(BME280_REG_DIG_H4 + 1);
    dig_H4 = (h4_msb << 4) | (h4_lsb & 0x0F);

    int8_t h5_msb = (int8_t)BME280Read8(BME280_REG_DIG_H5 + 1);
    dig_H5 = (h5_msb << 4) | (BME280Read8(BME280_REG_DIG_H5) >> 4);

    dig_H6 = (int8_t)BME280Read8(BME280_REG_DIG_H6);
}

// �����¶ȼ���
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

// ����ʪ�ȼ���
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

// ����ѹ������
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
        return 0; // ���������
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

//�豸��ʼ������
void open_devices()
{
    sw_fd = open(SW_DEVICE, O_RDONLY);
    zfm_fd = open(ZFM_DEVICE, O_RDWR | O_NOCTTY);
    if (sw_fd < 0 || zfm_fd < 0) {
        perror("���豸ʧ��");
        return;
    }
    configure_uart(zfm_fd);
    return;
}

// ��������
void trigger_alarm(const char* reason) {
    //printf("ALERT: %s\n", reason);
    sprintf(warn_msg, "{\'id\':6,\'dp\':{\'warn\':[{\'v':\'[WARN] %s\'}]}}", reason);
    mqtt_publish(warn_msg);
    
}

//��ȡ��������
void environment_monitor() {
    barometer(); // ������ʪ��

    // �¶ȼ��
    if (temp < 14 || temp > 24) {
        trigger_alarm("�¶��쳣");
    }

    // ʪ�ȼ��
    if (humidity < 45 || humidity > 60) {
        trigger_alarm("ʪ���쳣");
    }

    // ����ǿ�ȼ��
    int light_current = light();
    if (light_current > 0) { // ���Զ�ȡʧ��
        light_val = light_current;
        if (light_val > 100) { // ����100Ϊ�������ֵ
            trigger_alarm("����ǿ�ȹ���");
        }
    }
    printf("temp: %.2f ��C, humidity : %.2f %%, light : %d\n", temp, humidity, light_val);
    
    //�ϴ�����ǿ��
    sprintf(light_msg, "{\'id\':1,\'dp\':{\'light\':[{\'v':\'%d\'}]}}", light_val);
    mqtt_publish(light_msg);
    //�ϴ��¶�����
    sprintf(temp_msg, "{\'id\':2,\'dp\':{\'temp\':[{\'v':\'%.2f\'}]}}", temp);
    mqtt_publish(temp_msg);
    //�ϴ�ʪ������
    sprintf(humidity_msg, "{\'id\':3,\'dp\':{\'humidity\':[{\'v':\'%.2f\'}]}}", humidity);
    mqtt_publish(humidity_msg);
    
}

//���վ��Ҳ�����⺯��
void light_fluctuation_check() {
    light_history[light_index] = light_val;
    light_index = (light_index + 1) % 5;

    int fluctuations = 0;
    for (int i = 1; i < 5; i++) {
        int diff = abs(light_history[i] - light_history[i - 1]);
        if (diff > 50) { // ��ֵ��Ϊ50��λ
            fluctuations++;
        }
    }

    if (fluctuations >= 3) {
        trigger_alarm("��⵽���վ��ұ仯");
    }
}

//����״̬��⺯��
void handle_door_control() {
    int current_dist = ult();

    static bool initial = true;
    if (initial) { // ��ʼ�������״β���ֵ
        last_distance = current_dist;
        initial = false;
        return;
    }
    // ����ӱպ���ֵ�±�Ϊ��ֵ����Ϊ�Ŵ�
    if (last_distance <= 50
        && current_dist > 50) {
        door_state = true;
        door_operation_in_progress = true;
        door_opened_time = time(NULL);
        printf("Door opened detected\n");
        //�ϴ���״̬
        
        sprintf(door_msg, "{\'id\':4,\'dp\':{\'door\':[{\'v':\'�����Ѵ�\'}]}}");
        mqtt_publish(door_msg);
        
    }
    // �������ֵ�Ͻ��͵���ֵ����Ϊ�Źر�
    else if (last_distance > 50
        && current_dist <= 50) {
        door_state = false;
        door_operation_in_progress = false;
        door_opened_time = 0;
        printf("Door closed detected\n");
        //�ϴ���״̬
        
        sprintf(door_msg, "{\'id\':4,\'dp\':{\'door\':[{\'v':\'�����ѹر�\'}]}}");
        mqtt_publish(door_msg);
        
    }
    last_distance = current_dist;
    // ��δ�ؽ�����߼������ų���1�����������ƶ���
    if (door_state) {
        time_t now = time(NULL);
        bool timeout = (now - door_opened_time) > 30;
        bool no_motion = pir() == 0;

        if (timeout && no_motion) {
            trigger_alarm("����δ�ؽ�");
        }
    }
}

//�����̺���
void normal_operation() {
    environment_monitor();
    light_fluctuation_check();
    handle_door_control();
    
    if (!door_operation_in_progress) {
        // �˶���⴦��
        printf("�����ú����˶�����\n");
        /*
        int motion = pir();
        if (motion) {
            if (motion_start_time == 0) {
                motion_start_time = time(NULL);
            }

            // 3������˶����
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
   
    // ָ����֤����
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

        usleep(100000); // 100ms���
    }
    return 0;
}