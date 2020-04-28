/*
 * BLE2902.h
 *
 *  Created on: Jun 25, 2017
 *      Author: kolban
 *      Modified by D. Pavlenko @ EmSo
 */

#ifndef COMPONENTS_CPP_UTILS_BLE2902_H_
#define COMPONENTS_CPP_UTILS_BLE2902_H_
#include "sdkconfig.h"
#include "cBtCharacteristic.h"
// Descriptor for Client Characteristic Configuration.
// This is a convenience descriptor for the Client Characteristic Configuration which has a UUID of 0x2902.
class BLE2902: public cBtCharDescriptor {
public:
	BLE2902();
	bool getNotifications();
	bool getIndications();
	void setNotifications(bool flag);
	void setIndications(bool flag);

}; // BLE2902

#endif /* COMPONENTS_CPP_UTILS_BLE2902_H_ */
