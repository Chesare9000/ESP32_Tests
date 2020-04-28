/*
 * cBeeper.cpp
 *
 *  Created on: 18.10.2017 (c) EmSo
 *      Author: N. Lubin
 */
#include "cBeeper.h"
#include "cApplication.h"
#include <esp_log.h>

#define LEDC_BEEPER_TIMER		LEDC_TIMER_1
#define LEDC_BEEPER_MODE		LEDC_HIGH_SPEED_MODE
#define LEDC_BEEPER_GPIO		PIN_SPKR
#define LEDC_BEEPER_CHANNEL		LEDC_CHANNEL_1
#define LEDC_BEEPER_TIMER_BITS	LEDC_TIMER_12_BIT

enum beeperState_t
{
	BEEPER_INITIAL_STATE        = 0x00,
	BEEPER_IDLE_STATE           = 0x01,
	BEEPER_BUSY_STATE           = 0x02,
	BEEPER_ERROR_STATE          = 0xFF,
};

#define TEST_NOTES_AMOUNT 1

beeperNote_t testTuneNotes[TEST_NOTES_AMOUNT] = {
		{.frequency = BEEPER_PWM_FREQUENCY, .duration = 1000},
};

beeperTune_t testTune = {
		.options = {.cyclic = 1, .reserved = 0},
		.pause = 0,
		.notesAmount = TEST_NOTES_AMOUNT,
		.notes = testTuneNotes,
};


#define START_NOTES_AMOUNT 3

beeperNote_t startTuneNotes[START_NOTES_AMOUNT] = {
		{.frequency = 1000, .duration = 200},
		{.frequency = 2000, .duration = 200},
		{.frequency = 3000, .duration = 200},
};

beeperTune_t startTune = {
		.options = {.cyclic = 0, .reserved = 0},
		.pause = 0,
		.notesAmount = START_NOTES_AMOUNT,
		.notes = startTuneNotes,
};


#define ALARM_NOTES_AMOUNT 6

beeperNote_t alarmTuneNotes[ALARM_NOTES_AMOUNT] = {
		{.frequency = 3000, .duration = 500},
		{.frequency = 1500, .duration = 500},
		{.frequency = 3000, .duration = 500},
		{.frequency = 1500, .duration = 500},
		{.frequency = 3000, .duration = 500},
		{.frequency = 1500, .duration = 500},
};

beeperTune_t alarmTune = {
		.options = {.cyclic = 1, .reserved = 0},
		.pause = 2000,
		.notesAmount = ALARM_NOTES_AMOUNT,
		.notes = alarmTuneNotes,
};

beeperTune_t alarmTuneShort = {
		.options = {.cyclic = 0, .reserved = 0},
		.pause = 0,
		.notesAmount = 4,
		.notes = alarmTuneNotes,
};


static const char *TAG = "cBeeper";

int cBeeper::beeperState = BEEPER_INITIAL_STATE;


cBeeper::cBeeper() {
	beeperIndex = 0;
	beeperTune = nullptr;
	cur_frequency = 0;
}

cBeeper::~cBeeper() {

}

void cBeeper::Start() {
	if(BEEPER_INITIAL_STATE == beeperState) {
		//beeperSet(0);
		ledc_channel = {
				//GPIO number
				.gpio_num = LEDC_BEEPER_GPIO,
				//set LEDC mode, from ledc_mode_t
				.speed_mode = (ledc_mode_t)LEDC_BEEPER_MODE,
				//set LEDC channel 0
				.channel = (ledc_channel_t)LEDC_BEEPER_CHANNEL,
				//GPIO INTR TYPE, as an example, we enable fade_end interrupt here.
				.intr_type = LEDC_INTR_DISABLE,
				//set LEDC timer source, if different channel use one timer,
				//the frequency and bit_num of these channels should be the same
				.timer_sel = (ledc_timer_t)LEDC_BEEPER_TIMER,
				//set the duty for initialization.(duty range is 0 ~ ((2**bit_num)-1)
				.duty = (uint32_t)((1 << ledc_timer.duty_resolution) / 2),
				.hpoint = 0
		};
		//set the configuration
		if(ESP_OK != ledc_channel_config(&ledc_channel)) {
			ESP_LOGE(TAG, "PWM channel config: error\n");
		}else{ // OK
			beeperState = BEEPER_IDLE_STATE;
		}
	}
}

void cBeeper::beeperSet(uint16_t frequency) {

	if(0 == frequency) {
		beeperClr();
	} else if(cur_frequency != frequency) {
		cur_frequency = frequency;
		ledc_timer = {
				//.speed_mode =
				LEDC_BEEPER_MODE,		//timer mode,
				//.duty_resolution =
				LEDC_BEEPER_TIMER_BITS,	//set timer counter bit number
				//.timer_num =
				LEDC_BEEPER_TIMER,		//timer index
				//.freq_hz =
				frequency,				//set frequency of pwm
		};
		//configure timer for high speed channels
		if(ESP_OK != ledc_timer_config(&ledc_timer)) {
			ESP_LOGE(TAG, "PWM channel config error");
		} else {
			ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, (1 << ledc_timer.duty_resolution) / 2);
			ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
		}
	}
}

void cBeeper::beeperClr() {
	cur_frequency = 0;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
	ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
	ledc_stop(LEDC_HIGH_SPEED_MODE, (ledc_channel_t)LEDC_BEEPER_CHANNEL, 0);
}

void cBeeper::Play(beeperTune_t* tune) {
	Start(); // initialize PWM periphery if it was not init yet

	if((beeperTune != tune) || (tune != nullptr && 0 == tune->options.cyclic)) {
		if (BEEPER_BUSY_STATE == beeperState) {
			TaskDelete();
			beeperTune = nullptr; // only after TaskDelete()
			beeperClr();
			beeperState = BEEPER_IDLE_STATE;
			App.sens_3v3_off.off();
		}

		if (nullptr != tune) {
			beeperIndex = 0x00;
			beeperTune = tune;
			if (0x00 != tune->notesAmount)  {
				beeperState = BEEPER_BUSY_STATE;
				App.sens_3v3_off.on();	//Power On
				TaskCreate("Beeper task", 10, 8192);
			}
		}
	}
}

void cBeeper::TaskHandler() {

	while(nullptr != beeperTune)	{
		if (beeperIndex < beeperTune->notesAmount)  {
			//Process current tune note
			beeperSet(beeperTune->notes[beeperIndex].frequency);
			while(1) {
				uint32_t curt = cBaseTask::GetTickCount();
				if(curt >= last_time + beeperTune->notes[beeperIndex].duration){
					last_time = curt;
					break;
				}
				vTaskDelay(1); // give a chance for lower priority tasks!
			}			
			beeperIndex++;
		} else  {
			if((beeperIndex < (beeperTune->notesAmount + 1)) &&	//If pause is not played yet
					(0 != beeperTune->pause)) {                   	//and pause must be played
				beeperClr();

				beeperIndex++;
				while(1) {
					uint32_t curt = cBaseTask::GetTickCount();
					if(curt >= last_time + beeperTune->pause){
						last_time = curt;
						break;
					}
					vTaskDelay(1);
				}

			} else  {
				if (0x01 == beeperTune->options.cyclic) {
					//Process current tune note
					beeperSet(beeperTune->notes[0].frequency);
					beeperIndex = 0x01;
					while(1) {
						uint32_t curt = cBaseTask::GetTickCount();
						if(curt >= last_time + beeperTune->notes[0].duration){
							last_time = curt;
							break;
						}
						vTaskDelay(1);
					}					
				} else  {
					beeperTune = nullptr;
					beeperClr();
				}
			}
		}
	}

	beeperState = BEEPER_IDLE_STATE;
	App.sens_3v3_off.off();
	TaskDeleteSelf();
}
