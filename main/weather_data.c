#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "rom/cache.h"
#include "sdkconfig.h"
#include "weather_data.h"

// Settings

static const char *TAG = "WEATHER_DATA";


i2c_master_bus_config_t bus_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = -1,                // -1 lets the driver auto-select an available port
    .scl_io_num = CONFIG_SENSOR_I2C_SCL,      // XIAO SCL
    .sda_io_num = CONFIG_SENSOR_I2C_SDA,      // XIAO SDA
    .glitch_ignore_cnt = 7,
};

i2c_master_bus_handle_t bus_handle;

// Registering GXHT30
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x44,        // GXHT30 default address
    .scl_speed_hz = 400000,        // 400kHz
};

i2c_master_dev_handle_t dev_handle;


void i2c_master_init() {
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
    ESP_LOGI(TAG, "I2C master initialized");
}
sensor_data_t convert_gxht30_data(uint8_t *rx_data) {
    // Temperature is in bytes 0 (MSB) and 1 (LSB)
    uint16_t raw_temp = (rx_data[0] << 8) | rx_data[1];
    
    // Humidity is in bytes 3 (MSB) and 4 (LSB) 
    uint16_t raw_humidity = (rx_data[3] << 8) | rx_data[4];

    //Apply conversion formulas
    float temperature = -45.0f + (175.0f * (float)raw_temp / 65535.0f);
    float humidity = 100.0f * ((float)raw_humidity / 65535.0f);

    if (humidity > 100) humidity = 100;
    if (humidity < 0) humidity = 0;

    ESP_LOGI(TAG, "Temperature: %.2f °C\n", temperature);
    ESP_LOGI(TAG, "Humidity: %.2f %%\n", humidity);

    sensor_data_t results;
    results.temperature = temperature;
    results.humidity = humidity;
    return results;
}


sensor_data_t get_weather_data() {
    uint8_t cmd[] = {0x2C, 0x06};
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, cmd, sizeof(cmd), -1));
    vTaskDelay(pdMS_TO_TICKS(20));
    // Reading back 6 bytes (Temperature MSB, LSB, CRC, Humidity MSB, LSB, CRC)
    uint8_t rx_data[6];
    ESP_ERROR_CHECK(i2c_master_receive(dev_handle, rx_data, sizeof(rx_data), -1));
    return convert_gxht30_data(rx_data);
}
