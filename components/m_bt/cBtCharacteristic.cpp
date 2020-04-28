/*
 * cBtCharacteristic.cpp
 *
 *  Created on: 11.09.2017 (c) EmSo
 *      Author: koban
 *      Modified by D. Pavlenko @ EmSo 2017.09.11
 */

#include "cBtCharacteristic.h"
#include <esp_log.h>
#include <sstream>
#include <string.h>
#include <iomanip>
#include <stdlib.h>
#include "BLE2902.h"

static const char* LOG_TAG = "cBtCharacteristic";

cBtCharValue::cBtCharValue() {
	m_accumulation = "";
	m_value        = "";
	m_readOffset   = 0;
	b_accumulation_ongoing = false;
} // cBtCharValue


/**
 * @brief Add a message part to the accumulation.
 * The accumulation is a growing set of data that is added to until a commit or cancel.
 * @param [in] part A message part being added.
 */
void cBtCharValue::addPart(const std::string &part) {
	ESP_LOGD(LOG_TAG, ">> addPart: length=%d", part.length());
	m_accumulation += part;
	b_accumulation_ongoing = true;
} // addPart


/**
 * @brief Add a message part to the accumulation.
 * The accumulation is a growing set of data that is added to until a commit or cancel.
 * @param [in] pData A message part being added.
 * @param [in] length The number of bytes being added.
 */
void cBtCharValue::addPart(uint8_t* pData, size_t length) {
	ESP_LOGD(LOG_TAG, ">> addPart: length=%d", length);
	m_accumulation += std::string((char *)pData, length);
	b_accumulation_ongoing = true;
} // addPart


/**
 * @brief Cancel the current accumulation.
 */
void cBtCharValue::cancel() {
	ESP_LOGD(LOG_TAG, ">> cancel");
	m_accumulation = "";
	m_readOffset   = 0;
	b_accumulation_ongoing = false;
} // cancel


/**
 * @brief Commit the current accumulation.
 * When writing a value, we may find that we write it in "parts" meaning that the writes come in in pieces
 * of the overall message.  After the last part has been received, we may perform a commit which means that
 * we now have the complete message and commit the change as a unit.
 */
void cBtCharValue::commit() {
	ESP_LOGD(LOG_TAG, ">> commit");
	// If there is nothing to commit, do nothing.
	if (m_accumulation.length() == 0) {
		return;
	}
	setValue(m_accumulation);
	m_accumulation = "";
	m_readOffset   = 0;
	b_accumulation_ongoing = false;
} // commit

/**
 * @brief True if BLE execute write is allowed for this value.
 * When doing multi-packet writes (prepare write), the individual packets
 * are accumulated. To finalize the write an exec_write event is sent.
 * This method answers whether or not an exec write is allowed/possible for this value.
 */
bool cBtCharValue::execAllowed(void){
	return this->b_accumulation_ongoing;
}

/**
 * @brief Get the read offset.
 * @return The read offset into the read.
 */
uint16_t cBtCharValue::getReadOffset() {
	return m_readOffset;
} // getReadOffset


/**
 * @brief Get the current value.
 */
const std::string& cBtCharValue::getValue() {
	return m_value;
} // getValue


/**
 * @brief Set the read offset
 * @param [in] readOffset The offset into the read.
 */
void cBtCharValue::setReadOffset(uint16_t readOffset) {
	m_readOffset = readOffset;
} // setReadOffset


/**
 * @brief Set the current value.
 */
void cBtCharValue::setValue(const std::string &value) {
	m_value = value;
} // setValue


/**
 * @brief Set the current value.
 * @param [in] pData The data for the current value.
 * @param [in] The length of the new current value.
 */
void cBtCharValue::setValue(uint8_t* pData, size_t length) {
	m_value = std::string((char*)pData, length);
} // setValue


//===================================== cBtCharacteristic =============================

cBtCharacteristic::cBtCharacteristic(): m_pCallbacks(nullptr), m_pService(nullptr),
	m_semaphoreCreateEvt("cBtCharacteristic::CreateEvt"),
	m_semaphoreConfEvt("cBtCharacteristic::ConfEvt") {
	m_properties = (esp_gatt_char_prop_t)0;
	m_handle  = uint16_t(-1);
}

cBtCharacteristic::~cBtCharacteristic() {
	// free descriptors memory
	for(auto &d : descriptors){
		delete d;
	}
	descriptors.clear();
}

void cBtCharacteristic::setProperties(uint32_t properties){
	m_properties = (esp_gatt_char_prop_t)0;
	setBroadcastProperty((properties & PROPERTY_BROADCAST) !=0);
	setReadProperty((properties & PROPERTY_READ) !=0);
	setWriteProperty((properties & PROPERTY_WRITE) !=0);
	setNotifyProperty((properties & PROPERTY_NOTIFY) !=0);
	setIndicateProperty((properties & PROPERTY_INDICATE) !=0);
	setWriteNoResponseProperty((properties & PROPERTY_WRITE_NR) !=0);
}

cBtCharDescriptor* cBtCharacteristic::DescFind(const BLEUUID &uuid){
	for(auto &d : descriptors){
		if(d->uuid == uuid)
			return d;
	}
	return nullptr;
}

cBtCharDescriptor* cBtCharacteristic::DescCreate(const BLEUUID &uuid){
	auto pdesc = DescFind(uuid);
	if(!pdesc){
		pdesc = new cBtCharDescriptor;
		pdesc->uuid = uuid;
		descriptors.push_back(pdesc);
	}
	return pdesc;
}

void cBtCharacteristic::DescAdd(cBtCharDescriptor *pdesc){
	auto pd = DescFind(pdesc->uuid);
	if(pd) return;
	descriptors.push_back(pdesc);
}

/**
 * @brief Register a new characteristic with the ESP runtime.
 * @param [in] pService The service with which to associate this characteristic.
 */
void cBtCharacteristic::executeCreate(cBtService* pService) {
	ESP_LOGD(LOG_TAG, ">> executeCreate()");

	if (m_handle != uint16_t(-1)) {
		ESP_LOGE(LOG_TAG, "Characteristic already has a handle.");
		return;
	}

	m_pService = pService; // Save the service for to which this characteristic belongs.

	ESP_LOGD(LOG_TAG, "Registering characteristic (esp_ble_gatts_add_char): uuid: %s, service: %s",
		uuid.toString().c_str(),
		m_pService->uuid.toString().c_str());

	esp_attr_control_t control;
	control.auto_rsp = ESP_GATT_RSP_BY_APP;

	m_semaphoreCreateEvt.Lock();

	const std::string& strValue = m_value.getValue();

	esp_attr_value_t value;
	value.attr_len     = strValue.length() + 1;
	value.attr_max_len = 0; //ESP_GATT_MAX_ATTR_LEN / 4;
	value.attr_value   = (uint8_t*)strValue.data();

	esp_err_t errRc = ::esp_ble_gatts_add_char(
		m_pService->m_handle,
		uuid.getNative(),
		esp_gatt_perm_t(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE),
		getProperties(),
		&value,
		&control); // Whether to auto respond or not.

	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_add_char: rc=0x%x", errRc);
		return;
	}

	m_semaphoreCreateEvt.Wait();

	// Now that we have registered the characteristic, we must also register all the descriptors associated with this
	// characteristic.  We iterate through each of those and invoke the registration call to register them with the
	// ESP environment.

	for (auto &d : descriptors){
		d->executeCreate(this);
	}

	ESP_LOGD(LOG_TAG, "<< executeCreate");
} // executeCreate


/**
 * @brief Get the handle of the characteristic.
 * @return The handle of the characteristic.
 */
uint16_t cBtCharacteristic::getHandle() {
	return m_handle;
} // getHandle


esp_gatt_char_prop_t cBtCharacteristic::getProperties() {
	return m_properties;
} // getProperties


/**
 * @brief Get the service associated with this characteristic.
 */
cBtService* cBtCharacteristic::getService() {
	return m_pService;
} // getService


/**
 * @brief Retrieve the current value of the characteristic.
 * @return A pointer to storage containing the current characteristic value.
 */
const std::string& cBtCharacteristic::getValue() {
	return m_value.getValue();
} // getValue


void cBtCharacteristic::handleGATTServerEvent(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t* param) {
	switch(event) {
	// Events handled:
	// ESP_GATTS_ADD_CHAR_EVT
	// ESP_GATTS_WRITE_EVT
	// ESP_GATTS_READ_EVT
	//

		// ESP_GATTS_EXEC_WRITE_EVT
		// When we receive this event it is an indication that a previous write long needs to be committed.
		//
		// exec_write:
		// - uint16_t conn_id
		// - uint32_t trans_id
		// - esp_bd_addr_t bda
		// - uint8_t exec_write_flag
		//
		// param does not provide a characteristic handle or uuid for exec_write
		// we cannot simply call handleGATTServerEvent for every characteristic when an exec_write
		// occurs, because AfterWrite callbacks will be triggered on characteristics that weren't even
		// written. handled by b_accumulation_ongoing of cBtCharValue 
		case ESP_GATTS_EXEC_WRITE_EVT: {
			if(m_value.execAllowed()){
				if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
					m_value.commit();
					if (m_pCallbacks != nullptr) {
						m_pCallbacks->AftereWrite(this); // Invoke the onWrite callback handler.
					}
				}else{
					// ESP_GATT_PREP_WRITE_CANCEL
					m_value.cancel();
				}

				esp_err_t errRc = ::esp_ble_gatts_send_response(
					gatts_if,
					param->write.conn_id,
					param->write.trans_id, ESP_GATT_OK, nullptr);
				if (errRc != ESP_OK) {
					ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d", errRc);
				}
			}
			break;
		} // ESP_GATTS_EXEC_WRITE_EVT


		// ESP_GATTS_ADD_CHAR_EVT - Indicate that a characteristic was added to the service.
		// add_char:
		// - esp_gatt_status_t status
		// - uint16_t attr_handle
		// - uint16_t service_handle
		// - esp_bt_uuid_t char_uuid
		case ESP_GATTS_ADD_CHAR_EVT: {
			if (uuid == param->add_char.char_uuid &&
					getService()->m_handle == param->add_char.service_handle) {
				if(param->add_char.status != ESP_GATT_OK)
						ESP_LOGE(LOG_TAG, "ESP_GATTS_ADD_CHAR_EVT: status=0x%x", param->add_char.status);
				m_semaphoreCreateEvt.Unlock();
			}
			break;
		} // ESP_GATTS_ADD_CHAR_EVT


		// ESP_GATTS_WRITE_EVT - A request to write the value of a characteristic has arrived.
		//
		// write:
		// - uint16_t      conn_id
		// - uint16_t      trans_id
		// - esp_bd_addr_t bda
		// - uint16_t      handle
		// - uint16_t      offset
		// - bool          need_rsp
		// - bool          is_prep
		// - uint16_t      len
		// - uint8_t      *value
		//
		case ESP_GATTS_WRITE_EVT: {
// We check if this write request is for us by comparing the handles in the event.  If it is for us
// we save the new value.  Next we look at the need_rsp flag which indicates whether or not we need
// to send a response.  If we do, then we formulate a response and send it.
			if (param->write.handle == m_handle) {
				if (param->write.is_prep) {
					m_value.addPart(param->write.value, param->write.len);
				} else {
					setValue(param->write.value, param->write.len);
				}

				ESP_LOGD(LOG_TAG, " - Response to write event: New value: handle: %.2x, uuid: %s",
						getHandle(), uuid.toString().c_str());

				ESP_LOGD(LOG_TAG, " - Data: length: %d", param->write.len);

				if (param->write.need_rsp) {
					esp_gatt_rsp_t rsp;

					rsp.attr_value.len      = param->write.len;
					rsp.attr_value.handle   = m_handle;
					rsp.attr_value.offset   = param->write.offset;
					rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
					memcpy(rsp.attr_value.value, param->write.value, param->write.len);

					esp_err_t errRc = ::esp_ble_gatts_send_response(
							gatts_if,
							param->write.conn_id,
							param->write.trans_id, ESP_GATT_OK, &rsp);
					if (errRc != ESP_OK) {
						ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d", errRc);
					}
				} // Response needed

				if (m_pCallbacks != nullptr && param->write.is_prep != true) {
					m_pCallbacks->AftereWrite(this); // Invoke the onWrite callback handler.
				}
			} // Match on handles.
			break;
		} // ESP_GATTS_WRITE_EVT


		// ESP_GATTS_READ_EVT - A request to read the value of a characteristic has arrived.
		//
		// read:
		// - uint16_t      conn_id
		// - uint32_t      trans_id
		// - esp_bd_addr_t bda
		// - uint16_t      handle
		// - uint16_t      offset
		// - bool          is_long
		// - bool          need_rsp
		//
		case ESP_GATTS_READ_EVT: {
			//ESP_LOGD(LOG_TAG, "- Testing: 0x%.2x == 0x%.2x", param->read.handle, m_handle);
			if (param->read.handle == m_handle) {
				ESP_LOGD(LOG_TAG, "- Testing: 0x%.2x == 0x%.2x", param->read.handle, m_handle);
				if (m_pCallbacks != nullptr) {
					m_pCallbacks->BeforeRead(this); // Invoke the read callback.
				}

// Here's an interesting thing.  The read request has the option of saying whether we need a response
// or not.  What would it "mean" to receive a read request and NOT send a response back?  That feels like
// a very strange read.
//
// We have to handle the case where the data we wish to send back to the client is greater than the maximum
// packet size of 22 bytes.  In this case, we become responsible for chunking the data into uints of 22 bytes.
// The apparent algorithm is as follows.
// If the is_long flag is set then this is a follow on from an original read and we will already have sent at least 22 bytes.
// If the is_long flag is not set then we need to check how much data we are going to send.  If we are sending LESS than
// 22 bytes, then we "just" send it and thats the end of the story.
// If we are sending 22 bytes exactly, we just send it BUT we will get a follow on request.
// If we are sending more than 22 bytes, we send the first 22 bytes and we will get a follow on request.
// Because of follow on request processing, we need to maintain an offset of how much data we have already sent
// so that when a follow on request arrives, we know where to start in the data to send the next sequence.
// Note that the indication that the client will send a follow on request is that we sent exactly 22 bytes as a response.
// If our payload is divisible by 22 then the last response will be a response of 0 bytes in length.
//
// The following code has deliberately not been factored to make it fewer statements because this would cloud
// the logic flow comprehension.
//
				if (param->read.need_rsp) {
					ESP_LOGD(LOG_TAG, "Sending a response (esp_ble_gatts_send_response)");
					esp_gatt_rsp_t rsp;
					std::string value = m_value.getValue();
					if (param->read.is_long) {
						if (value.length() - m_value.getReadOffset() < 22) {
							// This is the last in the chain
							rsp.attr_value.len    = value.length() - m_value.getReadOffset();
							rsp.attr_value.offset = m_value.getReadOffset();
							memcpy(rsp.attr_value.value, value.data() + rsp.attr_value.offset, rsp.attr_value.len);
							m_value.setReadOffset(0);
						} else {
							// There will be more to come.
							rsp.attr_value.len    = 22;
							rsp.attr_value.offset = m_value.getReadOffset();
							memcpy(rsp.attr_value.value, value.data() + rsp.attr_value.offset, rsp.attr_value.len);
							m_value.setReadOffset(rsp.attr_value.offset + 22);
						}
					} else {
						if (value.length() > 21) {
							// Too big for a single shot entry.
							m_value.setReadOffset(22);
							rsp.attr_value.len    = 22;
							rsp.attr_value.offset = 0;
							memcpy(rsp.attr_value.value, value.data(), rsp.attr_value.len);
						} else {
							// Will fit in a single packet with no callbacks required.
							rsp.attr_value.len    = value.length();
							rsp.attr_value.offset = 0;
							memcpy(rsp.attr_value.value, value.data(), rsp.attr_value.len);
						}
					}
					rsp.attr_value.handle   = param->read.handle;
					rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;

					ESP_LOGD(LOG_TAG, " - Data: length=%d, offset=%d", rsp.attr_value.len, rsp.attr_value.offset);

					esp_err_t errRc = ::esp_ble_gatts_send_response(
							gatts_if, param->read.conn_id,
							param->read.trans_id,
							ESP_GATT_OK,
							&rsp);
					if (errRc != ESP_OK) {
						ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d", errRc);
					}
				} // Response needed
			} // Handle matches this characteristic.
			break;
		} // ESP_GATTS_READ_EVT

		// ESP_GATTS_CONF_EVT
		//
		// conf:
		// - esp_gatt_status_t status  � The status code.
		// - uint16_t          conn_id � The connection used.
		//
		case ESP_GATTS_CONF_EVT: {
			m_semaphoreConfEvt.Unlock();
			break;
		}

		default: {
			break;
		} // default

	} // switch event

	// Give each of the descriptors associated with this characteristic the opportunity to handle the
	// event.
	for(auto &desc : descriptors){
		desc->handleGATTServerEvent(event, gatts_if, param);
	}

} // handleGATTServerEvent

/**
 * @brief Send an indication.
 * An indication is a transmission of up to the first 20 bytes of the characteristic value.  An indication
 * will block waiting a positive confirmation from the client.
 * @return N/A
 */
void cBtCharacteristic::indicate() {

	ESP_LOGD(LOG_TAG, ">> indicate: length: %d", m_value.getValue().length());

	assert(getService() != nullptr);
	assert(getService()->pServ != nullptr);

	//GeneralUtils::hexDump((uint8_t*)m_value.getValue().data(), m_value.getValue().length());

	if (getService()->pServ->getConnectedCount() == 0) {
		ESP_LOGD(LOG_TAG, "<< indicate: No connected clients.");
		return;
	}

	// Test to see if we have a 0x2902 descriptor.  If we do, then check to see if indications are enabled
	// and, if not, prevent the indication.

	BLE2902 *p2902 = (BLE2902*)DescFind((uint16_t)0x2902);
	if (p2902 != nullptr && !p2902->getIndications()) {
		ESP_LOGD(LOG_TAG, "<< indications disabled; ignoring");
		return;
	}

	if (m_value.getValue().length() > 20) {
		ESP_LOGD(LOG_TAG, "- Truncating to 20 bytes (maximum notify size)");
	}

	size_t length = m_value.getValue().length();
	if (length > 20) {
		length = 20;
	}

	m_semaphoreConfEvt.Lock();

	esp_err_t errRc = ::esp_ble_gatts_send_indicate(
			getService()->pServ->getGattsIf(),
			getService()->pServ->getConnId(),
			getHandle(), length, (uint8_t*)m_value.getValue().data(), true); // The need_confirm = true makes this an indication.

	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_send_indicate: rc=%d", errRc);
		return;
	}

	m_semaphoreConfEvt.Wait();
	ESP_LOGD(LOG_TAG, "<< indicate");
} // indicate


/**
 * @brief Send a notify.
 * A notification is a transmission of up to the first 20 bytes of the characteristic value.  An notification
 * will not block; it is a fire and forget.
 * @return N/A.
 */
void cBtCharacteristic::notify() {
	ESP_LOGD(LOG_TAG, ">> notify: length: %d", m_value.getValue().length());


	assert(getService() != nullptr);
	assert(getService()->pServ != nullptr);


	//GeneralUtils::hexDump((uint8_t*)m_value.getValue().data(), m_value.getValue().length());

	if (getService()->pServ->getConnectedCount() == 0) {
		ESP_LOGD(LOG_TAG, "<< notify: No connected clients.");
		return;
	}

	// Test to see if we have a 0x2902 descriptor.  If we do, then check to see if notification is enabled
	// and, if not, prevent the notification.

	BLE2902 *p2902 = (BLE2902*)DescFind((uint16_t)0x2902);
	if (p2902 != nullptr && !p2902->getNotifications()) {
		ESP_LOGD(LOG_TAG, "<< notifications disabled; ignoring");
		return;
	}

	if (m_value.getValue().length() > 20) {
		ESP_LOGD(LOG_TAG, "- Truncating to 20 bytes (maximum notify size)");
	}

	size_t length = m_value.getValue().length();
	if (length > 20) {
		length = 20;
	}

	esp_err_t errRc = ::esp_ble_gatts_send_indicate(
			getService()->pServ->getGattsIf(),
			getService()->pServ->getConnId(),
			getHandle(), length, (uint8_t*)m_value.getValue().data(), false); // The need_confirm = false makes this a notify.
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_send_indicate: rc=%d", errRc);
		return;
	}

	ESP_LOGD(LOG_TAG, "<< notify");
} // Notify


/**
 * @brief Set the permission to broadcast.
 * A characteristics has properties associated with it which define what it is capable of doing.
 * One of these is the broadcast flag.
 * @param [in] value The flag value of the property.
 * @return N/A
 */
void cBtCharacteristic::setBroadcastProperty(bool value) {
	//ESP_LOGD(LOG_TAG, "setBroadcastProperty(%d)", value);
	if (value) {
		m_properties = (esp_gatt_char_prop_t)(m_properties | ESP_GATT_CHAR_PROP_BIT_BROADCAST);
	} else {
		m_properties = (esp_gatt_char_prop_t)(m_properties & ~ESP_GATT_CHAR_PROP_BIT_BROADCAST);
	}
} // setBroadcastProperty


/**
 * @brief Set the BLE handle associated with this characteristic.
 * A user program will request that a characteristic be created against a service.  When the characteristic has been
 * registered, the service will be given a "handle" that it knows the characteristic as.  This handle is unique to the
 * server/service but it is told to the service, not the characteristic associated with the service.  This internally
 * exposed function can be invoked by the service against this model of the characteristic to allow the characteristic
 * to learn its own handle.  Once the characteristic knows its own handle, it will be able to see incoming GATT events
 * that will be propagated down to it which contain a handle value and now know that the event is destined for it.
 * @param [in] handle The handle associated with this characteristic.
 */
void cBtCharacteristic::setHandle(uint16_t handle) {
	ESP_LOGD(LOG_TAG, ">> setHandle: handle=0x%.2x, characteristic uuid=%s", handle, uuid.toString().c_str());
	m_handle = handle;
	ESP_LOGD(LOG_TAG, "<< setHandle");
} // setHandle


/**
 * @brief Set the Indicate property value.
 * @param [in] value Set to true if we are to allow indicate messages.
 */
void cBtCharacteristic::setIndicateProperty(bool value) {
	//ESP_LOGD(LOG_TAG, "setIndicateProperty(%d)", value);
	if (value) {
		m_properties = (esp_gatt_char_prop_t)(m_properties | ESP_GATT_CHAR_PROP_BIT_INDICATE);
	} else {
		m_properties = (esp_gatt_char_prop_t)(m_properties & ~ESP_GATT_CHAR_PROP_BIT_INDICATE);
	}
} // setIndicateProperty


/**
 * @brief Set the Notify property value.
 * @param [in] value Set to true if we are to allow notification messages.
 */
void cBtCharacteristic::setNotifyProperty(bool value) {
	//ESP_LOGD(LOG_TAG, "setNotifyProperty(%d)", value);
	if (value) {
		m_properties = (esp_gatt_char_prop_t)(m_properties | ESP_GATT_CHAR_PROP_BIT_NOTIFY);
	} else {
		m_properties = (esp_gatt_char_prop_t)(m_properties & ~ESP_GATT_CHAR_PROP_BIT_NOTIFY);
	}
} // setNotifyProperty


/**
 * @brief Set the Read property value.
 * @param [in] value Set to true if we are to allow reads.
 */
void cBtCharacteristic::setReadProperty(bool value) {
	//ESP_LOGD(LOG_TAG, "setReadProperty(%d)", value);
	if (value) {
		m_properties = (esp_gatt_char_prop_t)(m_properties | ESP_GATT_CHAR_PROP_BIT_READ);
	} else {
		m_properties = (esp_gatt_char_prop_t)(m_properties & ~ESP_GATT_CHAR_PROP_BIT_READ);
	}
} // setReadProperty


/**
 * @brief Set the value of the characteristic.
 * @param [in] data The data to set for the characteristic.
 * @param [in] length The length of the data in bytes.
 */
void cBtCharacteristic::setValue(uint8_t* data, size_t length) {
	ESP_LOGD(LOG_TAG, ">> setValue: length=%d, characteristic UUID=%s", length, uuid.toString().c_str());

	if (length > ESP_GATT_MAX_ATTR_LEN) {
		ESP_LOGE(LOG_TAG, "Size %d too large, must be no bigger than %d", length, ESP_GATT_MAX_ATTR_LEN);
		return;
	}
	m_value.setValue(data, length);
	ESP_LOGD(LOG_TAG, "<< setValue");
} // setValue


/**
 * @brief Set the value of the characteristic from string data.
 * We set the value of the characteristic from the bytes contained in the
 * string.
 * @param [in] Set the value of the characteristic.
 * @return N/A.
 */
void cBtCharacteristic::setValue(std::string value) {
	ESP_LOGD(LOG_TAG, "setValue: %s", value.c_str());
	setValue((uint8_t*)(value.data()), value.length());
} // setValue


/**
 * @brief Set the Write No Response property value.
 * @param [in] value Set to true if we are to allow writes with no response.
 */
void cBtCharacteristic::setWriteNoResponseProperty(bool value) {
	//ESP_LOGD(LOG_TAG, "setWriteNoResponseProperty(%d)", value);
	if (value) {
		m_properties = (esp_gatt_char_prop_t)(m_properties | ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
	} else {
		m_properties = (esp_gatt_char_prop_t)(m_properties & ~ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
	}
} // setWriteNoResponseProperty


/**
 * @brief Set the Write property value.
 * @param [in] value Set to true if we are to allow writes.
 */
void cBtCharacteristic::setWriteProperty(bool value) {
	//ESP_LOGD(LOG_TAG, "setWriteProperty(%d)", value);
	if (value) {
		m_properties = (esp_gatt_char_prop_t)(m_properties | ESP_GATT_CHAR_PROP_BIT_WRITE);
	} else {
		m_properties = (esp_gatt_char_prop_t)(m_properties & ~ESP_GATT_CHAR_PROP_BIT_WRITE);
	}
} // setWriteProperty


/**
 * @brief Return a string representation of the characteristic.
 * @return A string representation of the characteristic.
 */
std::string cBtCharacteristic::toString() {
	std::stringstream stringstream;
	stringstream << std::hex << std::setfill('0');
	stringstream << "UUID: " << uuid.toString() + ", handle: 0x" << std::setw(2) << m_handle;
	stringstream << " " <<
		((m_properties & ESP_GATT_CHAR_PROP_BIT_READ)?"Read ":"") <<
		((m_properties & ESP_GATT_CHAR_PROP_BIT_WRITE)?"Write ":"") <<
		((m_properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR)?"WriteNoResponse ":"") <<
		((m_properties & ESP_GATT_CHAR_PROP_BIT_BROADCAST)?"Broadcast ":"") <<
		((m_properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)?"Notify ":"") <<
		((m_properties & ESP_GATT_CHAR_PROP_BIT_INDICATE)?"Indicate ":"");
	return stringstream.str();
} // toString


// ============================ cBtCharDescriptor ===============================================

cBtCharDescriptor::cBtCharDescriptor() {
	m_value.attr_value   = (uint8_t *)malloc(ESP_GATT_MAX_ATTR_LEN); // Allocate storage for the value.
	m_value.attr_len     = 0;
	m_value.attr_max_len = ESP_GATT_MAX_ATTR_LEN;
	m_handle             = -1;
	m_pCharacteristic    = nullptr; // No initial characteristic.

} // cBtCharDescriptor


/**
 * @brief cBtCharDescriptor destructor.
 */
cBtCharDescriptor::~cBtCharDescriptor() {
	free(m_value.attr_value);
} // ~cBtCharDescriptor


/**
 * @brief Execute the creation of the descriptor with the BLE runtime in ESP.
 * @param [in] pCharacteristic The characteristic to which to register this descriptor.
 */
void cBtCharDescriptor::executeCreate(cBtCharacteristic* pCharacteristic) {
	ESP_LOGD(LOG_TAG, ">> executeCreate(): %s", toString().c_str());

	if (m_handle != (uint16_t)-1) {
		ESP_LOGE(LOG_TAG, "Descriptor already has a handle.");
		return;
	}

	m_pCharacteristic = pCharacteristic; // Save the characteristic associated with this service.

	esp_attr_control_t control;
	control.auto_rsp = ESP_GATT_RSP_BY_APP;
	m_semaphoreCreateEvt.Lock();
	esp_err_t errRc = esp_ble_gatts_add_char_descr(
			pCharacteristic->getService()->m_handle,
			uuid.getNative(),
			(esp_gatt_perm_t)(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE),
			&m_value,
			&control);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_add_char_descr: rc=%d", errRc);
		return;
	}

	m_semaphoreCreateEvt.Wait();
	ESP_LOGD(LOG_TAG, "<< executeCreate");
} // executeCreate


/**
 * @brief Get the BLE handle for this descriptor.
 * @return The handle for this descriptor.
 */
uint16_t cBtCharDescriptor::getHandle() {
	return m_handle;
} // getHandle


/**
 * @brief Get the length of the value of this descriptor.
 * @return The length (in bytes) of the value of this descriptor.
 */
size_t cBtCharDescriptor::getLength() {
	return m_value.attr_len;
} // getLength



/**
 * @brief Get the value of this descriptor.
 * @return A pointer to the value of this descriptor.
 */
uint8_t* cBtCharDescriptor::getValue() {
	return m_value.attr_value;
} // getValue


/**
 * @brief Handle GATT server events for the descripttor.
 * @param [in] event
 * @param [in] gatts_if
 * @param [in] param
 */
void cBtCharDescriptor::handleGATTServerEvent(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t *param) {
	switch(event) {
		// ESP_GATTS_ADD_CHAR_DESCR_EVT
		//
		// add_char_descr:
		// - esp_gatt_status_t status
		// - uint16_t          attr_handle
		// - uint16_t          service_handle
		// - esp_bt_uuid_t     char_uuid
		case ESP_GATTS_ADD_CHAR_DESCR_EVT: {

			if (m_pCharacteristic != nullptr &&
					uuid == param->add_char_descr.descr_uuid &&
					m_pCharacteristic->getService()->m_handle == param->add_char_descr.service_handle &&
					m_pCharacteristic == m_pCharacteristic->getService()->m_lastCreatedCharacteristic) {
				setHandle(param->add_char_descr.attr_handle);
				m_semaphoreCreateEvt.Unlock();
			}
			break;
		} // ESP_GATTS_ADD_CHAR_DESCR_EVT

		// ESP_GATTS_WRITE_EVT - A request to write the value of a descriptor has arrived.
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
			if (param->write.handle == m_handle) {
				setValue(param->write.value, param->write.len);
				esp_gatt_rsp_t rsp;
				rsp.attr_value.len    = getLength();
				rsp.attr_value.handle = m_handle;
				rsp.attr_value.offset = 0;
				rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
				memcpy(rsp.attr_value.value, getValue(), rsp.attr_value.len);
				esp_err_t errRc = ::esp_ble_gatts_send_response(
						gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, &rsp);
				if (errRc != ESP_OK) {
					ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d", errRc);
				}
			}
			break;
		} // ESP_GATTS_WRITE_EVT

		// ESP_GATTS_READ_EVT - A request to read the value of a descriptor has arrived.
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
			ESP_LOGD(LOG_TAG, "- Testing: Sought handle: 0x%.2x == descriptor handle: 0x%.2x ?", param->read.handle, m_handle);
			if (param->read.handle == m_handle) {
				ESP_LOGD(LOG_TAG, "Sending a response (esp_ble_gatts_send_response)");
				if (param->read.need_rsp) {
					esp_gatt_rsp_t rsp;
					rsp.attr_value.len    = getLength();
					rsp.attr_value.handle = param->read.handle;
					rsp.attr_value.offset = 0;
					rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
					memcpy(rsp.attr_value.value, getValue(), rsp.attr_value.len);
					esp_err_t errRc = ::esp_ble_gatts_send_response(
							gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
					if (errRc != ESP_OK) {
						ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d", errRc);
					}
				}
			} // ESP_GATTS_READ_EVT
			break;
		} // ESP_GATTS_READ_EVT
		default: {
			break;
		}
	}// switch event
} // handleGATTServerEvent


/**
 * @brief Set the handle of this descriptor.
 * Set the handle of this descriptor to be the supplied value.
 * @param [in] handle The handle to be associated with this descriptor.
 * @return N/A.
 */
void cBtCharDescriptor::setHandle(uint16_t handle) {
	ESP_LOGD(LOG_TAG, ">> setHandle(0x%.2x): Setting descriptor handle to be 0x%.2x", handle, handle);
	m_handle = handle;
	ESP_LOGD(LOG_TAG, "<< setHandle()");
} // setHandle


/**
 * @brief Set the value of the descriptor.
 * @param [in] data The data to set for the descriptor.
 * @param [in] length The length of the data in bytes.
 */
void cBtCharDescriptor::setValue(uint8_t* data, size_t length) {
	if (length > ESP_GATT_MAX_ATTR_LEN) {
		ESP_LOGE(LOG_TAG, "Size %d too large, must be no bigger than %d", length, ESP_GATT_MAX_ATTR_LEN);
		return;
	}
	m_value.attr_len = length;
	memcpy(m_value.attr_value, data, length);
} // setValue


/**
 * @brief Set the value of the descriptor.
 * @param [in] value The value of the descriptor in string form.
 */
void cBtCharDescriptor::setValue(std::string value) {
	setValue((uint8_t *)value.data(), value.length());
} // setValue


/**
 * @brief Return a string representation of the descriptor.
 * @return A string representation of the descriptor.
 */
std::string cBtCharDescriptor::toString() {
	std::stringstream stringstream;
	stringstream << std::hex << std::setfill('0');
	stringstream << "UUID: " << uuid.toString() + ", handle: 0x" << std::setw(2) << m_handle;
	return stringstream.str();
} // toString


