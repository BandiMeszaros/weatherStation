#include "mqtt_manager.h" // Include your own header
#include "mqtt_client.h"
#include "esp_log.h"
#include <string.h>

// Static keeps this TAG local to this file only
static const char *TAG = "MQTT_MANAGER";
static esp_mqtt_client_handle_t global_client = NULL;

static bool is_connected = false;

// Human readable names so the log shows what event actually arrived
static const char *mqtt_event_name(esp_mqtt_event_id_t event) {
    switch (event) {
        case MQTT_EVENT_ANY:           return "ANY";
        case MQTT_EVENT_ERROR:         return "ERROR";
        case MQTT_EVENT_CONNECTED:     return "CONNECTED";
        case MQTT_EVENT_DISCONNECTED:  return "DISCONNECTED";
        case MQTT_EVENT_SUBSCRIBED:    return "SUBSCRIBED";
        case MQTT_EVENT_UNSUBSCRIBED:  return "UNSUBSCRIBED";
        case MQTT_EVENT_PUBLISHED:     return "PUBLISHED";
        case MQTT_EVENT_DATA:          return "DATA";
        case MQTT_EVENT_BEFORE_CONNECT: return "BEFORE_CONNECT";
        case MQTT_EVENT_DELETED:       return "DELETED";
        default:                       return "UNKNOWN";
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    ESP_LOGD(TAG, "Event received: %s (id=%d)", mqtt_event_name((esp_mqtt_event_id_t)event_id), (int)event_id);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "Connecting to broker %s ...", CONFIG_MQTT_BROKER_IP);
            break;
        case MQTT_EVENT_CONNECTED:
            is_connected = true;
            ESP_LOGI(TAG, "MQTT connected to %s:%d", CONFIG_MQTT_BROKER_IP, 1883);
            break;
        case MQTT_EVENT_DISCONNECTED:
            is_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected from %s:%d", CONFIG_MQTT_BROKER_IP, 1883);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Broker acknowledged publish, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribe acknowledged, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscribe acknowledged, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Incoming data on topic '%.*s': %.*s",
                     event->topic_len, event->topic, event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            is_connected = false;
            ESP_LOGE(TAG, "MQTT error event, error_handle=%p", (void *)event->error_handle);
            if (event->error_handle != NULL) {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    ESP_LOGE(TAG, "  TCP transport error: sock_errno=%d (%s), tls=%d",
                             event->error_handle->esp_transport_sock_errno,
                             strerror(event->error_handle->esp_transport_sock_errno),
                             event->error_handle->esp_tls_last_esp_err);
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    ESP_LOGE(TAG, "  Connection refused, return code=0x%02x",
                             event->error_handle->connect_return_code);
                } else {
                    ESP_LOGE(TAG, "  error_type=%d", event->error_handle->error_type);
                }
            }
            break;
        default:
            break;
    }
}

void mqtt_app_start(void) {
    static char broker_uri[64];
    snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s", CONFIG_MQTT_BROKER_IP);

    // Empty Kconfig strings mean "no credentials", so pass NULL instead of ""
    const char *username = (CONFIG_MQTT_USER[0] != '\0') ? CONFIG_MQTT_USER : NULL;
    const char *password = (CONFIG_MQTT_PASSWORD[0] != '\0') ? CONFIG_MQTT_PASSWORD : NULL;

    ESP_LOGI(TAG, "Starting MQTT client: uri=%s port=%d user=%s password=%s",
             broker_uri, 1883,
             (username != NULL) ? username : "<none>",
             (password != NULL) ? "<set>" : "<none>");

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .broker.address.port = 1883,
        .credentials.username = username,
        .credentials.authentication.password = password,
    };

    ESP_LOGI(TAG, "Calling esp_mqtt_client_init()");
    global_client = esp_mqtt_client_init(&mqtt_cfg); // Save to global
    if (global_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init() FAILED, client is NULL");
        return;
    }
    ESP_LOGI(TAG, "esp_mqtt_client_init() OK, client=%p", (void *)global_client);

    ESP_LOGI(TAG, "Calling esp_mqtt_client_register_event()");
    esp_err_t err = esp_mqtt_client_register_event(global_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event() FAILED: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "esp_mqtt_client_register_event() OK");

    ESP_LOGI(TAG, "Calling esp_mqtt_client_start()");
    err = esp_mqtt_client_start(global_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start() FAILED: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "esp_mqtt_client_start() OK");
}

void mqtt_send_weather_data(float temp, float hum) {
    ESP_LOGD(TAG, "mqtt_send_weather_data() called with temp=%.2f hum=%.2f", temp, hum);

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
    ESP_LOGI(TAG, "Calling esp_mqtt_client_publish() topic='weather' payload='%s'", payload);
    int msg_id = esp_mqtt_client_publish(global_client, "weather", payload, 0, 1, 0);
    if (msg_id != -1) {
        ESP_LOGI(TAG, "esp_mqtt_client_publish() OK, msg_id=%d", msg_id);
    } else {
        ESP_LOGE(TAG, "esp_mqtt_client_publish() FAILED (returned -1)");
    }
}
