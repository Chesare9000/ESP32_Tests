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



//For the Rev 1 & 2 of Livy Protect the GPIO values are RED= 22, GREEN =21 & BLUE=23.

//27mA normal working current w/o WIFI active
//0.1mA on deep_sleep



//Later all these should be move to the config file
//the definitions are not correct , they have to be #define on the config file
#define led_red_anode  GPIO_NUM_22
#define led_green_anode  GPIO_NUM_21
#define led_blue_anode  GPIO_NUM_23

#define buzzer GPIO_NUM_16

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

/* DELETE HERE FOR FULL DEMO

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

    while(1)
    {
      if (!gpio_get_level(button)) break;
      vTaskDelay(30);
    }
  }

  for(int i=9 ; i>0 ; i--)
  {
    i > 1 ? printf("\r-------- GOING TO SLEEP IN %d SECONDS -------------" , i)
          : printf("\r-------- GOING TO SLEEP IN %d SECOND  -------------" , i);

    fflush(stdout);
    vTaskDelay(100);
  }

  printf("\r----------------- SLEEPING ----------------------");
  fflush(stdout);

  //Redefining the button to act as trigger for wake up
  esp_sleep_enable_ext1_wakeup(button_bitmask, ESP_EXT1_WAKEUP_ANY_HIGH) ;

  //Here the program sleeps if the butt is not pressed,
  //nothing beyond this point will be exec.

  esp_deep_sleep_start();

}
