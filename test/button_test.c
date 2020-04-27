#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/adc.h"

//Button pressed for different time intervals
//if the butten is pressed longer than 1 second -> red LED lights up
//if the butten is pressed less than 1 second -> blue LED lights up
//if the butten is pressed less than 500 milliseconds -> green LED lights up



//defining GPIO pins
#define BLINK_GPIO_RED 22
#define BLINK_GPIO_GREEN 21
#define BLINK_GPIO_BLUE 23

#define buzzer GPIO_NUM_16
#define button GPIO_NUM_14

void app_main(void)
{


  gpio_set_direction(button, GPIO_MODE_INPUT);
  gpio_pulldown_en(button);

  gpio_pad_select_gpio(BLINK_GPIO_RED);
  gpio_pad_select_gpio(BLINK_GPIO_GREEN);
  gpio_pad_select_gpio(BLINK_GPIO_BLUE);
  
  gpio_set_direction(BLINK_GPIO_RED, GPIO_MODE_OUTPUT);
  gpio_set_direction(BLINK_GPIO_GREEN, GPIO_MODE_OUTPUT);
  gpio_set_direction(BLINK_GPIO_BLUE, GPIO_MODE_OUTPUT);

  gpio_set_level(BLINK_GPIO_RED, 1);
  gpio_set_level(BLINK_GPIO_GREEN, 1);
  gpio_set_level(BLINK_GPIO_BLUE, 1);

  
  while(1){
   
    //check if button is pressed
    if(gpio_get_level(button)){
      //delay 500ms and check again
      vTaskDelay(500/portTICK_PERIOD_MS);
      if(gpio_get_level(button)){
	//delay 500ms and check again
        vTaskDelay(500/portTICK_PERIOD_MS);
        if(gpio_get_level(button)){
          //button has been pressed for more than 1 sec
          gpio_set_level(BLINK_GPIO_RED, 0);
          printf("Button was pressed longer than 1 second.\n");
	  vTaskDelay(2000/portTICK_PERIOD_MS);
        }else{
          gpio_set_level(BLINK_GPIO_BLUE, 0);
          printf("Button was pressed less than 1 second.\n");
          vTaskDelay(2000/portTICK_PERIOD_MS);
        }
      }else{
        gpio_set_level(BLINK_GPIO_GREEN, 0);
        printf("Button was pressed less than 500 milli seconds.\n");
        vTaskDelay(2000/portTICK_PERIOD_MS);
      }
    }

    vTaskDelay(10/portTICK_PERIOD_MS);
    //turning LEDs of again
    gpio_set_level(BLINK_GPIO_RED, 1);
    gpio_set_level(BLINK_GPIO_GREEN, 1);
    gpio_set_level(BLINK_GPIO_BLUE, 1);
  }
}
