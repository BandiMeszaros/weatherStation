#ifndef WEATHER_DATA_H
#define WEATHER_DATA_H


typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;

void i2c_master_init(void);
sensor_data_t get_weather_data(void);

#endif