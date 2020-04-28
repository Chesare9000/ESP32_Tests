/*
 * cBtDevice.h
 *
 *  Created on: 11.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#ifndef COMPONENTS_M_BT_CBTDEVICE_H_
#define COMPONENTS_M_BT_CBTDEVICE_H_
#include "sdkconfig.h"
#include <esp_gap_ble_api.h> // ESP32 BLE
#include <esp_gattc_api.h>   // ESP32 BLE
#include <esp_gap_ble_api.h> // ESP32 BLE
#include <esp_gatts_api.h>   // ESP32 BLE

class cBtServer;

class cBtDevice {
	friend class cBtServer;
	static cBtDevice *pActiveInst; // instance of this class (must be only one, because we have only one BT device)
	cBtServer *pServer; // our device will be used as a BT Server
public:
	cBtDevice();
	~cBtDevice();
	// initialize and start BT device
	void Init(const char *deviceName);
	// stop BT device
	void Deinit();

private:
	// handlers
	void gattClientEventHandler(
			esp_gattc_cb_event_t event,
			esp_gatt_if_t gattc_if,
			esp_ble_gattc_cb_param_t *param);
	void gattServerEventHandler(
			esp_gatts_cb_event_t      event,
			esp_gatt_if_t             gatts_if,
			esp_ble_gatts_cb_param_t *param);
	void gapEventHandler(
			esp_gap_ble_cb_event_t event,
			esp_ble_gap_cb_param_t *param);

	// stubs
	static void gattClientEventHandlerStub(
			esp_gattc_cb_event_t event,
			esp_gatt_if_t gattc_if,
			esp_ble_gattc_cb_param_t *param){
		if(pActiveInst) pActiveInst->gattClientEventHandler(event, gattc_if, param);
	}
	static void gattServerEventHandlerStub(
			esp_gatts_cb_event_t      event,
			esp_gatt_if_t             gatts_if,
			esp_ble_gatts_cb_param_t *param){
		if(pActiveInst) pActiveInst->gattServerEventHandler(event, gatts_if, param);
	}
	static void gapEventHandlerStub(
			esp_gap_ble_cb_event_t event,
			esp_ble_gap_cb_param_t *param){
		if(pActiveInst) pActiveInst->gapEventHandler(event, param);
	}
};

#endif /* COMPONENTS_M_BT_CBTDEVICE_H_ */
