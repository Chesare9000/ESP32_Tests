/*
 * cBeeper.h
 *
 *  Created on: 18.10.2017 (c) EmSo
 *      Author: N. Lubin
 */

// The beeper Driver

#ifndef MAIN_BEEPER_H_
#define MAIN_BEEPER_H_
#include "common/cBaseTask.h"
#include "driver/ledc.h"

#define BEEPER_PWM_FREQUENCY		2700

struct beeperNote_t
{
  uint16_t	frequency;    //Platform depended
  uint16_t	duration;     //ms
};

struct beeperTuneOptions_t
{
  uint8_t cyclic          : 1;                    //Cyclic flag (1 - cyclick)
  uint8_t reserved        : 7;                    //Reserved
};

struct beeperTune_t
{
  const beeperTuneOptions_t	options;
  const uint16_t			pause;
  const uint8_t				notesAmount;
  const beeperNote_t		*notes;
};

class cBeeper : public cBaseTask {
public:
	
	cBeeper();
	~cBeeper();
	
	void Start();
	void beeperSet(uint16_t frequency);
	void beeperClr();
	void Play(beeperTune_t* tune);

private:
	void TaskHandler();

	uint8_t	beeperIndex;
	beeperTune_t* beeperTune;
	uint16_t cur_frequency;
	int sens_3v3_state;
	ledc_channel_config_t ledc_channel;
	ledc_timer_config_t ledc_timer;
	uint32_t last_time;
	static int beeperState;
};

extern beeperTune_t testTune;
extern beeperTune_t startTune;
extern beeperTune_t alarmTune;
extern beeperTune_t alarmTuneShort;

#endif /* MAIN_BEEPER_H_ */
