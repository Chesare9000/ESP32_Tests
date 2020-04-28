/*
 * cGpio.cpp
 *
 *  Created on: 19.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "cGpio.h"
#include <driver/rtc_io.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>

static const char *TAG = "cGpio";

// static members
int cGpio::isr_inst_ref_cnt = 0;
cGpio* cGpio::Instances[GPIO_NUM_MAX]={nullptr}; // contains current instances pointers array


cGpio::cGpio(const gpio_num_t gpio_pin, const bool bRtc) {
	if(gpio_pin >= GPIO_NUM_MAX) // ???
		return;
	pin = gpio_pin;
	autoToggleIntervalMs = 0;
	autoToggleDurationMs= 0;
	autoToggleExitState = 0;
	cur_state = 0;
	last_change_t = 0;
	period_ms = 0;
	last_autotoggle_t = 0;
	bProccessISR = false;
	b_config_status = false;
	b_mode_out = false;
	bRtcPin = bRtc;

	force_off_until = 0;

	if(bRtcPin && !rtc_gpio_is_valid_gpio(pin)){
		ESP_LOGE(TAG, "Pin %d is not a valid RTC!", pin);
		bRtcPin = false;
	}

	Instances[pin] = this;
}

cGpio::~cGpio() {
	Instances[pin] = nullptr;
}

void cGpio::SetAutoToggle(int AutoToggleIntervalMs, int AutoToggleExitState, int AutoToggleDurtionSec, int ToggleCount){
	if(AutoToggleExitState < 0){ // exit state is not explicitly defined
		if(autoToggleIntervalMs == 0){
			// store current state as exit state if no autotoggling currently is active
			autoToggleExitState = cur_state;
		}
	}else{
		autoToggleExitState = AutoToggleExitState;
		// set output state opposite to the Exit state
		*this = AutoToggleDurtionSec ? !autoToggleExitState : autoToggleExitState;
	}
	maxToggleCnt = ToggleCount * 2;
	if(autoToggleIntervalMs == 0){
		// reset toggles counter only when no autotoggling currently is active
		curToggleCnt = 0;
		last_autotoggle_t = cBaseTask::GetTickCount();
	}
	autoToggleIntervalMs = AutoToggleIntervalMs;
	autoToggleDurationMs = AutoToggleDurtionSec == 0 ? 0 : cBaseTask::GetTickCount() + AutoToggleDurtionSec * 1000;
}

// force off for secs amount of seconds (regardless of other settings/autotoggling)
void cGpio::forceOff(uint32_t secs){
	if(b_mode_out){
		uint32_t ms = secs*1000;
		uint32_t curr_time = cBaseTask::GetTickCount();

		if(curr_time + ms > force_off_until){
			force_off_until = curr_time + ms;
			setIoLevel(1);
		}
	}
}

// clear any ongoing forceOff
void cGpio::clearForceOff(void){
	if(b_mode_out && force_off_until){
		force_off_until = 0;
		setIoLevel(cur_state); // restore previous value
	}
}

// enforce forceOff setting without interfering with autotoggling
bool cGpio::setIoLevel(int level){
	if(b_mode_out){
		if(force_off_until && cBaseTask::GetTickCount() < force_off_until){
			level = 1;
		}
		return (bRtcPin ? rtc_gpio_set_level(pin, level) : gpio_set_level(pin, level)) == ESP_OK;
	}
	return false;
}

// read the state
int cGpio::operator()(){
	if(b_mode_out)
		return cur_state;
	int new_state = bRtcPin ? rtc_gpio_get_level(pin) : gpio_get_level(pin);
	if(cur_state != new_state){
		cur_state = new_state;
		uint32_t cur_t = cBaseTask::GetTickCount();
		period_ms = (period_ms + cur_t - last_change_t) / 2;
		last_change_t = cur_t;
	}
	return cur_state;
}

// set the state
int cGpio::operator = (const int newstate){
	if(b_mode_out){ // we may change output only for OUTPUT pin
		if(cur_state != newstate){
			b_config_status = setIoLevel(newstate);
			if(b_config_status){
				cur_state = newstate;
				last_change_t = cBaseTask::GetTickCount();
			}
		}
	}
	return cur_state;
}


// how long we are in the last state
uint32_t cGpio::StateNoChangeMs(){
	return cBaseTask::GetTickCount() - last_change_t;
}

int cGpio::StateChangePeriodMs(){
	int res = StateNoChangeMs();
	if(res > 2000)
		return -1;
	return period_ms;
}

// process interrupts
void cGpio::EnableInterrupt(gpio_int_type_t mode){
	if(b_mode_out){ // interrupts are only for inputs!
		b_config_status = false;
		return;
	}
	//install gpio isr service only once
	if(!isr_inst_ref_cnt){
		gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED);
	}
	isr_inst_ref_cnt ++;
	b_config_status = gpio_set_intr_type(pin, mode) == ESP_OK;
	if(b_config_status)
		b_config_status = gpio_isr_handler_add(pin, isr_handler_stub, (void*) this) == ESP_OK;
}

void cGpio::DisableInterrupt(){
	if(isr_inst_ref_cnt > 0){
		//remove isr handler for gpio number.
		gpio_isr_handler_remove(pin);
		isr_inst_ref_cnt --;
		if(isr_inst_ref_cnt == 0){
			gpio_uninstall_isr_service();
		}
	}
	b_config_status = gpio_intr_disable(pin) == ESP_OK;
}

// allow wakeup by this pin
void cGpio::EnableWakeup(bool by_high_level){
	if(b_mode_out){ // interrupts are only for inputs!
		b_config_status = false;
		return;
	}
	b_config_status = gpio_wakeup_enable(pin, by_high_level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL) == ESP_OK;
}

void cGpio::DisableWakeup(){
	b_config_status = gpio_wakeup_disable(pin) == ESP_OK;
}

void cGpio::Ext1WakeupInit(esp_sleep_ext1_wakeup_mode_t mode){
	b_config_status = esp_sleep_enable_ext1_wakeup(1ULL << (uint64_t)pin, mode) == ESP_OK;
}

// set pin mode
void cGpio::SetModeOutput(const int output_state){
	if(b_mode_out) // already as output
		return;

	if(rtc_gpio_is_valid_gpio(pin)){
		RtcUnhold();
		rtc_gpio_deinit(pin);
	}

	if(bRtcPin){
		b_config_status = rtc_gpio_init(pin) == ESP_OK;

		if(!b_config_status) // error
			return;
	}

	DisableInterrupt();
	DisableWakeup();
	PullOff();
	b_config_status = (bRtcPin ? rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY) :  gpio_set_direction(pin, GPIO_MODE_OUTPUT)) == ESP_OK;
	if(b_config_status){
		b_mode_out = true;
		bRtcPin ? rtc_gpio_set_level(pin, output_state): gpio_set_level(pin, output_state); // reset to the required level
		cur_state = output_state;
	}
}

void cGpio::SetModeInput(){
	if(rtc_gpio_is_valid_gpio(pin)){
		RtcUnhold();
		rtc_gpio_deinit(pin);
	}

	if(bRtcPin){
		b_config_status = rtc_gpio_init(pin) == ESP_OK;
		if(!b_config_status) // error
			return;
	}

	b_config_status = (bRtcPin ? rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY) :  gpio_set_direction(pin, GPIO_MODE_INPUT)) == ESP_OK;

	if(b_config_status){
		b_mode_out = false;
		cur_state = bRtcPin ? rtc_gpio_get_level(pin) : gpio_get_level(pin);
		last_change_t = cBaseTask::GetTickCount();
	}
}

void cGpio::SetOutputFast(const int output_state){
	// set output value first
	bRtcPin ? rtc_gpio_set_level(pin, output_state): gpio_set_level(pin, output_state); // set to the required level
	cur_state = output_state;
	// than change direction if required
	if(!b_mode_out){
		b_config_status = (bRtcPin ? rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY) :  gpio_set_direction(pin, GPIO_MODE_OUTPUT)) == ESP_OK;
		if(b_config_status)
			b_mode_out = true;
	}
}

int cGpio::ReadInputFast(){
	if(b_mode_out){
		b_config_status = (bRtcPin ? rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY) :  gpio_set_direction(pin, GPIO_MODE_INPUT)) == ESP_OK;
		if(b_config_status)
			b_mode_out = false;
		else
			return -1;
	}

	cur_state = bRtcPin ? rtc_gpio_get_level(pin) : gpio_get_level(pin);
	return cur_state;
}

// pull control
void cGpio::PullUp(){
	if(b_mode_out) // for inputs only
		return;
	PullOff();
	b_config_status = (bRtcPin ? rtc_gpio_pullup_en(pin) : gpio_pullup_en(pin)) == ESP_OK;
}

void cGpio::PullDown(){
	if(b_mode_out) // for inputs only
		return;
	PullOff();
	b_config_status = (bRtcPin ? rtc_gpio_pulldown_en(pin) : gpio_pulldown_en(pin)) == ESP_OK;
}

// disable pulls
void cGpio::PullOff(){
	if(bRtcPin){
		rtc_gpio_pullup_dis(pin);
		rtc_gpio_pulldown_dis(pin);
	}else{
		gpio_pullup_dis(pin);
		gpio_pulldown_dis(pin);
	}
}


// do it before going to sleep
void cGpio::RtcHold(){
	if(bRtcPin) {
		//ESP_LOGI(TAG, "Hold %u", pin)
		rtc_gpio_hold_en(pin);
	}
}

// allow to change pin's state
void cGpio::RtcUnhold(){
	if(rtc_gpio_is_valid_gpio(pin))
		rtc_gpio_hold_dis(pin);
}

// set hold to all configured RTC pins
void cGpio::RtcHoldAll(){
	for(int i = 0; i < GPIO_NUM_MAX ; i++){
		auto pgpio = Instances[i];
		if(!pgpio)
			continue;
		if(pgpio->bRtcPin)
			pgpio->RtcHold();
	}
}


void cGpio::Deinit(){
	if(bRtcPin)	{
		RtcUnhold();
		rtc_gpio_deinit(pin);
		//bRtcPin = false;
	}
}

void cGpio::Handler(){
	// check for ISR
	if(bProccessISR){
		bProccessISR = false;
		InterruptHandler();
	}

	if(b_mode_out){ // output
		uint32_t curt = cBaseTask::GetTickCount();
		if(autoToggleIntervalMs > 0){			
			if(curt >= last_autotoggle_t + autoToggleIntervalMs){
				if(maxToggleCnt <= 0 || curToggleCnt < maxToggleCnt){
					(*this) = !(*this)();
					last_autotoggle_t = curt;
					curToggleCnt ++;
				}
			}
			if(autoToggleDurationMs > 0 && curt > autoToggleDurationMs){
				// stop toggling
				autoToggleIntervalMs = 0;
				if(autoToggleExitState >= 0)
					(*this) = autoToggleExitState;
			}
		}
		if(this->force_off_until && curt > this->force_off_until){
			// forceOff was active but timer has elapsed
			clearForceOff();
		}
	}else{ // input
		// update input to check state changes
		(*this)();
	}
}
