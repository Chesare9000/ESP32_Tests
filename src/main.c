#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/adc.h"

//For the Rev 1 & 2 of Livy Protect the GPIO values are RED= 22, GREEN =21 & BLUE=23.


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

int point_delay = 1000000;

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

//Entry Point
void app_main(void)
{
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

  printf("\n\n---------- Charger Disconnected --------------------------\n\n");

  //Not charging anymore

  //printf("STAT1: %d , STAT2: %d  \n", gpio_get_level(stat1), gpio_get_level(stat2) );

  //show bat volt and exit when charger disconnected

  //gpio_set_direction(PIN_STAT1, GPIO_MODE_INPUT);
	//gpio_set_direction(PIN_STAT2, GPIO_MODE_INPUT);

  //Activate wifi , display , and connect to the ones we know

  //Display the Bluetooths we know



}
