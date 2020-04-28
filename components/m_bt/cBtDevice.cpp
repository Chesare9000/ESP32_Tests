/*
 * cBtDevice.cpp
 *
 *  Created on: 11.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "sdkconfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_err.h>
#include <esp_log.h>

#include "cBtDevice.h"
#include "cBtServer.h"



static const char* LOG_TAG = "cBtDevice";

cBtDevice* cBtDevice::pActiveInst(nullptr);

cBtDevice::cBtDevice() : pServer(nullptr) {
	pActiveInst = this;
}

cBtDevice::~cBtDevice() {
	pActiveInst = nullptr;
	Deinit();
}


// BT events handlers
/* Handler for the GATT client events.
 * * `ESP_GATTC_OPEN_EVT` – Invoked when a connection is opened.
 * * `ESP_GATTC_PREP_WRITE_EVT` – Response to write a characteristic.
 * * `ESP_GATTC_READ_CHAR_EVT` – Response to read a characteristic.
 * * `ESP_GATTC_REG_EVT` – Invoked when a GATT client has been registered.
 */
void cBtDevice::gattClientEventHandler(
		esp_gattc_cb_event_t event,
		esp_gatt_if_t gattc_if,
		esp_ble_gattc_cb_param_t *param){

	ESP_LOGD(LOG_TAG, "gattClientEventHandler [esp_gatt_if: %d] ... %d",
			gattc_if, event);

	switch(event) {
	default: {
		break;
	}
	} // switch

}

// Handle GATT server events.
void cBtDevice::gattServerEventHandler(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t *param){
	ESP_LOGD(LOG_TAG, "gattServerEventHandler [esp_gatt_if: %d] ... %d", gatts_if, event);

	if(pServer)
		pServer->handleGATTServerEvent(event, gatts_if, param);

}

// Handle GAP events
void cBtDevice::gapEventHandler(
		esp_gap_ble_cb_event_t event,
		esp_ble_gap_cb_param_t *param){


	switch(event) {
	case ESP_GAP_BLE_SEC_REQ_EVT: {
		esp_err_t errRc = esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
		if (errRc != ESP_OK) {
			ESP_LOGE(LOG_TAG, "esp_ble_gap_security_rsp: rc=%d", errRc);
		}
		break;
	}

	default: {
		break;
	}
	} // switch

	if(pServer)
		pServer->handleGAPEvent(event, param);

}



void cBtDevice::Init(const char *deviceName){
	esp_err_t errRc = ::nvs_flash_init();
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "nvs_flash_init: rc=%d", errRc);
		return;
	}

	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	errRc = esp_bt_controller_init(&bt_cfg);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_bt_controller_init: rc=%d", errRc);
		return;
	}

	errRc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P7);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_tx_power_set: rc=%d", errRc);
		return;
	}

	errRc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_bt_controller_enable: rc=%d", errRc);
		return;
	}

	errRc = esp_bluedroid_init();
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_bluedroid_init: rc=%d", errRc);
		return;
	}

	errRc = esp_bluedroid_enable();
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_bluedroid_enable: rc=%d", errRc);
		return;
	}

	errRc = esp_ble_gap_register_callback(gapEventHandlerStub);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gap_register_callback: rc=%d", errRc);
		return;
	}

	errRc = esp_ble_gattc_register_callback(gattClientEventHandlerStub);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gattc_register_callback: rc=%d", errRc);
		return;
	}

	errRc = esp_ble_gatts_register_callback(gattServerEventHandlerStub);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gatts_register_callback: rc=%d", errRc);
		return;
	}

	errRc = ::esp_ble_gap_set_device_name(deviceName);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gap_set_device_name: rc=%d", errRc);
		return;
	};

	esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
	errRc = ::esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gap_set_security_param: rc=%d", errRc);
		return;
	};

	vTaskDelay(300/portTICK_PERIOD_MS); // Delay for at least 200 msecs as a workaround to some hw issues.

}

void cBtDevice::Deinit(){
	ESP_LOGD(LOG_TAG, ">> cBtDevice::Deinit()");
	esp_bluedroid_disable();
	esp_bluedroid_deinit();
	esp_bt_controller_disable();
	esp_bt_controller_deinit(); // after this call it will be not possible init BT again
	ESP_LOGD(LOG_TAG, "<< cBtDevice::Deinit()");
}

