#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cJSON.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mqtt_client.h"

char http_response[1024];

static const char *TAG = "MQTT_TCP";

double convert_wind_speed(double wind_speed_meters_per_second)
{
    double wind_speed_km_per_h = wind_speed_meters_per_second * 3.6;
    return wind_speed_km_per_h;
}

char* convert_wind_direction(double wind_deg)
{
    if (wind_deg >= 337.5 || wind_deg < 22.5)
    { return "N";}
    else if (wind_deg >= 22.5 && wind_deg < 67.5)
    { return "NE";}
    else if (wind_deg >= 67.5 && wind_deg < 112.5)
    { return "E";}
    else if (wind_deg >= 112.5 && wind_deg < 157.5)
    { return "SE";}
    else if (wind_deg >= 157.5 && wind_deg < 202.5)
    { return "S";}
    else if (wind_deg >= 202.5 && wind_deg < 247.5)
    { return "SW";}
    else if (wind_deg >= 247.5 && wind_deg < 292.5)
    { return "W";}
    else if (wind_deg >= 292.5 && wind_deg < 337.5)
    { return "NW";}
    else
    { return "N/A";}
}

void rest_get();

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, "weather/get_weather", 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("\nTOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        rest_get();

        cJSON *root = cJSON_Parse(http_response);

        cJSON *_main = cJSON_GetObjectItem(root,"main");
        cJSON *_weather = cJSON_GetObjectItem(root,"weather");
        cJSON *_sys= cJSON_GetObjectItem(root,"sys");
        cJSON *_wind = cJSON_GetObjectItem(root,"wind");
        cJSON *_index = cJSON_GetArrayItem(_weather, 0);
        cJSON *wind_gust=NULL; //Placeholder so the ESP doesn't freak out
        if (cJSON_GetObjectItem(_wind,"gust")){
            wind_gust = cJSON_GetObjectItem(_wind,"gust");
        }

        char *city = cJSON_GetObjectItem(root,"name")->valuestring;
        char *country_code = cJSON_GetObjectItem(_sys,"country")->valuestring;
        int dt = cJSON_GetObjectItem(root, "dt")->valueint;
        int timezone = cJSON_GetObjectItem(root, "timezone")->valueint;
        double temp = cJSON_GetObjectItem(_main, "temp")->valuedouble;
        int humidity = cJSON_GetObjectItem(_main, "humidity")->valueint;
        char *description = cJSON_GetObjectItem(_index, "description")->valuestring;
        double wind_speed = cJSON_GetObjectItem(_wind, "speed")->valuedouble;
        double wind_deg = cJSON_GetObjectItem(_wind, "deg")->valuedouble;

        time_t epoch = dt + timezone;
        char *wind_direction = convert_wind_direction(wind_deg);
        wind_speed = convert_wind_speed(wind_speed);
    
        struct tm ts;
        char current_time[80];
        ts = *localtime(&epoch);
        strftime(current_time , sizeof ( current_time ), "%A, %B %d, %Y %I:%M:%S %p", &ts);


        if (wind_gust){
        
        int length = snprintf(NULL, 0,"%s,%s,%s,%.1f,%d,%s,%.1f,%s,%.1f", city, country_code, current_time, temp, humidity, description, wind_speed, wind_direction,wind_gust->valuedouble);
        char str[length+1];
        sprintf(str, "%s,%s,%s,%.1f,%d,%s,%.1f,%s,%.1f", city, country_code, current_time, temp, humidity, description, wind_speed, wind_direction,wind_gust->valuedouble);
        esp_mqtt_client_publish(client, "weather/current_weather", str, 0, 1, 0);
        }
        else{
            int length = snprintf(NULL, 0,"%s,%s,%s,%.1f,%d,%s,%.1f,%s", city, country_code, current_time, temp, humidity, description, wind_speed, wind_direction);
            char str[length+1];
            sprintf(str, "%s,%s,%s,%.1f,%d,%s,%.1f,%s", city, country_code, current_time, temp, humidity, description, wind_speed, wind_direction);
            esp_mqtt_client_publish(client, "weather/current_weather", str, 0, 1, 0);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://192.168.1.106:1883",  //Change to mqtt://(device's IPv4 address):1883
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}