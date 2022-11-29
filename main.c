#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include <cJSON.h>

char http_response[512];
static unsigned int http_index;


static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    // 1 - Wi-Fi/LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation 					s1.1
    esp_event_loop_create_default();     // event loop 			                s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station 	                    s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); // 					                    s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = "YOUR WIFI SSID",
            .password = "YOUR WIFI PASSWORD"}};
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    // 3 - Wi-Fi Start Phase
    esp_wifi_start();
    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();
}

esp_err_t client_event_get_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
                http_index = http_index + evt->data_len;
                strcat(http_response, (char*)evt->data);
                http_response[http_index] = '\0';
        // printf("HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

int convert_epoch(time_t epoch)
{
    struct tm ts;
    char buff[80];
    ts = *localtime(&epoch);
    strftime(buf , sizeof ( buff ), "%A, %B %d, %Y %I:%M:%S %p", &ts);
    printf ("% s\n", buff );
    return 0;
}

double convert_wind_speed(double wind_speed_meters_per_second)
{
    double wind_speed_km_per_h = wind_speed_meters_per_second * 3.6;
    return wind_speed_km_per_h;
}

char* convert_wind_direction(double wind_deg)
{
    if (wind_deg >= 337.5 || wind_deg < 22.5)
    {
        return "N";
    }
    else if (wind_deg >= 22.5 && wind_deg < 67.5)
    {
        return "NE";
    }
    else if (wind_deg >= 67.5 && wind_deg < 112.5)
    {
        return "E";
    }
    else if (wind_deg >= 112.5 && wind_deg < 157.5)
    {
        return "SE";
    }
    else if (wind_deg >= 157.5 && wind_deg < 202.5)
    {
        return "S";
    }
    else if (wind_deg >= 202.5 && wind_deg < 247.5)
    {
        return "SW";
    }
    else if (wind_deg >= 247.5 && wind_deg < 292.5)
    {
        return "W";
    }
    else if (wind_deg >= 292.5 && wind_deg < 337.5)
    {
        return "NW";
    }
    else
    {
        return "N/A";
    }
}

static void rest_get()
{
    http_index = 0;
    memset(http_response, 0, 512);
    strcpy(http_response, "");
    esp_http_client_config_t config_get = {
        .url = "http://api.openweathermap.org/data/2.5/weather?q=Beirut,LB&units=metric&appid=263ef3582b967a7eb181c5e42797a2f6",
        .method = HTTP_METHOD_GET,
        .cert_pem = NULL,
        .event_handler = client_event_get_handler};
        
    esp_http_client_handle_t client = esp_http_client_init(&config_get);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

void app_main(void)
{
    nvs_flash_init();
    wifi_connection();

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    printf("WIFI was initiated ...........\n\n");

    rest_get();
    
    // Using cJSON, parse retrieved JSON string into human readable weather data
    cJSON *root = cJSON_Parse(http_response);

    cJSON *_main = cJSON_GetObjectItem(root,"main");
    cJSON *_weather = cJSON_GetObjectItem(root,"weather");
    cJSON *_sys= cJSON_GetObjectItem(root,"sys");
    cJSON *_wind = cJSON_GetObjectItem(root,"wind");
    cJSON *_index = cJSON_GetArrayItem(_weather, 0);

    char *city = cJSON_GetObjectItem(root,"name")->valuestring;
    char *country_code = cJSON_GetObjectItem(_sys,"country")->valuestring;
    int dt = cJSON_GetObjectItem(_main, "dt")->valueint;
    int timezone = cJSON_GetObjectItem(_main, "timezone")->valueint;
    double temp = cJSON_GetObjectItem(_main, "temp")->valuedouble;
    int humidity = cJSON_GetObjectItem(_main, "humidity")->valueint;
    char *description = cJSON_GetObjectItem(_index, "description")->valuestring;
    char *description = cJSON_GetObjectItem(_index, "description")->valuestring;
    double wind_speed = cJSON_GetObjectItem(_wind, "speed")->valuedouble;
    double wind_deg = cJSON_GetObjectItem(_wind, "deg")->valuedouble;

    time_t epoch = dt + timezone;
    int current_time = convert_epoch(epoch);
    char *wind_direction = convert_wind_direction(wind_deg);
    double wind_speed = convert_wind_speed(wind_speed);
    
    printf("Weather Conditions in %s, %s \n, ",city,country_code);
    printf(current_time);
    printf("\nTemperature: %.1f C", temp);
    printf("\nHumidity: %d %%", humidity);
    printf("\nDescription: %s\n", description);
    printf("Wind: %.1f km/h from the %s\n", wind_speed, wind_direction);

}