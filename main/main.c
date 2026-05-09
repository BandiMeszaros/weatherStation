#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "weather_data.h"
#include "network_connection.h"
#include "mqtt_manager.h"


# define SENSOR_MEASUREMENT_INTERVAL_MS CONFIG_MEASUREMENT_INTERVAL_MS

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI("MAIN", "Starting Weather Station Application...");
    wifi_init_sta();
    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for Wi-Fi to connect and get IP
    mqtt_app_start();
    while (1) {
        sensor_data_t data = get_weather_data();
        mqtt_send_weather_data(data.temperature, data.humidity);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_MEASUREMENT_INTERVAL_MS));
    }

}