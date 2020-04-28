/*
 * BLEUUID.h
 *
 *  Created on: Jun 21, 2017
 *      Author: kolban
 *      Modified by D. Pavlenko @ EmSo 2017.09.11
 */

#ifndef COMPONENTS_CPP_UTILS_BLEUUID_H_
#define COMPONENTS_CPP_UTILS_BLEUUID_H_
#include "sdkconfig.h"
#include <esp_gatt_defs.h>
#include <string>

// A model of a BLE UUID.
class BLEUUID {
public:
	BLEUUID(const std::string &uuid);
	BLEUUID(uint16_t uuid);
	BLEUUID(uint32_t uuid);
	BLEUUID(const esp_bt_uuid_t &uuid);
	BLEUUID(uint8_t* pData, size_t size, bool msbFirst);
	BLEUUID(const esp_gatt_srvc_id_t &srcvId);
	BLEUUID();
	bool           equals(const BLEUUID &uuid) const;
	bool operator ==(const BLEUUID &uuid) const{
		return equals(uuid);
	}
	int bitSize(); // Get the number of bits in this uuid.
	esp_bt_uuid_t* getNative();
	BLEUUID        to128() ;
	std::string    toString() const;

private:
	esp_bt_uuid_t m_uuid;
	bool          m_valueSet;
}; // BLEUUID
#endif /* CONFIG_BT_ENABLED */
