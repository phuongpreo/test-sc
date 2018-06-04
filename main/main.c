/* Esptouch example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;


/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "sc";

bool findNetwork = 0;

void smartconfig_example_task(void *parm);
void printStationList(system_event_t *event);

static char *get_authmode(wifi_auth_mode_t authmode)
{
    char *mode_name[] = {"OPEN", "WEP", "WPA PSK", "WPA2 PSK", "WPA WPA2 PSK", "MAX"};
    return mode_name[authmode];
}


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        printStationList(event);
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        printf("Connected to the network \n");
    break;

    default : break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));


    //trong config thi nvs: enable
    //.nvs_enable = WIFI_NVS_ENABLED,
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());


    //let us test a wifi scan ...
    /*
    wifi_scan_config_t scan_config = {0};
    scan_config.show_hidden = false;
    scan_config.scan_time.passive = 1000;
    */

    wifi_scan_config_t scanConf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = 1};

    printf("\nStarting to Scan Wifi Acess Point\n");
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, 0));



}

static void sc_callback(smartconfig_status_t status, void *pdata)
{
    switch (status)
    {
    case SC_STATUS_WAIT:
        ESP_LOGI(TAG, "SC_STATUS_WAIT");
        break;
    case SC_STATUS_FIND_CHANNEL:
        ESP_LOGI(TAG, "SC_STATUS_FINDING_CHANNEL");
        break;
    case SC_STATUS_GETTING_SSID_PSWD:
        ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
        break;
    case SC_STATUS_LINK:
        ESP_LOGI(TAG, "SC_STATUS_LINK");
        wifi_config_t *wifi_config = pdata;
        ESP_LOGI(TAG, "SSID:%s", wifi_config->sta.ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", wifi_config->sta.password);
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());

        break;
    case SC_STATUS_LINK_OVER:
        ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");

        if (pdata != NULL)
        {
            uint8_t phone_ip[4] = {0};
            memcpy(phone_ip, (uint8_t *)pdata, 4);
            ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
        }
        xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
        break;
    default:
        break;
    }
}

void smartconfig_example_task(void *parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    ESP_ERROR_CHECK(esp_smartconfig_start(sc_callback));
    while (1)
    {
        uxBits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if (uxBits & ESPTOUCH_DONE_BIT)
        {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

// print the list of connected stations
void printStationList(system_event_t *event)
{

    printf("WiFi Scan Completed!\n");
    printf("Number of access points found: %d\n", event->event_info.scan_done.number);
    uint16_t apCount = event->event_info.scan_done.number;
    if (apCount != 0)
    {

        wifi_ap_record_t *list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));

        // check if wifi already config last time?, then check if it is in the scan list
        wifi_config_t config;

        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &config))

        ESP_LOGI(TAG, "SSID:%s", config.sta.ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", config.sta.password);
        printf("We found network connected last time: \n");
        printf("               SSID              | Password \n");
        printf("----------------------------------------------------------------\n");
        printf("%32s | %16s \n", (char *)config.sta.ssid, (char *)config.sta.password);
        printf("----------------------------------------------------------------\n");

        printf("\n");
        printf("               SSID              | Channel | RSSI |   Auth Mode \n");
        printf("----------------------------------------------------------------\n");
        for (int i = 0; i < apCount; i++)
        {
            printf("%32s | %7d | %4d | %12s\n", (char *)list[i].ssid, list[i].primary, list[i].rssi, get_authmode(list[i].authmode));

            if (strcmp((char *)&config.sta.ssid, (char *)&list[i].ssid) == 0) // match
            {
                findNetwork = 1;
                printf("Khop SSID");
            }

        }

        printf("----------------------------------------------------------------\n");

        free(list);

        if (findNetwork == 1)
        {
            printf(" The network %s is in the list, ready to connect, don't need to run smart config\n", config.sta.ssid);
            // try to connect to the network
            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
            printf("Connecting to network: %s...", config.sta.ssid);
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
    }
}
void task_printf(void *pvParameters)
{
    for(;;){
        printf("STT:\n");
        if(esp_wifi_connect()==ESP_OK)
        	printf ("connet");
        		else printf("disconnet");

        vTaskDelay(1000/portTICK_RATE_MS);
    }
}

void app_main()
{
    xTaskCreate(task_printf, "task_print", 4096, NULL, 3, NULL);
    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi();
}
