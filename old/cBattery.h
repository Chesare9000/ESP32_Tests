/*
 * cBattery.h
 *
 *  Created on: 17.10.2017 (c) EmSo
 *      Author: N. Lubin
 */


#ifndef MAIN_BATTERY_H_
#define MAIN_BATTERY_H_
#include "common/cBaseTask.h"
#include "../components/m_gpio/cGpio.h"
#include "../components/m_adc/cAdc.h"
#include "NVRAM_Sensors.h"

class cBattery:cBaseTask {
public:
	
	struct Status {
		uint16_t voltage_mv;		
		uint8_t capacity;
		eBatteryState state;
	};

	cBattery();
	~cBattery();

	void Start();

	cBattery::Status GetStatus(bool bAutoMosfetControl = true);	// get all info

	bool ChargerConnected(void);

	cGpio vbatt_adc_on;

private:
	cGpio charge_stat1, charge_stat2;

	uint16_t measure(void);			// measure voltage and update globals
	eBatteryState state(uint16_t voltage, uint16_t max_voltage);// derive state from STAT pins and voltage
	uint8_t capacity(uint16_t voltage, uint16_t max_voltage);	// calc capacity from voltage (in percent)

	uint16_t get_voltage(bool bAutoMosfetControl = true);	// read ADC, return voltage
	bool is_no_bat(void);

	void TaskHandler();
};

#endif /* MAIN_BATTERY_H_ */
