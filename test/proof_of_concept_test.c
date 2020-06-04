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
#include "driver/i2c.h"

//defining GPIO pins
#define button GPIO_NUM_19
#define water GPIO_NUM_18
#define shutdown GPIO_NUM_17

#define led_water GPIO_NUM_16
#define led_button GPIO_NUM_4



//Slave address
#define ESP_SLAVE_ADDR 0x68
//pins
#define RTC 16
#define ACK_VAL 0x0                             /*!< I2C ack value */
#define NACK_VAL 0x1				/*!< I2C nack value */
#define ACK_CHECK_EN 0x1

/**
 * @brief i2c master initialization
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_NUM_0; 			//I2C port 0
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;				//set ESP32 as master
    conf.sda_io_num = 21;						//set sda GPIO number
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;	//enable sda pull up
    conf.scl_io_num = 22;						//set scl GPIO number
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;	//enable scl pull up
    conf.master.clk_speed = 100000;				//set clk freq to 100kHz	
    i2c_param_config(i2c_master_port, &conf);
    return i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0); //install driver
}


//function to write to a slave
//writes one byte of data to specified adress
static esp_err_t i2c_write(i2c_port_t i2c_num, uint8_t adress, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ESP_SLAVE_ADDR << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, adress, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, data, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

//function to make RTC trigger Alarm once per minute
void inta_minute(void){

	//write to RTC registers to setup INTA once per minute
	ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x07, 0x00));
	ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x08, 0xFF));
	ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x09, 0xFF));
	ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x0A, 0xFF));
	ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x0E, 0x07));	
}

//function to return boot up reason
int boot_up_reason(void){

	// returns the boot up reason
	// 0 -> water is detected
	// 1 -> manual boot up
	// 2 -> RTC

	if(gpio_get_level(water) == 1) return 0;
	if(gpio_get_level(button) == 1) return 1;
	else return 2;
}

//function to shutdown the device
static void shut_down(void){

	gpio_set_level(shutdown, 1);
}


void app_main(void)
{

	/* Set the GPIO direction */
	gpio_set_direction(button, GPIO_MODE_INPUT);
	gpio_pulldown_en(button);
	gpio_set_direction(water, GPIO_MODE_INPUT);
	gpio_pulldown_en(water);
	gpio_set_direction(shutdown, GPIO_MODE_OUTPUT);
	gpio_set_direction(led_water, GPIO_MODE_OUTPUT);
	gpio_set_direction(led_button, GPIO_MODE_OUTPUT);

	

	//we need to wait some amount of time, otherwise button is always high
	printf("I'm awake...\n");
	vTaskDelay(100 / portTICK_PERIOD_MS);


	//check if water is detected
	//if true connect to Wi-Fi and send out alarm
	if(boot_up_reason() == 0){

		//send out alarm
		printf("ALLERT! WATER IS DETECTED!\n");
		//wait for 10 mins
	
		gpio_set_level(led_water, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_water, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_water, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_water, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_water, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_water, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		//has to be done after every boot up, because if the interrupt happens
		printf("Write to RTC\n");
		
		ESP_ERROR_CHECK(i2c_master_init());		
		ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x0F, 0x00));

		vTaskDelay(100 / portTICK_PERIOD_MS);

		//shut ESP down
		printf("Shutdown!\n");

		shut_down();

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	//check if boot up was manual
	//if true do smart configure routine and turn off
	if(boot_up_reason() == 1){
		//light up a LED or something

		printf("Manual boot up...\n");

		gpio_set_level(led_button, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		
		
		printf("Write to RTC\n");
			
		//initialize master i2c
		ESP_ERROR_CHECK(i2c_master_init());
		//ALARM once per minute
		inta_minute();
		
		vTaskDelay(100 / portTICK_PERIOD_MS);

		//Set INTA low again
		//has to be done after every boot up, because if the interrupt happens
		printf("Write to RTC\n");
		
		ESP_ERROR_CHECK(i2c_master_init());		
		ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x0F, 0x00));

		vTaskDelay(100 / portTICK_PERIOD_MS);

		printf("Shutdown!\n");

		//shut ESP down
		shut_down();

		vTaskDelay(1000 / portTICK_PERIOD_MS);

	}

	if(boot_up_reason() == 2){

		printf("No boot up reason detected, so it must be RTC\n");

		gpio_set_level(led_button, 1);
		gpio_set_level(led_water, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 0);
		gpio_set_level(led_water, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);	
		gpio_set_level(led_button, 1);
		gpio_set_level(led_water, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(led_button, 0);
		gpio_set_level(led_water, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		//Set INTA low again
		
		printf("Write to RTC\n");
		
		ESP_ERROR_CHECK(i2c_master_init());		
		ESP_ERROR_CHECK(i2c_write(I2C_NUM_0, 0x0F, 0x00));

		vTaskDelay(100 / portTICK_PERIOD_MS);

		//shut ESP down
		printf("Shutdown!\n");

		shut_down();

		vTaskDelay(1000 / portTICK_PERIOD_MS);

	}
	
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	shut_down();

}
