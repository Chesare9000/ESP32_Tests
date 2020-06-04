#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/adc.h"
#include "esp_sleep.h"
#include "driver/ledc.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdlib.h>
#include "esp_wpa2.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

//defining GPIO pins
#define button GPIO_NUM_14
//#define water GPIO_NUM_XX
//#define shutdown GPIO_NUM_XX
//communication with RTC
//#define scl_1 GPIO_NUM_XX
//#define sda_1 GPIO_NUM_XX

//function to return boot up reason
int boot_up_reason(void){

	// returns the boot up reason
	// 0 -> water is detected
	// 1 -> manual boot up
	// 2 -> RTC

	//if(gpio_get_level(water)) return 0;
	//else if(gpio_get_level(button)) return 1;
	//else return 2;

	if(gpio_get_level(button) == 1) return 1;
	else return 2;
}

//function to shutdown the device
//static void shut_down(void){

//	gpio_set_level(shutdown, 1);
//}


///////////////////////////////////////////////////////////////////////////
////////////////DEFINING EVERYTHING WE NEED FOR WIFI///////////////////////
///////////////////////////////////////////////////////////////////////////

/*CONFIG_EXAMPLE_SCAN_METHOD*/
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN

/*CONFIG_EXAMPLE_SORT_METHOD*/
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL


/*CONFIG_EXAMPLE_FAST_SCAN_THRESHOLD*/
#define DEFAULT_RSSI -127
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN


///////////////////////////////////////////////////////////////////////////
////////////////FUNCTIONS AND CONSTANTS WE NEED FOR WIFI///////////////////
///////////////////////////////////////////////////////////////////////////

wifi_config_t wifi_config;

static const char *TAG = "scan";

static void event_handler_wifi(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}


/* Initialize Wi-Fi as sta and set scan method */
static void fast_scan(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_wifi, NULL, NULL));

    // Initialize default station as network interface instance (esp-netif)
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    //esp_wifi_get_config gets wifi_config from NVS
    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}


//main only has to call this function to connect to Wifi specified in DEFINITIONS

static void scan_and_connect(void)
{

    printf("------------------------------------------------------------\n\n");
    printf("Trying to connect to known wifi...\n\n");
    printf("------------------------------------------------------------\n");
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    fast_scan();

		vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("------------------------------------------------------------\n\n");
    printf("Connection was successful...\n");
    printf("Sending out alarm and waiting for shut down...\n\n");
    printf("------------------------------------------------------------\n");

}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////
////////////FUNCTIONS AND CONSTANTS WE NEED FOR SMART_CONFIG///////////////
///////////////////////////////////////////////////////////////////////////

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG1 = "smartconfig_example";

static void smartconfig_example_task(void * parm);

static void event_handler_config(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG1, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG1, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG1, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG1, "SSID:%s", ssid);
        ESP_LOGI(TAG1, "PASSWORD:%s", password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_config, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_config, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler_config, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG1, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG1, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////




void app_main(void)
{

	/* Set the GPIO direction */
	gpio_set_direction(button, GPIO_MODE_INPUT);
	gpio_pulldown_en(button);
	//gpio_set_direction(water, GPIO_MODE_INPUT);
	//gpio_set_direction(shutdown, GPIO_MODE_OUTPUT);

	//we need to wait some amount of time, otherwise button is always high
	printf("Waiting 10 miliseconds...\n");
	vTaskDelay(10 / portTICK_PERIOD_MS);


	//check if water is detected
	//if true connect to Wi-Fi and send out alarm
	if(boot_up_reason() == 0){
		//connect to Wi-Fi
		//send out alarm
		printf("ALLERT! WATER IS DETECTED!\n");
		//wait for 10 mins
		//shut_down();
		// Restart module
		for (int i = 10; i >= 0; i--) {
			printf("Restarting in %d seconds...\n", i);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
		printf("Restarting now.\n");
		fflush(stdout);
		esp_restart();
	}

	//check if boot up was manual
	//if true do smart configure routine and turn off
	if(boot_up_reason() == 1){
		//light up a LED or something
		printf("Manual boot up...\n");
		//smart_config...
		ESP_ERROR_CHECK( nvs_flash_init() );
		initialise_wifi();
		printf("\nWaiting for smart configure...\n");
		//programm RTC
		//shut_down();
		// Restart module
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		for (int i = 10; i >= 0; i--) {
			printf("Restarting in %d seconds...\n", i);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
		printf("Restarting now.\n");
		fflush(stdout);
		esp_restart();

	}

	//check if RTC is responsible for boot up
	//if true connect to Wi-Fi and report battery
	if(boot_up_reason() == 2){
		printf("RTC boot up...\n");
		//connect to Wi-Fi
		scan_and_connect();
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		//report battery status
		printf("Battery is at XX percent.\n");
		// Restart module
		for (int i = 10; i >= 0; i--) {
			printf("Restarting in %d seconds...\n", i);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
		printf("Restarting now.\n");
		fflush(stdout);
		esp_restart();
	}

}
