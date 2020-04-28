/*
 * cGpio.h
 *
 *  Created on: 19.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#ifndef COMPONENTS_M_GPIO_CGPIO_H_
#define COMPONENTS_M_GPIO_CGPIO_H_
#include "../../main/common/cBaseTask.h"
#include <driver/gpio.h>
#include <esp_sleep.h>

class cGpio {
	bool b_mode_out;
	int cur_state; // 1 or 0
	bool bProccessISR; // ISR happened flag
	static int isr_inst_ref_cnt; // ISR service references counter
	uint32_t last_change_t, last_autotoggle_t;
	int autoToggleIntervalMs; // only for output mode, 0 to turn off auto toggling, other value to toggle output
	uint32_t autoToggleDurationMs; // when to stop autotoggling, 0 to infinite
	int autoToggleExitState;
	int curToggleCnt, maxToggleCnt; // limit counters for autotoggle mode
	bool bRtcPin; // true for RTC mode
	static cGpio *Instances[GPIO_NUM_MAX]; // contains current instances pointers array
	int period_ms; // toggling period for input mode

	// timestamp until when output is forced off (no matter if auto-toggle is currently set)
	uint32_t force_off_until;
public:
	gpio_num_t pin; // use it as read-only, please
	bool b_config_status; // false if not configured or it was an error

	cGpio(const gpio_num_t gpio_pin, const bool bRtc = false);
	~cGpio();

	// auto-toggler configurator, don't forget to call Handler() somewhere; AutoToggleExitState = -1 means ignore the exit state
	// SetAutoToggle(250) to infinite blinking with 2Hz;
	// SetAutoToggle(250, 1, 60) - blinking 2Hz during 60 seconds, after 60 sec set HIGH output level
	// SetAutoToggle(1000 * 30, 1, 30) - set LOW output level and hold it during 30 sec, after 30 sec set output to HIGH
	// ToggleCount - if > 0 then limit toggles (blinks for LED) count to this number
	void SetAutoToggle(int AutoToggleIntervalMs=250, int AutoToggleExitState = -1, int AutoToggleDurtionSec = 0, int ToggleCount = 0);

	// force output off (HIGH) without interfering with auto-toggling (or anything else)
	// this is used to ensure only one LED is active at any given time
	void forceOff(uint32_t secs);
	// clears any ongoing forceOff and restores previous output level
	void clearForceOff(void);
	// used to inject forceOff setting into =Operator and to set
	// IO level without disrupting auto-toggling
	bool setIoLevel(int level);

	// read the state
	int operator()();
	// set the state
	int operator = (const int newstate);
	// how long we are in the last state, use only together with Handler() calls
	uint32_t StateNoChangeMs();
	// for inputs only, no toggles if <= 0
	int StateChangePeriodMs();
	// process interrupts
	void EnableInterrupt(gpio_int_type_t mode = GPIO_INTR_HIGH_LEVEL);
	void DisableInterrupt();
	// allow wakeup by this pin
	void EnableWakeup(bool by_high_level = true);
	void DisableWakeup();
	// DeepSleep Wakeup
	void Ext1WakeupInit(esp_sleep_ext1_wakeup_mode_t mode = ESP_EXT1_WAKEUP_ALL_LOW);

	// set pin mode
	void SetModeOutput(const int output_state = 0);
	void SetModeInput();

	// fast versions of getter and setter combined  with minimal automatic pin direction reconfiguration
	void SetOutputFast(const int output_state);
	int ReadInputFast();
	// pull control
	void PullUp();
	void PullDown();
	// disable pull
	void PullOff();

	// RTC-only stuff
	void RtcHold(); // do it before going to sleep
	void RtcUnhold(); // allow to change pin's state
	// set hold to all configured RTC pins
	static void RtcHoldAll();
	// check this pin kind
	bool IsRtcPin()const{return bRtcPin;}

	void Deinit();

	// main task handler, call it frequently enough to process all required events
	void Handler();
protected:
	// overload this method in descendants. it should be called NOT from the ISR
	virtual void InterruptHandler(){}

private:
	// real interrupt handler
	static void IRAM_ATTR isr_handler_stub(void *arg){
		cGpio *pthis = (cGpio*) arg;
		if(pthis)
			pthis->bProccessISR = true;
	}

};

#endif /* COMPONENTS_M_GPIO_CGPIO_H_ */
