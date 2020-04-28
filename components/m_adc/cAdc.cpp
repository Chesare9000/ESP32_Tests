/*
 * cAdc.cpp
 *
 *  Created on: 12.10.2017 (c) EmSo
 *      Author: D. Pavlenko
 *  Modified: 16.10.2007 N. Lubin
 */

#include <esp_log.h>
#include "driver/periph_ctrl.h"
#include "driver/gpio.h"

#include "soc/rtc_io_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc_cntl_reg.h"
extern "C" {
#include "soc/timer_group_struct.h"
}

#include "cAdc.h"

// cAdcChannel static memebers
adc_bits_width_t cAdcChannel::bits; // common for all adc channels
bool  cAdcChannel::bAdcWidthConfigured = false;

// cAdcAverage constants
#define TIMER_INTR_SEL	TIMER_INTR_LEVEL	/*!< Timer level interrupt */
#define TIMER_GROUP		TIMER_GROUP_0		/*!< Timer group 0 */
#define TIMER_IDX		TIMER_0				/*!< Timer 0 */
#define TIMER_DIVIDER	16					/*!< Hardware timer clock divider */
#define TIMER_SCALE		(TIMER_BASE_CLK / TIMER_DIVIDER)  /*!< used to calculate counter value */
#define ADC_MAIN_PERIOD 50   				/*!< 1/ADC_MAIN_PERIOD sec */
#define ADC_SAMPLES_PER_PERIOD	32
#define QUEUE_SIZE		100

static const char *TAG = "cAdc";

struct timer_event{
	unsigned cntr;
	int value;
};

adc_bits_width_t cAdcAverage::bits_width = ADC_WIDTH_12Bit; // common for all adc channels
cSemaphore cAdcAverage::adcSemaphore;
bool  cAdcAverage::bAdcWidthConfigured = false;
bool  cAdcAverage::bWasInit = false;
xQueueHandle  cAdcAverage::timer_queue = 0;
uint16_t  cAdcAverage::sampleCntr = 0;
int	 cAdcAverage::value = 0;
int	 cAdcAverage::lastValue = 0;
timer_config_t cAdcAverage::timer_config;

cAdcAverage::cAdcAverage(const adc_bits_width_t _bw)
{
	sampleCntr = 0;
	if(!bAdcWidthConfigured)
		bits_width = _bw;
	bWasInit = false;
}

cAdcAverage::~cAdcAverage() {}

// 
void cAdcAverage::Init()
{
	if(!bAdcWidthConfigured)	{
		if(ESP_OK != adc1_config_width(bits_width))	{
			ESP_LOGE(TAG, "ADC bit width config: error\n");
			return;
		}
		bAdcWidthConfigured = true;
	}

	bWasInit = true;

	timer_queue = xQueueCreate(QUEUE_SIZE, sizeof(timer_event));
	
	xTaskCreate(cAdcAverage::TaskHandler, "timer_evt_task", 2048, NULL, 5 /*| portPRIVILEGE_BIT*/, NULL);
}

int cAdcAverage::Start(const adc1_channel_t _ch, const adc_atten_t _atten)
{
	int result;
	
	adcChannel = _ch;
	adcAtten = _atten;

	if(!bWasInit)
		Init();

	adcSemaphore.Wait();
	adcSemaphore.Lock(200 / portTICK_PERIOD_MS);	//20ms (50Hz filter)

	if(ESP_OK != (result = adc1_config_channel_atten(adcChannel, adcAtten))) {
		ESP_LOGE(TAG, "ADC channel atten config: error\n");
		adcSemaphore.Unlock();
		return result;
	}

	value = ADC_SAMPLES_PER_PERIOD / 2;
	sampleCntr = ADC_SAMPLES_PER_PERIOD;
    /*Start timer counter*/
	adcSwitchChannel(adcChannel);

    timer_config.alarm_en = 1;
    timer_config.auto_reload = 1;
    timer_config.counter_dir = TIMER_COUNT_UP;
    timer_config.divider = TIMER_DIVIDER;
    timer_config.intr_type = TIMER_INTR_SEL;
    timer_config.counter_en = TIMER_PAUSE;
    if(ESP_OK != (result = timer_init(TIMER_GROUP, TIMER_IDX, &timer_config))) {	//Configure timer
		ESP_LOGE(TAG, "Timer init: error\n");
		adcSemaphore.Unlock();
		return result;
    }
    timer_pause(TIMER_GROUP, TIMER_IDX);			//Stop timer counter
    timer_set_counter_value(TIMER_GROUP, TIMER_IDX, 0x00000000ULL);		//Load counter value
    timer_set_alarm_value(TIMER_GROUP, TIMER_IDX, TIMER_SCALE / ADC_SAMPLES_PER_PERIOD / ADC_MAIN_PERIOD);	//Set alarm value
    timer_enable_intr(TIMER_GROUP, TIMER_IDX);		//Enable timer interrupt
    timer_isr_register(TIMER_GROUP, TIMER_IDX, timer_isr, (void*) TIMER_IDX, ESP_INTR_FLAG_IRAM, NULL);		//Set ISR handler
    timer_start(TIMER_GROUP, TIMER_IDX);

    adcStart();		//start first measurement
	
	return ESP_OK;
}

int cAdcAverage::Get(void)
{
	adcSemaphore.Wait();
	return lastValue;
}

void cAdcAverage::TaskHandler(void* arg)
{
    while(1) {
        timer_event evt;
        if(pdTRUE == xQueueReceive(timer_queue, &evt, 1))	{
			value += evt.value;
			if(0 == evt.cntr)	{
				lastValue = value / ADC_SAMPLES_PER_PERIOD;
				adcSemaphore.Unlock();
			}
		}
	}
}

int cAdcAverage::adcSwitchChannel(adc1_channel_t channel)
{
	if(!(channel < ADC1_CHANNEL_MAX))
		return ESP_ERR_INVALID_ARG;
	
	//portENTER_CRITICAL(&rtc_spinlock);
	//Adc Controler is Rtc module,not ulp coprocessor
	SET_PERI_REG_BITS(SENS_SAR_MEAS_START1_REG, 1, 1, SENS_MEAS1_START_FORCE_S); //force pad mux and force start
	//Bit1=0:Fsm  Bit1=1(Bit0=0:PownDown Bit10=1:Powerup)
	SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 0, SENS_FORCE_XPD_SAR_S); //force XPD_SAR=0, use XPD_FSM
	//Disable Amp Bit1=0:Fsm  Bit1=1(Bit0=0:PownDown Bit10=1:Powerup)
	SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_AMP, 0x2, SENS_FORCE_XPD_AMP_S); //force XPD_AMP=0
	//Open the ADC1 Data port Not ulp coprocessor
	SET_PERI_REG_BITS(SENS_SAR_MEAS_START1_REG, 1, 1, SENS_SAR1_EN_PAD_FORCE_S); //open the ADC1 data port
	//Select channel
	SET_PERI_REG_BITS(SENS_SAR_MEAS_START1_REG, SENS_SAR1_EN_PAD, (1 << channel), SENS_SAR1_EN_PAD_S); //pad enable
	SET_PERI_REG_BITS(SENS_SAR_MEAS_CTRL_REG, 0xfff, 0x0, SENS_AMP_RST_FB_FSM_S);  //[11:8]:short ref ground, [7:4]:short ref, [3:0]:rst fb
	SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT1_REG, SENS_SAR_AMP_WAIT1, 0x1, SENS_SAR_AMP_WAIT1_S);
	SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT1_REG, SENS_SAR_AMP_WAIT2, 0x1, SENS_SAR_AMP_WAIT2_S);
	SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_SAR_AMP_WAIT3, 0x1, SENS_SAR_AMP_WAIT3_S);

	while (GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR1_REG, 0x7, SENS_MEAS_STATUS_S) != 0); //wait det_fsm==0

	return ESP_OK;
}

void IRAM_ATTR cAdcAverage::adcStart(void)
{
	SET_PERI_REG_BITS(SENS_SAR_MEAS_START1_REG, 1, 0, SENS_MEAS1_START_SAR_S); //start force 0
	SET_PERI_REG_BITS(SENS_SAR_MEAS_START1_REG, 1, 1, SENS_MEAS1_START_SAR_S); //start force 1
}

int IRAM_ATTR cAdcAverage::adcGet(void)
{
    while (GET_PERI_REG_MASK(SENS_SAR_MEAS_START1_REG, SENS_MEAS1_DONE_SAR) == 0) {}; //read done
    return (GET_PERI_REG_BITS2(SENS_SAR_MEAS_START1_REG, SENS_MEAS1_DATA_SAR, SENS_MEAS1_DATA_SAR_S));
}

void IRAM_ATTR cAdcAverage::timer_isr(void *para)
{
    int timer_idx = (int) para;
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    timer_event evt;
    if((intr_status & BIT(timer_idx)) && timer_idx == TIMER_IDX) {
        //ets_printf("T%u\n", sampleCntr);
		sampleCntr--;
		evt.cntr = sampleCntr;
		evt.value = adcGet();
		xQueueSendFromISR(timer_queue, &evt, NULL);
		if(sampleCntr)	{
			/*Timer will reload counter value*/
			TIMERG0.hw_timer[timer_idx].update = 1;
			/*We don't call a API here because they are not declared with IRAM_ATTR*/
			TIMERG0.int_clr_timers.t0 = 1;
			/*Post an event to out example task*/
			adcStart();
			/*For a auto-reload timer, we still need to set alarm_en bit if we want to enable alarm again.*/
			TIMERG0.hw_timer[timer_idx].config.alarm_en = 1;
		} else {
			/*Timer will reload counter value*/
			TIMERG0.hw_timer[timer_idx].update = 0;
			/*We don't call a API here because they are not declared with IRAM_ATTR*/
			TIMERG0.int_clr_timers.t0 = 1;
			/*For a auto-reload timer, we still need to clr alarm_en bit if we want to disable alarm again.*/
			TIMERG0.hw_timer[timer_idx].config.alarm_en = 0;
			TIMERG0.hw_timer[timer_idx].config.enable = 0;
		}
    }
}

//-----------------------------------------------------------------------------
cAdcChannel::cAdcChannel(const adc1_channel_t _ch, const adc_atten_t _atten, const int samplesToAvg){
	ch = _ch;
	bits = ADC_WIDTH_12Bit;
	atten = _atten;
	bWasInit = false;
	LastValue = 0;
	SamplesToAvg = samplesToAvg;
	if(SamplesToAvg < 1)
		SamplesToAvg = 1;
	else if(SamplesToAvg > 100)
		SamplesToAvg = 100;

}

int cAdcChannel::ReadRaw(){
	if(!bWasInit)
		Init();
	int res(0);
	for(int i = 0; i < SamplesToAvg; i++){
		res += adc1_get_raw(ch);
		if(i > 0 && (i % 10) == 0)
			vTaskDelay(1);
	}
	res /= SamplesToAvg;
	if(res < 0)
		ESP_LOGE(TAG, "ADC read failed, ch %d", ch);
	return res;
}

float cAdcChannel::ReadVoltage(){
	int raw = ReadRaw();
	if(raw < 0) // error
		return LastValue;

	float mul;
	switch(atten){
	case ADC_ATTEN_0db:
		mul = 1;
		break;
	case ADC_ATTEN_2_5db:
		mul = 1.34;
		break;
	case ADC_ATTEN_6db:
		mul = 2;
		break;
	case ADC_ATTEN_11db:
		mul = 3.6;
		break;
	default:
		mul = 1;
	}

	switch(bits){
	case ADC_WIDTH_9Bit:
		mul *= 1.0/511.0;
		break;
	case ADC_WIDTH_10Bit:
		mul *= 1.0/1023.0;
		break;
	case ADC_WIDTH_11Bit:
		mul *= 1.0/2047.0;
		break;
	default: // ADC_WIDTH_12Bit:
		mul *= 1.0/4095.0;
	}

	LastValue = 1.1 * mul * raw; // VRef is 1100 mV (but may vary from 1000 to 1200 mV for different devices)
	return LastValue;
}


void cAdcChannel::Init(){
	if(!bAdcWidthConfigured){
		bAdcWidthConfigured = adc1_config_width(bits) == ESP_OK;
	}
	bool bOk = bAdcWidthConfigured;
	if(bOk)
		bOk = adc1_config_channel_atten(ch, atten) == ESP_OK;

	if(!bOk)
		ESP_LOGE(TAG, "ADC Init() failed, ch %d, atten %d", ch, atten);
	bWasInit = bOk;
}

// =======================================
cAdc::cAdc() {
	for(int i = 0; i < ADC1_CHANNEL_MAX; i++)
		channels[i] = nullptr;
}

cAdc::~cAdc() {
	Stop();
	// delete all channels
	for(int i = 0; i < ADC1_CHANNEL_MAX; i++){
		if(channels[i])
			delete channels[i];
	}
}

cAdcChannel* cAdc::Add(const adc1_channel_t _ch, const adc_atten_t _atten){
	if(_ch >= ADC1_CHANNEL_MAX)
		return nullptr;
	if(channels[_ch])
		delete channels[_ch];
	channels[_ch] = new cAdcChannel(_ch, _atten);
	return channels[_ch];
}

cAdcChannel* cAdc::operator[](const int ind){
	if(ind >= ADC1_CHANNEL_MAX)
		return nullptr;
	return channels[ind];
}

// start continuous update of all configured channels
void cAdc::Start(){
	TaskCreate("cAdc", 3, 4096);
}

// stop update
void cAdc::Stop(){
	TaskDelete();
}

void cAdc::TaskHandler(){
	int curch(0);
	while(true){
		// get next not null channel
		if(curch >= ADC1_CHANNEL_MAX)
			curch = 0;
		cAdcChannel *pch(nullptr);
		for(; curch < ADC1_CHANNEL_MAX; curch++){
			pch = channels[curch];
			if(pch){
				break;
			}
		}

		if(pch != nullptr){ // update last channel voltage value
			pch->ReadVoltage();
			curch ++; // go to the next channel
		}

		vTaskDelay(10); // wait
	}
}

