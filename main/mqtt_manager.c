#include "mqtt_manager.h" // Include your own header
#include "mqtt_client.h"
#include "esp_log.h"

// Static keeps this TAG local to this file only
static const char *TAG = "MQTT_MANAGER";
static esp_mqtt_client_handle_t global_client = NULL;

static bool is_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            is_connected = true;
            ESP_LOGI(TAG, "MQTT connected!");
            break;
        case MQTT_EVENT_DISCONNECTED:
            is_connected = false;
            ESP_LOGI(TAG, "MQTT disconnected!");
            break;
        default:
            break;
    }
}

void mqtt_app_start(void) {
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.1.173",
        .broker.address.port = 1883,
    };

    global_client = esp_mqtt_client_init(&mqtt_cfg); // Save to global
    esp_mqtt_client_register_event(global_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(global_client);
}

void mqtt_send_weather_data(float temp, float hum) {
    if (global_client == NULL) {
        ESP_LOGW(TAG, "Cannot publish: MQTT client not initialized");
        return;
    }
    if (!is_connected) {
        ESP_LOGW(TAG, "Skip Publish: Not connected to broker yet.");
        return;
    }

    char payload[64];
    // Create a JSON-like string
    snprintf(payload, sizeof(payload), "{\"temp\": %.2f, \"hum\": %.2f}", temp, hum);

    // Publish to "weather" topic
    int msg_id = esp_mqtt_client_publish(global_client, "weather", payload, 0, 1, 0);
    if (msg_id != -1) {
        ESP_LOGI(TAG, "Sent weather data, msg_id=%d", msg_id);
    } else {
        ESP_LOGE(TAG, "Failed to publish weather data");
    }
}