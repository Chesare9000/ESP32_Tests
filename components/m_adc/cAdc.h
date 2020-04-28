/*
 * cAdc.h
 *
 *  Created on: 12.10.2017 (c) EmSo
 *      Author: D. Pavlenko
 *  Modified: 16.10.2007 N. Lubin
 */

#ifndef COMPONENTS_M_ADC_CADC_H_
#define COMPONENTS_M_ADC_CADC_H_
#include <driver/timer.h>
#include <driver/adc.h>
#include "../../main/common/cBaseTask.h"
#include "freertos/queue.h"

class cAdcAverage{
	adc1_channel_t adcChannel;
	adc_atten_t adcAtten;

public:
	cAdcAverage(const adc_bits_width_t _bw = ADC_WIDTH_12Bit);
	~cAdcAverage();
	//
	int Start(const adc1_channel_t _ch, const adc_atten_t _atten = ADC_ATTEN_11db);
	int Get(void);
private:
	void Init(void);

	static adc_bits_width_t bits_width; // common for all adc channels
	static cSemaphore adcSemaphore;
	static bool bAdcWidthConfigured;
	static bool bWasInit;
	//static portMUX_TYPE rtc_spinlock;
	static xQueueHandle timer_queue;
	static uint16_t sampleCntr;
	static int	value;
	static int	lastValue;
	static timer_config_t timer_config;

	static int adcSwitchChannel(adc1_channel_t channel);
	static void adcStart(void);
	static int adcGet(void);
	static void TaskHandler(void* arg);
	static void timer_isr(void *para);

};

class cAdcChannel{
	adc1_channel_t ch;
	static adc_bits_width_t bits; // common for all adc channels
	static bool bAdcWidthConfigured;
	adc_atten_t atten;
	bool bWasInit; // initialization status
	int SamplesToAvg; // simple averager
public:

	float LastValue; // last value obtained from the ADC, converted to volts
	cAdcChannel(const adc1_channel_t _ch, const adc_atten_t _atten = ADC_ATTEN_0db, const int samplesToAvg = 1);
	// get raw adc result
	int ReadRaw();
	// get current ADC value expressed in volts
	float ReadVoltage();
private:
	void Init();
};

class cAdc:public cBaseTask {
	cAdcChannel *channels[ADC1_CHANNEL_MAX];
public:
	cAdc();
	~cAdc();
	cAdcChannel *Add(const adc1_channel_t _ch, const adc_atten_t _atten = ADC_ATTEN_0db);
	cAdcChannel *operator[](const int ind);
	// start continuous update of all configured channels
	void Start();
	// stop update
	void Stop();
private:
	void TaskHandler();
};

#endif /* COMPONENTS_M_ADC_CADC_H_ */
