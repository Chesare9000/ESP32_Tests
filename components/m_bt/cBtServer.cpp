/*
 * cBtServer.cpp
 *
 *  Created on: 11.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "cBtServer.h"
#include "../../main/smart_alert_defs.h"
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
//#include <gatt_api.h>

static const char* LOG_TAG = "cBtServer";


cBtService* cBtServer::ServiceFind(const BLEUUID &uuid){
	for(auto &svc : services){
		if(svc->uuid == uuid)
			return svc;
	}
	return nullptr;
}

void cBtServer::ServiceAddExisting(cBtService *pSvc){
	if(ServiceFind(pSvc->uuid))
		return;
	services.push_back(pSvc);
}

cBtService* cBtServer::ServiceCreate(const BLEUUID &uuid){
	ESP_LOGD(LOG_TAG, ">> createService - %s", uuid.toString().c_str());
	cBtService *psvc = ServiceFind(uuid);
	if(psvc){
		return psvc;
	}
	psvc = new cBtService;
	psvc->uuid = uuid;
	psvc->pServ = this;
	services.push_back(psvc);

	ESP_LOGD(LOG_TAG, "<< createService");
	return psvc;
}

cBtServer::cBtServer():pDevice(nullptr),
		m_semaphoreRegisterAppEvt("cBtServer::RegisterAppEvt"),
		m_semaphoreCreateEvt("cBtServer::CreateEvt"){
	m_appId            = -1;
	m_gatts_if         = -1;
	m_connectedCount   = 0;
	m_connId           = -1;
	last_client_active_t = 0;
	server_create_t = cBaseTask::GetTickCount();
}

cBtServer::~cBtServer() {
	Deinit();
}


void cBtServer::createApp(uint16_t appId) {
	m_appId = appId;
	registerApp();
}

void cBtServer::Init(cBtDevice *pDev){
	pDevice = pDev;
	pDev->pServer = this;
	createApp(0);
}

void cBtServer::Start(){
	// start all our services
	for(auto &svc : services){
		svc->Start();
	}
}

void cBtServer::Deinit(){
	// delete all services
	for(auto &svc : services){
		delete svc;
	}
	services.clear();
	// HW disable
	if(pDevice) pDevice->pServer = nullptr;
	if(m_gatts_if == (uint16_t)-1) return;
	esp_ble_gatts_app_unregister(m_gatts_if);
	m_gatts_if = -1;
}



// return An advertising object.
BLEAdvertising* cBtServer::getAdvertising() {
	return &m_bleAdvertising;
}

uint16_t cBtServer::getConnId() {
	return m_connId;
}


// return The number of connected clients.
uint32_t cBtServer::getConnectedCount() {
	return m_connectedCount;
}


int cBtServer::getGattsIf() {
	return m_gatts_if;
}


// Handle a receiver GAP event.
void cBtServer::handleGAPEvent(
		esp_gap_ble_cb_event_t  event,
		esp_ble_gap_cb_param_t* param) {
	ESP_LOGD(LOG_TAG, "cBtServer ... handling GAP event! %d", event);
	switch(event) {
	case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT: {
		break;
	}
	default:
		break;
	}
} // handleGAPEvent


// Handle a GATT Server Event.
void cBtServer::handleGATTServerEvent(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t* param) {

	ESP_LOGD(LOG_TAG, ">> handleGATTServerEvent: %d", event);


	// Invoke the handler for every Service we have.
	for(auto &svc : services){
		svc->handleGATTServerEvent(event, gatts_if, param);
	}

	switch(event) {


	// ESP_GATTS_CONNECT_EVT
	// connect:
	// - uint16_t conn_id
	// - esp_bd_addr_t remote_bda
	// - bool is_connected
	case ESP_GATTS_CONNECT_EVT: {
		m_connId = param->connect.conn_id; // Save the connection id.

		m_connectedCount++;
		last_client_active_t = cBaseTask::GetTickCount();
		// stop advertising after the first connected client
		m_bleAdvertising.stop();
		TS_PRINT("BT client is connected");
		break;
	} // ESP_GATTS_CONNECT_EVT


	// ESP_GATTS_REG_EVT
	// reg:
	// - esp_gatt_status_t status
	// - uint16_t app_id
	case ESP_GATTS_REG_EVT: {
		m_gatts_if = gatts_if;
		m_semaphoreRegisterAppEvt.Unlock();
		startAdvertising();
		break;
	} // ESP_GATTS_REG_EVT


	// ESP_GATTS_CREATE_EVT
	// Called when a new service is registered as having been created.
	//
	// create:
	// * esp_gatt_status_t status
	// * uint16_t service_handle
	// * esp_gatt_srvc_id_t service_id
	//
	case ESP_GATTS_CREATE_EVT: {
		cBtService* pService = ServiceFind(param->create.service_id.id.uuid);
		if(pService)
			pService->m_handle = param->create.service_handle; //set handle
		else
			ESP_LOGE(LOG_TAG, "ESP_GATTS_CREATE_EVT called for server with unknown UUID!");
		m_semaphoreCreateEvt.Unlock();
		break;
	} // ESP_GATTS_CREATE_EVT


	// ESP_GATTS_READ_EVT - A request to read the value of a characteristic has arrived.
	//
	// read:
	// - uint16_t conn_id
	// - uint32_t trans_id
	// - esp_bd_addr_t bda
	// - uint16_t handle
	// - uint16_t offset
	// - bool is_long
	// - bool need_rsp
	//
	case ESP_GATTS_READ_EVT: {
		last_client_active_t = cBaseTask::GetTickCount();
		break;
	} // ESP_GATTS_READ_EVT


	// ESP_GATTS_WRITE_EVT - A request to write the value of a characteristic has arrived.
	//
	// write:
	// - uint16_t conn_id
	// - uint16_t trans_id
	// - esp_bd_addr_t bda
	// - uint16_t handle
	// - uint16_t offset
	// - bool need_rsp
	// - bool is_prep
	// - uint16_t len
	// - uint8_t *value

	case ESP_GATTS_WRITE_EVT: {
		last_client_active_t = cBaseTask::GetTickCount();
		break;
	}

	// ESP_GATTS_DISCONNECT_EVT
	// If we receive a disconnect event then invoke the callback for disconnects (if one is present).
	// we also want to start advertising again.
	case ESP_GATTS_DISCONNECT_EVT: {
		m_connectedCount--;
		startAdvertising();
		TS_PRINT("BT client is disconnected");
		break;
	} // ESP_GATTS_DISCONNECT_EVT


	// ESP_GATTS_ADD_CHAR_EVT - Indicate that a characteristic was added to the service.
	// add_char:
	// - esp_gatt_status_t status
	// - uint16_t attr_handle
	// - uint16_t service_handle
	// - esp_bt_uuid_t char_uuid
	case ESP_GATTS_ADD_CHAR_EVT: {
		break;
	} // ESP_GATTS_ADD_CHAR_EVT


	default: {
		break;
	}
	}
	ESP_LOGD(LOG_TAG, "<< handleGATTServerEvent");
} // handleGATTServerEvent


/**
 * @brief Register the app.
 *
 * @return N/A
 */
void cBtServer::registerApp() {
	ESP_LOGD(LOG_TAG, ">> registerApp - %d", m_appId);
	m_semaphoreRegisterAppEvt.Lock(); // Take the mutex, will be released by ESP_GATTS_REG_EVT event.
	esp_ble_gatts_app_register(m_appId);
	m_semaphoreRegisterAppEvt.Wait();
	ESP_LOGD(LOG_TAG, "<< registerApp");
} // registerApp


// Start the server advertising its existence.  This is a convenience function and is equivalent to
void cBtServer::startAdvertising() {
	ESP_LOGD(LOG_TAG, ">> startAdvertising");
	m_bleAdvertising.start();
	ESP_LOGD(LOG_TAG, "<< startAdvertising");
} // startAdvertising




// ============================================== cBtService ================================================

cBtService::cBtService():pServ(nullptr),
		m_semaphoreAddCharEvt("cBtService::AddCharEvt"),
		m_semaphoreCreateEvt("cBtService::CreateEvt"),
		m_semaphoreStartEvt("cBtService::StartEvt"),
		m_handle(-1),
		m_lastCreatedCharacteristic(nullptr){
}

cBtService::~cBtService(){
	// chars cleanup
	for(auto &c : chars){
		delete c;
	}
	chars.clear();
}


cBtCharacteristic* cBtService::CharFind(const BLEUUID &uuid){
	for(auto &c : chars){
		if(c->uuid  == uuid)
			return c;
	}
	return nullptr;
}

cBtCharacteristic* cBtService::CharCreate(const BLEUUID &uuid){
	auto pc = CharFind(uuid);
	if(pc) return pc;
	pc = new cBtCharacteristic;
	pc->uuid = uuid;
	chars.push_back(pc);
	return pc;
}

void cBtService::Start(){

	executeCreate();   // Perform the API calls to actually create the service.

	// We ask the BLE runtime to start the service and then create each of the characteristics.
	// We start the service through its local handle which was returned in the ESP_GATTS_CREATE_EVT event
	// obtained as a result of calling esp_ble_gatts_create_service().
	//
	ESP_LOGD(LOG_TAG, ">> cBtService::start(): Starting service (esp_ble_gatts_start_service): %s", uuid.toString().c_str());
	if (m_handle == (uint16_t)-1) {
		ESP_LOGE(LOG_TAG, "<< !!! We attempted to start a service but don't know its handle!");
		return;
	}

	// Start each of the characteristics ... these are found in the m_characteristicMap.

	for(auto &pc: chars){
		m_semaphoreAddCharEvt.Lock();
		m_lastCreatedCharacteristic = pc;
		pc->executeCreate(this);
	}

	m_semaphoreStartEvt.Lock();
	esp_err_t errRc = ::esp_ble_gatts_start_service(m_handle);

	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_start_service: rc=%d", errRc);
		return;
	}
	m_semaphoreStartEvt.Wait();

	ESP_LOGD(LOG_TAG, "<< cBtService::start()");

}

void cBtService::executeCreate(){
	ESP_LOGD(LOG_TAG, ">> executeCreate() - Creating service (esp_ble_gatts_create_service) service uuid: %s", uuid.toString().c_str());

	esp_gatt_srvc_id_t srvc_id;
	srvc_id.id.inst_id = 0;
	srvc_id.is_primary = true; // !!! very important https://github.com/nkolban/esp32-snippets/issues/109
	srvc_id.id.uuid    = *uuid.getNative();

	m_semaphoreCreateEvt.Lock(); // Take the mutex and release at event ESP_GATTS_CREATE_EVT
	// calculate handles count to allocate
	int handle_count_required = 4 + chars.size() * 2;
	// start service registration
	esp_err_t errRc = esp_ble_gatts_create_service(pServ->getGattsIf(), &srvc_id, handle_count_required);

	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gatts_create_service: rc=%d", errRc);
		return;
	}

	m_semaphoreCreateEvt.Wait();
	m_semaphoreCreateEvt.Unlock();

	ESP_LOGD(LOG_TAG, "<< executeCreate");
}

void cBtService::handleGATTServerEvent(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t *param) {


	switch(event) {
	// ESP_GATTS_ADD_CHAR_EVT - Indicate that a characteristic was added to the service.
	// add_char:
	// - esp_gatt_status_t status
	// - uint16_t attr_handle
	// - uint16_t service_handle
	// - esp_bt_uuid_t char_uuid

	// If we have reached the correct service, then locate the characteristic and remember the handle
	// for that characteristic.
	case ESP_GATTS_ADD_CHAR_EVT: {
		if (m_handle == param->add_char.service_handle) {
			cBtCharacteristic *pCharacteristic = CharFind(BLEUUID(param->add_char.char_uuid));
			if (pCharacteristic == nullptr) {
				ESP_LOGE(LOG_TAG, "Expected to find characteristic with UUID: %s, but didnt!",
						BLEUUID(param->add_char.char_uuid).toString().c_str());
				//dump();
				m_semaphoreAddCharEvt.Unlock();
				break;
			}
			pCharacteristic->setHandle(param->add_char.attr_handle);
			//ESP_LOGD(tag, "Characteristic map: %s", m_characteristicMap.toString().c_str());
			m_semaphoreAddCharEvt.Unlock();
			break;
		} // Reached the correct service.
		break;
	} // ESP_GATTS_ADD_CHAR_EVT

	// ESP_GATTS_START_EVT
	//
	// start:
	// esp_gatt_status_t status
	// uint16_t service_handle
	case ESP_GATTS_START_EVT: {
		if (param->start.service_handle == m_handle) {
			m_semaphoreStartEvt.Unlock();
		}
		break;
	} // ESP_GATTS_START_EVT


	// ESP_GATTS_CREATE_EVT
	// Called when a new service is registered as having been created.
	//
	// create:
	// * esp_gatt_status_t status
	// * uint16_t service_handle
	// * esp_gatt_srvc_id_t service_id
	// * - esp_gatt_id id
	// *   - esp_bt_uuid uuid
	// *   - uint8_t inst_id
	// * - bool is_primary
	//
	case ESP_GATTS_CREATE_EVT: {
		if (uuid == param->create.service_id.id.uuid) {
			m_handle = param->create.service_handle;
			m_semaphoreCreateEvt.Unlock();
		}
		break;
	} // ESP_GATTS_CREATE_EVT

	default: {
		break;
	} // Default
	} // Switch

	for(auto &ch : chars){
		ch->handleGATTServerEvent(event, gatts_if, param);
	}
} // handleGATTServerEvent

