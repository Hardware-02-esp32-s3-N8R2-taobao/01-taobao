#include "icm42688.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ICM42688_WHO_AM_I_REG       0x75
#define ICM42688_WHO_AM_I_VALUE     0x47
#define ICM42688_DEVICE_CONFIG_REG  0x11
#define ICM42688_TEMP_DATA1_REG     0x1D
#define ICM42688_PWR_MGMT0_REG      0x4E
#define ICM42688_GYRO_CONFIG0_REG   0x4F
#define ICM42688_ACCEL_CONFIG0_REG  0x50
#define ICM42688_REG_BANK_SEL_REG   0x76

#define ICM42688_SPI_READ_FLAG      0x80
#define ICM42688_RESET_VALUE        0x01
#define ICM42688_BANK0              0x00

static const char *TAG = "icm42688";

static esp_err_t icm42688_write_reg(icm42688_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t tx_data[2] = {reg & 0x7F, value};
    spi_transaction_t transaction = {
        .length = sizeof(tx_data) * 8,
        .tx_buffer = tx_data,
    };

    return spi_device_transmit(dev->spi, &transaction);
}

static esp_err_t icm42688_read_regs(icm42688_t *dev, uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t tx_data[16] = {0};
    uint8_t rx_data[16] = {0};

    if (len + 1 > sizeof(tx_data)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx_data[0] = reg | ICM42688_SPI_READ_FLAG;

    spi_transaction_t transaction = {
        .length = (len + 1) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    esp_err_t ret = spi_device_transmit(dev->spi, &transaction);
    if (ret != ESP_OK) {
        return ret;
    }

    memcpy(data, &rx_data[1], len);
    return ESP_OK;
}

static esp_err_t icm42688_select_bank0(icm42688_t *dev)
{
    return icm42688_write_reg(dev, ICM42688_REG_BANK_SEL_REG, ICM42688_BANK0);
}

static float icm42688_convert_accel(int16_t raw)
{
    return (float)raw / 8192.0f;
}

static float icm42688_convert_gyro(int16_t raw)
{
    return (float)raw / 32.8f;
}

static float icm42688_convert_temp(int16_t raw)
{
    return ((float)raw / 132.48f) + 25.0f;
}

esp_err_t icm42688_init(icm42688_t *dev, const icm42688_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    memset(dev, 0, sizeof(*dev));
    dev->config = *config;

    spi_bus_config_t bus_config = {
        .mosi_io_num = config->mosi_io,
        .miso_io_num = config->miso_io,
        .sclk_io_num = config->sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    esp_err_t ret = spi_bus_initialize(config->host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = config->clock_hz,
        .mode = 0,
        .spics_io_num = config->cs_io,
        .queue_size = 1,
    };

    ESP_RETURN_ON_ERROR(spi_bus_add_device(config->host, &dev_config, &dev->spi), TAG, "add spi device failed");

    ESP_RETURN_ON_ERROR(icm42688_select_bank0(dev), TAG, "select bank0 failed");
    ESP_RETURN_ON_ERROR(icm42688_write_reg(dev, ICM42688_DEVICE_CONFIG_REG, ICM42688_RESET_VALUE), TAG, "soft reset failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(icm42688_select_bank0(dev), TAG, "select bank0 failed after reset");

    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(icm42688_read_who_am_i(dev, &who_am_i), TAG, "read who am i failed");
    ESP_RETURN_ON_FALSE(who_am_i == ICM42688_WHO_AM_I_VALUE, ESP_ERR_NOT_FOUND, TAG, "unexpected who_am_i=0x%02X", who_am_i);

    ESP_RETURN_ON_ERROR(icm42688_write_reg(dev, ICM42688_PWR_MGMT0_REG, 0x0F), TAG, "power config failed");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(icm42688_write_reg(dev, ICM42688_GYRO_CONFIG0_REG, 0x06), TAG, "gyro config failed");
    ESP_RETURN_ON_ERROR(icm42688_write_reg(dev, ICM42688_ACCEL_CONFIG0_REG, 0x66), TAG, "accel config failed");

    return ESP_OK;
}

esp_err_t icm42688_read_who_am_i(icm42688_t *dev, uint8_t *who_am_i)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    ESP_RETURN_ON_FALSE(who_am_i != NULL, ESP_ERR_INVALID_ARG, TAG, "who_am_i is null");
    return icm42688_read_regs(dev, ICM42688_WHO_AM_I_REG, who_am_i, 1);
}

esp_err_t icm42688_read_sample(icm42688_t *dev, icm42688_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_ARG, TAG, "dev is null");
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG, "sample is null");

    uint8_t raw[14] = {0};
    ESP_RETURN_ON_ERROR(icm42688_read_regs(dev, ICM42688_TEMP_DATA1_REG, raw, sizeof(raw)), TAG, "read sample failed");

    int16_t temp_raw = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t accel_x = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t accel_y = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t accel_z = (int16_t)((raw[6] << 8) | raw[7]);
    int16_t gyro_x = (int16_t)((raw[8] << 8) | raw[9]);
    int16_t gyro_y = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gyro_z = (int16_t)((raw[12] << 8) | raw[13]);

    sample->temperature_c = icm42688_convert_temp(temp_raw);
    sample->accel_x_g = icm42688_convert_accel(accel_x);
    sample->accel_y_g = icm42688_convert_accel(accel_y);
    sample->accel_z_g = icm42688_convert_accel(accel_z);
    sample->gyro_x_dps = icm42688_convert_gyro(gyro_x);
    sample->gyro_y_dps = icm42688_convert_gyro(gyro_y);
    sample->gyro_z_dps = icm42688_convert_gyro(gyro_z);

    return ESP_OK;
}
