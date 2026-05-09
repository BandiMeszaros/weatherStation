#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "NETWORK_CONNECTION";
# define WIFI_SSID CONFIG_WIFI_SSID
# define WIFI_PASSWORD CONFIG_WIFI_PASSWORD

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi started. Attempting to connect to SSID: %s...", WIFI_SSID);
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to the AP! Waiting for IP address...");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Disconnected from Wi-Fi. Retrying connection...");
                esp_wifi_connect();
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Successfully obtained IP address!");
        ESP_LOGI(TAG, "IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Network is ready for MQTT.");
    }
}

void wifi_init_sta(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi Station...");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize TCP/IP and Event Loop
    ESP_LOGI(TAG, "Initializing TCP/IP stack...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 3. Init Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 4. Register Event Handlers
    ESP_LOGI(TAG, "Registering event handlers...");
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, 
                                                        ESP_EVENT_ANY_ID, 
                                                        &event_handler, 
                                                        NULL, 
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, 
                                                        IP_EVENT_STA_GOT_IP, 
                                                        &event_handler, 
                                                        NULL, 
                                                        NULL));

    // 5. Configure
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            /* Setting a threshold forces to connect to any security mode or better */
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 6. Start!
    ESP_LOGI(TAG, "Starting Wi-Fi Driver...");
    ESP_ERROR_CHECK(esp_wifi_start());
}