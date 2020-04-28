/*
 * cBtServer.h
 *
 *  Created on: 11.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#ifndef COMPONENTS_M_BT_CBTSERVER_H_
#define COMPONENTS_M_BT_CBTSERVER_H_
#include "sdkconfig.h"
#include <esp_gatts_api.h>
#include <list>
#include "../../main/common/cBaseTask.h"
#include "cBtDevice.h"
#include "BLEUUID.h"
#include "BLEAdvertising.h"
#include "cBtCharacteristic.h"

class cBtServer;
class cBtService;
class cBtAttribute;
class cBtCharacteristic;

// BT Application Server Service
class cBtService{
	friend class cBtServer;
	friend class cBtAttribute;
	friend class cBtCharacteristic;
	friend class cBtCharDescriptor;

	cBtServer *pServ;
	cSemaphore m_semaphoreAddCharEvt;
	cSemaphore m_semaphoreCreateEvt;
	cSemaphore m_semaphoreStartEvt;

	std::list<cBtCharacteristic*> chars;

public:
	BLEUUID uuid; // service unique ID
	uint16_t m_handle; // service handle
	cBtService();
	~cBtService();

	cBtCharacteristic* CharFind(const BLEUUID &uuid);
	cBtCharacteristic* CharCreate(const BLEUUID &uuid);
	// call this to start the service when all attributes are ready
	void Start();

private:
	cBtCharacteristic*   m_lastCreatedCharacteristic;
	void executeCreate();
	void handleGATTServerEvent(
			esp_gatts_cb_event_t      event,
			esp_gatt_if_t             gatts_if,
			esp_ble_gatts_cb_param_t* param);
};

// The BT Application Sever
class cBtServer {
	friend class cBtDevice;
	friend class cBtService;
	friend class cBtCharacteristic;

	esp_ble_adv_data_t  m_adv_data;
	uint16_t            m_appId;

	uint16_t			m_connId;
	uint32_t            m_connectedCount;
	int          		m_gatts_if;
	cBtDevice *pDevice;
	std::list<cBtService*> services; // pointers to all our services
	BLEAdvertising      m_bleAdvertising; // BT advertiser

public:
	uint32_t last_client_active_t; // timestamp of the any client's activity
	uint32_t server_create_t; // timestamp of the server instantiation

	cBtService* ServiceFind(const BLEUUID &uuid);
	// entry point to add a service
	cBtService* ServiceCreate(const BLEUUID &uuid);
	void ServiceAddExisting(cBtService *pSvc); // not used now

	cBtServer();
	~cBtServer();
	void Init(cBtDevice *pDev);
	void Deinit();
	// register all objects and start processing
	void Start();
	uint32_t        getConnectedCount();
	BLEAdvertising* getAdvertising();
	void            startAdvertising();
	int        getGattsIf();
private:

	void            createApp(uint16_t appId);
	uint16_t        getConnId();
	void            handleGAPEvent(esp_gap_ble_cb_event_t event,	esp_ble_gap_cb_param_t *param);
	void            handleGATTServerEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
	void            registerApp();

	cSemaphore m_semaphoreRegisterAppEvt;
	cSemaphore m_semaphoreCreateEvt;
};
#endif /* COMPONENTS_M_BT_CBTSERVER_H_ */
