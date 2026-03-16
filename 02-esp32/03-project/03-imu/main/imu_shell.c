#include "imu_shell.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "imu_app.h"

#define IMU_SHELL_UART      UART_NUM_0
#define IMU_SHELL_BUF_SIZE  128

static const char *TAG = "imu_shell";

static void imu_shell_print_help(void)
{
    printf("commands:\r\n");
    printf("  help                 show this help\r\n");
    printf("  whoami               print imu who_am_i\r\n");
    printf("  once                 print one sample\r\n");
    printf("  start [n] [ms]       print n samples every ms, default start 10 200\r\n");
    printf("  stop                 stop streaming\r\n");
    printf("  status               show stream status\r\n");
}

static char *trim_line(char *line)
{
    while (*line != '\0' && isspace((unsigned char)*line)) {
        line++;
    }

    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }

    return line;
}

static void imu_shell_handle_line(char *line)
{
    char *trimmed = trim_line(line);
    if (trimmed[0] == '\0') {
        return;
    }

    char *argv[4] = {0};
    int argc = 0;
    char *token = strtok(trimmed, " ");
    while (token != NULL && argc < 4) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (strcmp(argv[0], "help") == 0) {
        imu_shell_print_help();
        return;
    }

    if (strcmp(argv[0], "whoami") == 0) {
        esp_err_t ret = imu_app_print_who_am_i();
        if (ret != ESP_OK) {
            printf("whoami failed: %s\r\n", esp_err_to_name(ret));
        }
        return;
    }

    if (strcmp(argv[0], "once") == 0) {
        esp_err_t ret = imu_app_print_one_sample();
        if (ret != ESP_OK) {
            printf("once failed: %s\r\n", esp_err_to_name(ret));
        }
        return;
    }

    if (strcmp(argv[0], "start") == 0) {
        uint32_t count = (argc >= 2) ? (uint32_t)strtoul(argv[1], NULL, 10) : 10;
        uint32_t period_ms = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 200;
        esp_err_t ret = imu_app_start_streaming(count, period_ms);
        if (ret == ESP_OK) {
            printf("stream started: count=%lu period_ms=%lu\r\n",
                   (unsigned long)count, (unsigned long)period_ms);
        } else {
            printf("stream start failed: %s\r\n", esp_err_to_name(ret));
        }
        return;
    }

    if (strcmp(argv[0], "stop") == 0) {
        imu_app_stop_streaming();
        printf("stream stop requested\r\n");
        return;
    }

    if (strcmp(argv[0], "status") == 0) {
        printf("streaming=%s\r\n", imu_app_is_streaming() ? "yes" : "no");
        return;
    }

    printf("unknown command: %s\r\n", argv[0]);
    imu_shell_print_help();
}

static void imu_shell_task(void *arg)
{
    uint8_t rx_buf[IMU_SHELL_BUF_SIZE];
    char line[IMU_SHELL_BUF_SIZE];
    int line_pos = 0;

    printf("\r\n03-imu shell ready, type help\r\n");

    while (true) {
        int len = uart_read_bytes(IMU_SHELL_UART, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        for (int i = 0; i < len; i++) {
            char c = (char)rx_buf[i];
            if (c == '\r' || c == '\n') {
                if (line_pos > 0) {
                    line[line_pos] = '\0';
                    imu_shell_handle_line(line);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = c;
            }
        }
    }
}

void imu_shell_start(void)
{
    const uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(IMU_SHELL_UART, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(IMU_SHELL_UART, &uart_cfg));
    xTaskCreate(imu_shell_task, "imu_shell", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "uart shell started");
}
