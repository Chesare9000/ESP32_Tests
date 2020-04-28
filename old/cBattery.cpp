/*
 * cBattery.h
 *
 *  Created on: 17.10.2017 (c) EmSo
 *      Author: N. Lubin
 */

#include "cBattery.h"
#include "cApplication.h"
#include "../components/m_gpio/cGpio.h"
#include "pindefs.h"
#include <esp_log.h>
#include "NVRAM_Sensors.h"


//charge_stat1(PIN_STAT1)
cBattery::cBattery() : vbatt_adc_on(PIN_VBAT_ADC_ON), charge_stat1(PIN_STAT1), charge_stat2(PIN_STAT2) {

}

cBattery::~cBattery() {

}

void cBattery::Start()
{
	vbatt_adc_on.SetModeOutput(0); // off
	charge_stat1.SetModeInput();
	charge_stat2.SetModeInput();
	charge_stat1.PullUp();
	charge_stat2.PullUp();
	TaskCreate("cBatTask", 6, 2048);
}

void cBattery::TaskHandler(){
	//int cnt(0);
	while(true){
		charge_stat1.Handler();
		charge_stat2.Handler();
		vTaskDelay(5);
		/*
		cnt ++;
		if((cnt % 10) == 0){
			ESP_LOGD("Battery", "stat1 level %d period %d ||| stat2 level %d period %d", charge_stat1(), charge_stat1.StateChangePeriodMs(), charge_stat2(), charge_stat2.StateChangePeriodMs());
		}
		*/
	}
}

cBattery::Status cBattery::GetStatus(bool bAutoMosfetControl)
{
	uint16_t voltage = 0;

	if(!bAutoMosfetControl){
		voltage = get_voltage(false);
	}else{
		voltage = measure(); // updates global values
	}
	 
	uint16_t max_voltage = SensorDataGlobal.ChargerStatusLastMaxBatU;

	struct Status status;
	status.voltage_mv = voltage;
	status.capacity = capacity(voltage, max_voltage);
	status.state = state(voltage, max_voltage);

	return status;
}

bool cBattery::ChargerConnected(void)
{
	return SensorDataGlobal.bPirIsDigital ? ( charge_stat1() != charge_stat2() ) : is_no_bat();
}


eBatteryState cBattery::state(uint16_t voltage, uint16_t max_voltage)
{
	/* AAT3693AA
	ST1		ST2
	FLASH	FLASH		No battery
	LOW		HIGH		Charging
	HIGH	LOW			Charge complete
	HIGH	HIGH		Fault or charger not connected
	*/

	bool is_g2 = SensorDataGlobal.bPirIsDigital;
	bool bChargerConnected = SensorDataGlobal.ChargerConnected;
	bool bCharging = bChargerConnected && !charge_stat1() && charge_stat2();
	bool bCharged = bChargerConnected && charge_stat1() && !charge_stat2();

	// calculate status
	eBatteryState rbs = SensorDataGlobal.BatteryState;

	if(bChargerConnected){
		// charging
		if( (bCharged && is_g2) || (!is_g2 && bChargerConnected && voltage > max_voltage - 50)){
			rbs = e_bat_charging_full;			
		}
		else if(bCharging || bChargerConnected){ // revision 2 board with analog PIR always shows NO BATTERY error state, so we can't use bCharging here
			if(voltage > max_voltage - 400){ // 3800
				rbs = e_bat_charging_med;
			} else {
				rbs = e_bat_charging_low;
			}
		}
	}
	else{
		// discharging
		if(voltage > max_voltage - 300) // 3900
			rbs = e_bat_full;
		else if(voltage > max_voltage - 700 && voltage < max_voltage - 400) // 3500
			rbs = e_bat_med;
		else if(voltage < max_voltage - 900) // 3300
			rbs = e_bat_low;
		else if (rbs > e_bat_low) // it was charging and current state is undefined
			rbs = e_bat_med;
	}

	return rbs;
}

bool cBattery::is_no_bat(void){
	// make sure noBat is properly recognized
	// pins toggle at 1Hz, so 500ms delay is necessary to properly detect changes

	vTaskDelay(500 / portTICK_PERIOD_MS);

	bool toggle = (charge_stat1.StateChangePeriodMs() > 0 && charge_stat2.StateChangePeriodMs() > 0);
	bool f_1hz = (charge_stat1.StateChangePeriodMs() < 1000 && charge_stat2.StateChangePeriodMs() < 1000);

	return toggle && f_1hz;
}


// get battery charge level in % (0...100)
uint8_t cBattery::capacity(uint16_t voltage, uint16_t max_voltage)
{
	max_voltage = max_voltage > 0 ? max_voltage - 100 : 4100;
	int delta = max_voltage - voltage;

	if(delta < 0){
		return 100;
	}

	int res = 100 - 100 * delta / (4200 - 3500);
	if(res < 0){
		res = 0;
	}

	return res;
}

uint16_t cBattery::measure(void)
{
	uint16_t batu_mv = get_voltage();
	uint16_t max_val = 0;
	
	if(SensorDataGlobal.ChargerStatusLastMaxBatU > 0){
		max_val = SensorDataGlobal.ChargerStatusLastMaxBatU;
	}else{
		// initialize global max voltage
		max_val = SensorDataGlobal.ChargerStatusLastMaxBatU = 4000;
	}

	// update global voltage
	SensorDataGlobal.BatteryVoltage = (float)batu_mv / 1000.0f;

	// update maximum voltage
	if(batu_mv > max_val){
		SensorDataGlobal.ChargerStatusLastMaxBatU = batu_mv;
		App.config.MaxUbat(IntToStr(batu_mv)); // and store it
	}

	SensorDataGlobal.ChargerConnected = ChargerConnected();

	return batu_mv;
}

uint16_t cBattery::get_voltage(bool bAutoMosfetControl)
{
	if(bAutoMosfetControl){
		vbatt_adc_on = 1; // turn the divider MOSFET ON
	}
	vTaskDelay(1);

	cAdcChannel adc(PIN_ADC_VBAT_ADC, ADC_ATTEN_0db, 40);
	//	unsigned int result = adc.ReadRaw() * 4650 / 4095;
	unsigned int result = adc.ReadVoltage() * (2.26 + 7.87) / 2.26 * 1000; // result in mV

	if(bAutoMosfetControl){
		vbatt_adc_on = 0;
	}
	return result;
}