/*
 * cBluetooth.cpp
 *
 *  Created on: 8.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "cBluetooth.h"
#include <esp_log.h>
#include "cApplication.h"
#include "cFactoryTest.h"
#include <stdlib.h>
#include "../../main/common/cBaseTask.h"

static const char* LOG_TAG = "cBluetooth";

// BT characteristics callbacks classes declarations and objects definitions
// number at the and corresponds to the characteristic UID


// DEVICE_STATUS
class cBtCallbacks04: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.config.AssignStatus());
	}
}btcb_04;

// OPERATION_MODE
class cBtCallbacks05: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.gsCurrentState == e_home ? "home" : App.gsCurrentState == e_guard ? "guard" : App.gsCurrentState == e_alarm ? "alarm" : App.gsCurrentState == e_motest ? "motest" : "unknown");
	}
	void AftereWrite(cBtCharacteristic *pCaller){
		auto smode = pCaller->getValue();
		auto &ts = App.gsTargetState;
		if(smode == "home")
			ts = e_home;
		else if(smode == "guard")
			ts = e_guard;
		else if(smode == "alarm")
			ts = e_alarm;
		else if(smode == "motest")
			ts = e_motest;
	}
}btcb_05;

// ALARM_STATE
class cBtCallbacks06: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.gsCurrentState != e_alarm ? "off" : SensorDataGlobal.bPIR && SensorDataGlobal.bSmoke ? "motion,smoke":
				SensorDataGlobal.bPIR ? "motion" : SensorDataGlobal.bSmoke ? "smoke" : "on");
	}
}btcb_06;

// ERROR_CODE
class cBtCallbacks07: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.errors.ErrCount() ? App.errors.ToJsonString() : "no errors");
	}
}btcb_07;

// WLAN_STATE
class cBtCallbacks08: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		std::string status = App.config.WiFiStatus();
		pCaller->setValue(App.wifiCurrentState == eAppWiFiState::e_active ? "connected" :
				status == "ok" ? "disconnected" : status );
	}
}btcb_08;


// BATTERY_STATUS
class cBtCallbacks09: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		std::string res;
		switch(SensorDataGlobal.BatteryState){
		case e_bat_full:
			res = "full";
			break;
		case e_bat_med:
			res = "med";
			break;
		case e_bat_low:
			res = "low";
			break;
		case e_bat_charging_full:
			res = "charging full";
			break;
		case e_bat_charging_med:
			res = "charging med";
			break;
		case e_bat_charging_low:
			res = "charging low";
			break;
		case e_bat_charging_aborted:
			res = "charging aborted";
			break;
		case e_bat_error:
			res = "battery error";
			break;
		}
		pCaller->setValue(res);
	}
}btcb_09;

// WLAN_SSID
class cBtCallbacks0a: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.config.WiFiSSID());
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		App.config.WiFiSSID(pCaller->getValue());
	}
}btcb_0a;

// WLAN_PWD
class cBtCallbacks0b: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		App.config.WiFiPass(pCaller->getValue());
	}
}btcb_0b;

// M_UUID
class cBtCallbacks0d: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.config.UsrUUID());
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		App.config.UsrUUID(pCaller->getValue());
	}
}btcb_0d;

// APPLY_NOW
class cBtCallbacks0e: public cBtCharCallbacks{
public:
	void AftereWrite(cBtCharacteristic *pCaller){
		App.WiFiTryAssign();
	}
}btcb_0e;

// PIR_TRESH_LOW
class cBtCallbacks0f: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(IntToStr(WakeStubData.thresholdPIRLow));
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		std::string val = pCaller->getValue();
		// store to NVS
		App.config.PirThrLow(val);
		// update NVRAM
		WakeStubData.thresholdPIRLow = atoi(val.c_str());
	}
}btcb_0f;

// PIR_TRESH_HIGH
class cBtCallbacks10: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(IntToStr(WakeStubData.thresholdPIRHi));
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		std::string val = pCaller->getValue();
		// store to NVS
		App.config.PirThrHigh(val);
		// update NVRAM
		WakeStubData.thresholdPIRHi = atoi(val.c_str());
	}
}btcb_10;

// callbacks for PIR calibration
// create calibrator
class cBtCallbacks11: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? "inst exists" : "no inst");
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		if(!App.pPirCalibrator)
			App.pPirCalibrator = new cPirCalibrator;
	}
}btcb_11;

// delete calibrator instance
class cBtCallbacks12: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? "inst exists" : "no inst");
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		if(App.pPirCalibrator){
			delete App.pPirCalibrator;
			App.pPirCalibrator = nullptr;
			// also we have to destroy BT, Reza: Turn off BT and go to main after CALIBRATOR_DESTROY
			App.btTargetState = eAppBtState::e_disabled;
		}
	}
}btcb_12;

// Mode Get or Set
//// ret is one of: no_motion, human_slow_motion, human_motion, animal_motion
//std::string GetMode();
//// set current thresholds calibration mode, arg should be one values enumerated for GetMode() return
//// call it prior to measurements
//void SetMode(const std::string &mode);
class cBtCallbacks13: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? App.pPirCalibrator->GetMode() : "no inst");
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		std::string val = pCaller->getValue();
		if(App.pPirCalibrator)
			App.pPirCalibrator->SetMode(val);
	}
}btcb_13;

//// ret is one of: start, observe, finished
//std::string GetStep();
class cBtCallbacks14: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? App.pPirCalibrator->GetStep() : "no inst");
	}
}btcb_14;

//// ret is string representation of numbers from 0 to 4095
//std::string GetHighThr();
class cBtCallbacks15: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? App.pPirCalibrator->GetHighThr() : "no inst");
	}
}btcb_15;

//std::string GetLowThr();
class cBtCallbacks16: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? App.pPirCalibrator->GetLowThr() : "no inst");
	}
}btcb_16;


//// drop the result for the current mode without further measurements
//void ResetModeResult();
class cBtCallbacks17: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? "reset" : "no inst");
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		if(App.pPirCalibrator)
			App.pPirCalibrator->ResetModeResult();
	}
}btcb_17;

//// Start (or restart) the measurement process and set current thresholds detection mode, reset will be done automatically
//void NewMeasure ();
class cBtCallbacks18: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? "measure" : "no inst");
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		if(App.pPirCalibrator)
			App.pPirCalibrator->NewMeasure();
	}
}btcb_18;

//// recalculate thresholds, apply and save the results of the last measurements to the NVS
//void StoreMeas();

class cBtCallbacks19: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(App.pPirCalibrator ? "store" : "no inst");
	}

	void AftereWrite(cBtCharacteristic *pCaller){
		if(App.pPirCalibrator)
			App.pPirCalibrator->StoreMeas();
	}
}btcb_19;


// destory Bluetooth request
class cBtCallbacks1a: public cBtCharCallbacks{
public:
	void AftereWrite(cBtCharacteristic *pCaller){
		App.btTargetState = eAppBtState::e_disabled;
	}
}btcb_1a;

// read the stored Factory tests report
class cBtCallbacks1b: public cBtCharCallbacks{
public:
	void BeforeRead(cBtCharacteristic *pCaller){
		pCaller->setValue(DescribeStoredResult());
	}
}btcb_1b;

// force factory tests repetition
class cBtCallbacks1c: public cBtCharCallbacks{
public:
	void AftereWrite(cBtCharacteristic *pCaller){

		std::string s = pCaller->getValue();
		int flags = StrToInt(App.config.FactTestRes());

		if(s == "forceall"){
			flags &= ~ALL_TESTS_PASSED;
		}
		else if(s == "skipall"){
			flags |= ALL_TESTS_PASSED;
		}
		else if(s == "ubat"){
			flags &= ~(int)eTestResult::e_UbatAdc;
		}
		else if(s == "temphum"){
			flags &= ~(int)eTestResult::e_TempHum;
		}
		else if(s == "pir"){
			flags &= ~(int)eTestResult::e_Pir;
		}
		else if(s == "air"){
			flags &= ~(int)eTestResult::e_Air;
		}
		else if(s == "spk"){
			flags &= ~(int)eTestResult::e_Speaker;
			flags &= ~(int)eTestResult::e_Mic;
		}
		else if(s == "mic"){
			flags &= ~(int)eTestResult::e_Mic;
		}
		else if(s == "ble"){
			flags &= ~(int)eTestResult::e_Bluetooth;
		}
		else if(s == "wifi"){
			flags &= ~(int)eTestResult::e_WiFi;
		}
		else{
			// do nothing
			return;
		}

		// store new flags
		App.config.FactTestRes(IntToStr(flags));
		// reboot the device
		cBaseTask::Reboot();
	}
}btcb_1c;

//===================================

cBluetooth::cBluetooth() {
	bWasDisconnect = false;
}

cBluetooth::~cBluetooth() {
	TS_PRINT("Bluetooth is destroyed");
	if(calibration_ongoing()){
		cancel_calibration();
	}
}

bool cBluetooth::IsTimeToDestroy(){
	if(client_was_connected()){

		bool cal_active = calibration_ongoing();

		if(cal_active && client_inactivity_timeout(BT_CLIENT_CALIBRATION_TIMEOUT_MINUTES)){
			cancel_calibration();
			cal_active = false;
		}

		if(!cal_active && client_inactivity_timeout(BT_CLIENT_INACTIVITY_TIMEOUT_MINUTES)){
			return true;
		}
		
		if(!IsClientConnected()){
			bWasDisconnect = true;
		}
	}else{
		// no client connection happened
		if(server_existance_timeout(BT_SERVER_EXISTANCE_TIMEOUT_MINUTES)){
			// nothing has happened since server was created
			return true;
		}
	}
	return false;
}

inline void cBluetooth::cancel_calibration(void)
{
	delete App.pPirCalibrator;
	App.pPirCalibrator = nullptr;
}

inline bool cBluetooth::calibration_ongoing(void){
	return (App.pPirCalibrator != nullptr);
}


#define PRW cBtCharacteristic::PROPERTY_BROADCAST | cBtCharacteristic::PROPERTY_READ | cBtCharacteristic::PROPERTY_WRITE
#define PRO cBtCharacteristic::PROPERTY_BROADCAST | cBtCharacteristic::PROPERTY_READ
#define PWO cBtCharacteristic::PROPERTY_BROADCAST | cBtCharacteristic::PROPERTY_WRITE

void cBluetooth::Init(){

	ESP_LOGD(LOG_TAG, ">> Init()");
	TS_PRINT("Bluetooth is activated");
	// configure BT
	BLEUUID svcuuid = uint16_t(0xafff);

	//btDev.Init("LivyProtectSmartRing");
	btDev.Init("SmartRing");
	//btDev.Init("HUM_SMART_ALERT");
	auto padv = btSrv.getAdvertising();
	padv->addServiceUUID(svcuuid);

	BLEAdvertisementData advData;
	advData.setManufacturerData(GetDeviceUID());
	advData.setPartialServices(svcuuid);
	advData.setName("SmartRing");
	advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
	advData.setAppearance(0);
	//padv->setScanResponseData(advData);
	padv->setAdvertisementData(advData);

	// add the Service to the server
	auto psvc = btSrv.ServiceCreate(svcuuid);

	// add characteristics here
	// DEV_UUID Unique ID of the device RO string: �123456789�
	auto pchar = psvc->CharCreate(0xaf01u);
	pchar->setProperties(PRO);
	pchar->setValue(GetDeviceUID());

	// FW_VERSION Version of the firmware
	pchar = psvc->CharCreate(0xaf02u);
	pchar->setProperties(PRO);
	pchar->setValue(CURRENT_FIRMWARE_VERSION);

	// HW_REVISION Revision number of the hardware
	pchar = psvc->CharCreate(0xaf03u);
	pchar->setProperties(PRO);
	pchar->setValue(App.GetHwVersion());

	// DEVICE_STATUS  A status code that represent the current status
	/* RO string:
	�not assigned�
	�assigned�
	�assign failed�
	�factory reset�
	 */
	pchar = psvc->CharCreate(0xaf04u);
	pchar->setProperties(PRO);
	//pchar->setValue("DEVICE_STATUS");
	pchar->setCallbacks(&btcb_04);

	// OPERATION_MODE The operation code which represent the current mode of device
	/* RW string:
	�home�
	�guard�
	�alarm�
	�motest�
	 */
	pchar = psvc->CharCreate(0xaf05u);
	pchar->setProperties(PRW);
	//pchar->setValue("OPERATION_MODE");
	pchar->setCallbacks(&btcb_05);

	// ALARM_STATE Code that identifies whether the alarm is on or off and related sensor (smoke, motion ...)
	/*RO string:
	�off�
	�smoke�
	�motion�
	�motion,smoke�
	 */
	pchar = psvc->CharCreate(0xaf06u);
	pchar->setProperties(PRO);
	//pchar->setValue("ALARM_STATE");
	pchar->setCallbacks(&btcb_06);

	// ERROR_CODE If any error occurs an error code should be written in this array with a timestamp
	/* RW string
	TBD!
	 */
	pchar = psvc->CharCreate(0xaf07u);
	pchar->setProperties(PRO);
	//pchar->setValue("ERROR_CODE");
	pchar->setCallbacks(&btcb_07);

	// WLAN_STATE A value that identifies the success of WLAN connection
	/* RO string:
	�connected�
	�disconnected�
	�failed�
	 */
	pchar = psvc->CharCreate(0xaf08u);
	pchar->setProperties(PRO);
	//pchar->setValue("WLAN_STATE");
	pchar->setCallbacks(&btcb_08);

	// BATTERY_STATUS A code that shows whether the device is battery operated
	/* RO string:
	�full�
	�med�
	�low�
	�charging full�
	�charging med�
	�charging low�
	 */
	pchar = psvc->CharCreate(0xaf09u);
	pchar->setProperties(PRO);
	//pchar->setValue("BATTERY_STATUS");
	pchar->setCallbacks(&btcb_09);

	// WLAN_SSID WiFi AP SSID
	pchar = psvc->CharCreate(0xaf0au);
	pchar->setProperties(PRW);
	//pchar->setValue(App.config.WiFiSSID());
	pchar->setCallbacks(&btcb_0a);


	// WLAN_PWD WiFi AP password
	pchar = psvc->CharCreate(0xaf0bu);
	pchar->setProperties(PWO);
	pchar->setCallbacks(&btcb_0b);


	// M_UUID The identity of user adding
	pchar = psvc->CharCreate(0xaf0du);
	pchar->setProperties(PRW);
	//pchar->setValue("M_UUID");
	pchar->setCallbacks(&btcb_0d);

	// APPLY_NOW
	pchar = psvc->CharCreate(0xaf0eu);
	pchar->setProperties(PWO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_0e);

	// PIR_TRESH_LOW
	pchar = psvc->CharCreate(0xaf0fu);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_0f);

	// PIR_TRESH_HIGH
	pchar = psvc->CharCreate(0xaf10u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_10);


	// PIR calibrator interface
	// Create calibrator
	pchar = psvc->CharCreate(0xaf11u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_11);

	//Destroy calibrator
	pchar = psvc->CharCreate(0xaf12u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_12);

	// Set or Get current mode to calibrate
	pchar = psvc->CharCreate(0xaf13u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_13);

	// Get current calibration status (step)
	pchar = psvc->CharCreate(0xaf14u);
	pchar->setProperties(PRO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_14);

	// Get High threshold for selected mode
	pchar = psvc->CharCreate(0xaf15u);
	pchar->setProperties(PRO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_15);

	// Get Low threshold for selected mode
	pchar = psvc->CharCreate(0xaf16u);
	pchar->setProperties(PRO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_16);

	// Reset thresholds for selected mode
	pchar = psvc->CharCreate(0xaf17u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_17);

	// Start or restart measurements scenario for the selected mode
	pchar = psvc->CharCreate(0xaf18u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_18);

	// Request to apply and store results of the last measurements
	pchar = psvc->CharCreate(0xaf19u);
	pchar->setProperties(PRW);
	// add callbacks on change
	pchar->setCallbacks(&btcb_19);

	// Request to destroy Bluetooth now
	pchar = psvc->CharCreate(0xaf1au);
	pchar->setProperties(PWO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_1a);

	// read the factory test report
	pchar = psvc->CharCreate(0xaf1bu);
	pchar->setProperties(PRO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_1b);

	// force factory tests repetition
	pchar = psvc->CharCreate(0xaf1cu);
	pchar->setProperties(PWO);
	// add callbacks on change
	pchar->setCallbacks(&btcb_1c);


	// initialize Bt Server
	btSrv.Init(&btDev);

	// start service requests processing
	btSrv.Start();

	ESP_LOGD(LOG_TAG, "<< Init()");
}

