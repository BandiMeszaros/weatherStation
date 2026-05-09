#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

/**
 * @brief Initializes and starts the MQTT client.
 * Connects to the broker defined in the .c file.
 */
void mqtt_app_start(void);
void mqtt_send_weather_data(float temp, float hum);

#endif