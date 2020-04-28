/*
 * cBtCharacteristic.h
 *
 *  Created on: 11.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#ifndef COMPONENTS_M_BT_CBTCHARACTERISTIC_H_
#define COMPONENTS_M_BT_CBTCHARACTERISTIC_H_
#include "sdkconfig.h"
#include "cBtServer.h"
#include "BLEUUID.h"

class cBtService;
class cBtCharacteristic;

// BT Characteristic Value
class cBtCharValue {
public:
	cBtCharValue();
	void        addPart(const std::string &part);
	void        addPart(uint8_t* pData, size_t length);
	void        cancel();
	void        commit();
	bool		execAllowed(void);
	uint16_t    getReadOffset();
	const std::string& getValue();
	void        setReadOffset(uint16_t readOffset);
	void        setValue(const std::string &value);
	void        setValue(uint8_t* pData, size_t length);

private:
	std::string m_accumulation;
	uint16_t    m_readOffset;
	std::string m_value;
	// flag to remember whether multi-package write is ongoing (prepare / execute write)
	bool		b_accumulation_ongoing;
};

// BT Characteristic Descriptor
class cBtCharDescriptor{
	friend class cBtCharacteristic;
public:
	BLEUUID  uuid;
	cBtCharDescriptor();
	~cBtCharDescriptor();

	size_t   getLength();
	uint8_t* getValue();
	void handleGATTServerEvent(
			esp_gatts_cb_event_t      event,
			esp_gatt_if_t             gatts_if,
			esp_ble_gatts_cb_param_t* param);
	void setValue(uint8_t* data, size_t size);
	void setValue(std::string value);
	std::string toString();

private:

	esp_attr_value_t     m_value;
	uint16_t             m_handle;
	cBtCharacteristic*   m_pCharacteristic;
	void executeCreate(cBtCharacteristic* pCharacteristic);
	uint16_t getHandle();
	void setHandle(uint16_t handle);
	cSemaphore m_semaphoreCreateEvt;
};


// callbacks to read/write notifications for a characteristic
// inherit this class and define required methods
class cBtCharCallbacks{
public:
	virtual void AftereWrite(cBtCharacteristic *pCaller){}
	virtual void BeforeRead(cBtCharacteristic *pCaller){}
	virtual ~cBtCharCallbacks(){}
};

// BT Service Characteristic
class cBtCharacteristic {
	friend class cBtServer;
	friend class cBtService;
	friend class cBtCharDescriptor;

	std::list<cBtCharDescriptor*> descriptors;
public:
	BLEUUID uuid; // characteristic ID

	cBtCharDescriptor* DescFind(const BLEUUID &uuid);
	cBtCharDescriptor* DescCreate(const BLEUUID &uuid);
	void DescAdd(cBtCharDescriptor *pdesc);

	cBtCharacteristic();
	~cBtCharacteristic();

	const std::string& getValue();

	void indicate();
	void notify();
	void setBroadcastProperty(bool value);
	void setCallbacks(cBtCharCallbacks* pCallbacks){m_pCallbacks = pCallbacks;}
	void setIndicateProperty(bool value);
	void setNotifyProperty(bool value);
	void setReadProperty(bool value);
	void setValue(uint8_t* data, size_t size);
	void setValue(std::string value);
	void setWriteProperty(bool value);
	void setWriteNoResponseProperty(bool value);
	std::string toString();

	void setProperties(uint32_t properties);

	static const uint32_t PROPERTY_READ      = 1<<0;
	static const uint32_t PROPERTY_WRITE     = 1<<1;
	static const uint32_t PROPERTY_NOTIFY    = 1<<2;
	static const uint32_t PROPERTY_BROADCAST = 1<<3;
	static const uint32_t PROPERTY_INDICATE  = 1<<4;
	static const uint32_t PROPERTY_WRITE_NR  = 1<<5;

private:

	uint16_t                    m_handle;
	esp_gatt_char_prop_t        m_properties;
	cBtCharCallbacks* 			m_pCallbacks;
	cBtService*                 m_pService;
	cBtCharValue                m_value;

	void handleGATTServerEvent(
			esp_gatts_cb_event_t      event,
			esp_gatt_if_t             gatts_if,
			esp_ble_gatts_cb_param_t* param);

	void                 executeCreate(cBtService* pService);
	uint16_t             getHandle();
	esp_gatt_char_prop_t getProperties();
	cBtService*          getService();
	void                 setHandle(uint16_t handle);
	cSemaphore m_semaphoreCreateEvt;
	cSemaphore m_semaphoreConfEvt;
};

#endif /* COMPONENTS_M_BT_CBTCHARACTERISTIC_H_ */
