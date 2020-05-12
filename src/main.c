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


//For the Rev 1 & 2 of Livy Protect the GPIO values are RED= 22, GREEN =21 & BLUE=23.

//27mA normal working current w/o WIFI active
//0.1mA on deep_sleep



//Later all these should be move to the config file
//the definitions are not correct , they have to be #define on the config file
#define led_red_anode  GPIO_NUM_22
#define led_green_anode  GPIO_NUM_21
#define led_blue_anode  GPIO_NUM_23


//BUZZER
#define buzzer GPIO_NUM_16



//COMM
#define scl_1 GPIO_NUM_18
#define sda_1 GPIO_NUM_19

#define button GPIO_NUM_14

//Attention ! , just for the D203S PIR , check the model before uploading
#define bat_volt_adc GPIO_NUM_32 //Analog PIR on 32 and Digital PIR on 35

#define air_qual GPIO_NUM_34

#define stat1 GPIO_NUM_36
#define stat2 GPIO_NUM_39

//FOR PIR D203S (Pyroelektrischer Infrarot-Sensor im TO-5 Gehäuse)

//#define 3v3_pir_en GPIO_NUM_25
//#define pir_int	GPIO_NUM_2
//#define beeper_freq	2700



#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  10        /* Time ESP32 will go to sleep (in seconds) */

#define button_bitmask 0x4000 // 2^14 in HEX

///////////////////////////////////////////////////////////////////////////
////////////////DEFINING EVERYTHING WE NEED FOR WIFI///////////////////////
///////////////////////////////////////////////////////////////////////////

/* Set the SSID and Password via project configuration, or can set directly here */


//uint8_t DEFAULT_SSID[33] = { 0 };
//uint8_t DEFAULT_PWD[65] = { 0 };
//#define DEFAULT_SSID "SSID"
//#define DEFAULT_PWD "PWD"

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

    /* Initialize Wi-Fi as sta and set scan method

    // Initialize and start WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = DEFAULT_SSID,
            .password = DEFAULT_PWD,
            .scan_method = DEFAULT_SCAN_METHOD,
            .sort_method = DEFAULT_SORT_METHOD,
            .threshold.rssi = DEFAULT_RSSI,
            .threshold.authmode = DEFAULT_AUTHMODE,
        },
    };

     */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
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

    printf("------------------------------------------------------------\n\n");
    printf("Connection was successful...\n");
    printf("Sending out alarm and waiting for shut down...\n\n");
    printf("------------------------------------------------------------\n");

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    //Read into sending an alarm to the livy app
    //Consider to disconnect when no response

}


//function that handles communication
static void communicate_wifi()
{
    esp_sleep_wakeup_cause_t wakeup_reason;

    wakeup_reason = esp_sleep_get_wakeup_cause();


    //Depending on the wake-up reason send an alarm or send battery voltage...
    switch(wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0 : printf("\n\nWakeup caused by external signal using RTC_IO (EXT_0), sending alarm..."); break;
    //Our Button in ons EXT_0 config on the bitmask for the GPIO_14
    case ESP_SLEEP_WAKEUP_EXT1 : printf("\n\nWakeup caused by external signal using RTC_CNTL (EXT_1)"); break;
    case ESP_SLEEP_WAKEUP_TIMER : printf("\n\nWake-Up caused by timer, sending battery voltage..."); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : printf("\n\nWakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : printf("\n\nWakeup caused by ULP program"); break;
    default : printf("\n\nWakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
    }
}


//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////


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

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

int point_delay = 1*uS_TO_S_FACTOR;

RTC_DATA_ATTR int bootCount = 0; //Saving into the RTC MEM the of wake up number


void led_blink(gpio_num_t anode,float interval,int repetitions)
{

  const char *led_color = ( anode == led_red_anode   ) ? "red"
                         :( anode == led_green_anode ) ? "green"
                         :( anode == led_blue_anode  ) ? "blue"
                         :                               " "    ;

  gpio_pad_select_gpio(anode);
  gpio_set_direction(anode, GPIO_MODE_OUTPUT);

  int count = 0;
  interval = interval*1000;
  printf("\n\n");

  while(repetitions > count)
  {
    printf("\rTesting the %s led :  ON " , led_color );
    fflush(stdout);

    //Sending the anode to ground (Closing the circuit so led turns on)
    gpio_set_level(anode, 0);

    vTaskDelay(interval / portTICK_PERIOD_MS);

    printf("\rTesting the %s led :  OFF " , led_color );
    fflush(stdout);

    //Sending the anode to high impedance(opening the circuit so led turns off)
    gpio_set_level(anode, 1);

    vTaskDelay(interval / portTICK_PERIOD_MS);

    count++;
  }

}

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : printf("\n\nWakeup caused by external signal using RTC_IO (EXT_0)"); break;
    //Our Button in ons EXT_0 config on the bitmask for the GPIO_14
    case ESP_SLEEP_WAKEUP_EXT1 : printf("\n\nWakeup caused by external signal using RTC_CNTL (EXT_1)"); break;
    case ESP_SLEEP_WAKEUP_TIMER : printf("\n\nWake-Up caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : printf("\n\nWakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : printf("\n\nWakeup caused by ULP program"); break;
    default : printf("\n\nWakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

//Entry Point
void app_main(void)
{

  ++bootCount;  //increment every time we wake up , is RTC so sleep resistant


  //////////////////////////SMART CONFIGURE//////////////////////////////////
  if(bootCount <= 1)
  {
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    printf("\nWaiting for smart configure...\n");
    vTaskDelay(20000 / portTICK_PERIOD_MS);
  }
  ///////////////////////////////////////////////////////////////////////////
  printf("\n\nI am UP !  , Boot number: %d ",bootCount);

  if (bootCount > 1) print_wakeup_reason();

  /*
  First we configure the wake up source
  We set our ESP32 to wake up for an external trigger.
  There are two types for ESP32, ext0 and ext1 .
  ext0 uses RTC_IO to wakeup thus requires RTC peripherals
  to be on while ext1 uses RTC Controller so doesnt need
  peripherals to be powered on.
  Note that using internal pullups/pulldowns also requires
  RTC peripherals to be turned on.

  We will be using the GPIO 14(is the button on livy_v1)
  The GPIO_14 is the RTC_GPIO16

  Two kinds of wake ups will be implemented , every 10 secs by the timer
  and every time the user push the button(GPIO_14 to gnd) more than 1 sec
  */

  //Configuring int. timer (most likely not using it due to v_reg inneficiency)
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  //Only RTC IO can be used as a source for external wake
  //source. They are pins: 0,2,4,12-15,25-27,32-39
  //The button is on pin 14 on Livy Leak

  //Button normally on Ground , pressed on HIGH
  //esp_sleep_enable_ext1_wakeup(button_bitmask, ESP_EXT1_WAKEUP_ANY_HIGH) ;

  printf("\n\nWill go to sleep for %d Seconds\n", TIME_TO_SLEEP);
  printf("or until you push the button,\n");
  printf("whatever occurs first\n\n");

  /*
  DELETE HERE FOR FULL DEMO

  for (int i = 1 ; i <= 10 ; i++)
    {
        printf("Hello world! .... Nr. %d of 10 \n" , i );
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

  led_blink(led_red_anode  , 1.00f  , 3  ); //blinking red
  led_blink(led_green_anode, 0.50f  , 5  ); //blinking green
  led_blink(led_blue_anode , 0.25f , 7  ); //blinking blue

  printf("\n\n\nPress the Button to scan the WIFI networks"); fflush(stdout);

  gpio_set_direction(button, GPIO_MODE_INPUT);
  gpio_pulldown_en(button);

  uint64_t currentMillis = esp_timer_get_time() + point_delay; // 1 second more

  while(1)
  {
    if (esp_timer_get_time() >= currentMillis)
    {
      printf(" . "); fflush(stdout);
      currentMillis = esp_timer_get_time() + point_delay ;
    }
    vTaskDelay(10);
    if (gpio_get_level(button)) break ; //Pull up Button (1v N.O. , 3.5v closed)
  }


  //Here Scan the WIFIs



  printf("\n\n\nPlease connect the charger");

  currentMillis = esp_timer_get_time() + point_delay;

  gpio_set_direction(stat1, GPIO_MODE_INPUT);
  gpio_pullup_en(stat1);

  gpio_set_direction(stat2 , GPIO_MODE_INPUT);
  gpio_pullup_en(stat2);

  while(1) //Waiting for charger connection (stat1 or stat2 set to low)
  {
    if (esp_timer_get_time() >= currentMillis)
    {
      printf(" . "); fflush(stdout);
      currentMillis = esp_timer_get_time() + point_delay ;
    }
    vTaskDelay(10);

    //Normally set to high when not charging
    if ( !gpio_get_level(stat1) || !gpio_get_level(stat2) ) break;
  }

  vTaskDelay(20);
  printf("\n\n------------------ Charging -----------------------------\n\n");

  //Ring the Buzzer here

  //Later include  the battery voltage there
  gpio_set_direction(bat_volt_adc,GPIO_MODE_INPUT);

  //ADC1_GPIO32_CHANNEL is the ADC1 channel number of GPIO 32 (ADC1 channel 4)


  int iterator=0;

  //If stat1 and stat2 are different means there is no battery
  while(1) //Charging (4 times low and 4 times high)
  {
    vTaskDelay(10);
    printf(" \r STAT1: %d , STAT2: %d  , Battery Voltage : %d  " ,
           gpio_get_level(stat1),
           gpio_get_level(stat2),
           gpio_get_level(bat_volt_adc));
    fflush(stdout);

    if (gpio_get_level(stat1) || gpio_get_level(stat2))
    {
      if (iterator < 10) iterator++ ; else break ;
    }
    else iterator =0;




    //Battery Voltage

    //Compare the two levels for 3 nested ifs , redundant
    //exit when the Stat1 and stat2 are solid high and not iterating anymore

    //Charger Disconnected , break the loop
  }


  printf("\n\n---------- Charger Disconnected ------------------------\n\n\n\n");



  DELETE HERE FOR FULL DEMO  */



  //Not charging anymore

  //printf("STAT1: %d , STAT2: %d  \n", gpio_get_level(stat1), gpio_get_level(stat2) );

  //show bat volt and exit when charger disconnected

  //gpio_set_direction(PIN_STAT1, GPIO_MODE_INPUT);
	//gpio_set_direction(PIN_STAT2, GPIO_MODE_INPUT);

  //Activate wifi , display , and connect to the ones we know

  /////////////////////////////////////////////////////////////////////////////////////
  //////////////////////Scanning and connecting the wifi///////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////

  if(bootCount > 1)
  {
    scan_and_connect();
  }


  /////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////


  //Display the Bluetooths we know


  //Important to remap it as otherwise will still be config for the sleep trigger
  gpio_set_direction(button, GPIO_MODE_INPUT);
  gpio_pulldown_en(button);
  vTaskDelay(10);


  if (gpio_get_level(button))
  {
    printf("\n\n");

    printf("\r----- BUTTON IS STILL PRESSED , RELEASE TO SLEEP -----------");
    fflush(stdout);

    //PWM definitions for the alarm led (RED)


  	int minValue      = 1023;  // micro seconds (uS)

    //At this value we already have the full brightness so from here we sweep
  	int maxValue      = 500; // micro seconds (uS)

    //The LED in on at GND (duty=0) , therefore we start with the highest duty
    int duty          = minValue ;

  	ledc_timer_config_t timer_conf;
    timer_conf.duty_resolution = LEDC_TIMER_10_BIT;
  	timer_conf.freq_hz    = 300;
  	timer_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
  	timer_conf.timer_num  = LEDC_TIMER_0;
  	ledc_timer_config(&timer_conf);

  	ledc_channel_config_t ledc_conf;
  	ledc_conf.channel    = LEDC_CHANNEL_0;
  	ledc_conf.duty       = duty;
  	ledc_conf.gpio_num   = led_red_anode;
  	ledc_conf.intr_type  = LEDC_INTR_DISABLE;
  	ledc_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
  	ledc_conf.timer_sel  = LEDC_TIMER_0;
  	ledc_channel_config(&ledc_conf);

    //Looping until the button is released


    while(1)
    {
      for(int i = minValue  ; i > maxValue  ; i-=50)
      {
        if (!gpio_get_level(button)) break; //Just break the for
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, i );
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(10);
      }

      if (!gpio_get_level(button)) break; //Break the while

      for(int i = maxValue  ; i < minValue  ; i+=50)
      {
        if (!gpio_get_level(button)) break; //Just break the for
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, i );
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(10);
      }

      if (!gpio_get_level(button)) break; //Break the while
    }

    //TURNING COMPLETELY OFF THE ALARM
    gpio_pad_select_gpio(led_red_anode);
    gpio_set_direction(led_red_anode, GPIO_MODE_OUTPUT);
    gpio_set_level(led_red_anode, 1);
  }

  for(int i=9 ; i>0 ; i--)
  {
    i > 1 ? printf("\r-------- GOING TO SLEEP IN %d SECONDS -------------" , i)
          : printf("\r-------- GOING TO SLEEP IN %d SECOND  -------------" , i);

    fflush(stdout);
    vTaskDelay(100); //1 second
  }

  printf("\r----------------- SLEEPING ----------------------");
  fflush(stdout);

  //Redefining the button to act as trigger for wake up
  esp_sleep_enable_ext1_wakeup(button_bitmask, ESP_EXT1_WAKEUP_ANY_HIGH) ;

  //Here the program sleeps if the butt is not pressed,
  //nothing beyond this point will be exec.

  esp_deep_sleep_start();

}
