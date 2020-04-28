/*
 * cBluetooth.h
 *
 *  Created on: 8.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 *      Bluetooth hardware C++ helpers
 */

#ifndef COMPONENTS_M_BT_CBLUETOOTH_H_
#define COMPONENTS_M_BT_CBLUETOOTH_H_

#include "../../main/common/cBaseTask.h"
#include "../components/m_bt/cBtDevice.h"
#include "../components/m_bt/cBtServer.h"
#include "../components/m_bt/BLE2902.h"

#define BT_CLIENT_INACTIVITY_TIMEOUT_MINUTES			3	// client was connected but no activity anymore
#define BT_CLIENT_CALIBRATION_TIMEOUT_MINUTES			5	// client was performing calibration (G1 devices) but no activity anymore
#define BT_SERVER_EXISTANCE_TIMEOUT_MINUTES				3	// client was not connected, server lifetime since creation

class cBluetooth {
	cBtServer btSrv;
	cBtDevice btDev;
	bool bWasDisconnect;
public:
	cBluetooth();
	~cBluetooth();
	void Init();
	bool IsTimeToDestroy();
	bool IsClientConnected(){
		return btSrv.getConnectedCount() > 0;
	}

	bool IsSrvInit(){
		return btSrv.getGattsIf() != -1;
	}

	bool IsNeedRecreation(){ // due to unknown reasons for difficulties to reconnect reported by Reza (not approved by tests), we need to recreate BT object after the user has disconnected
		return bWasDisconnect;
	}

private:
	bool client_was_connected(void){
		return btSrv.last_client_active_t > 0;
	}

	bool calibration_ongoing(void);

	bool client_inactivity_timeout(uint32_t to_minutes){
		uint32_t time_inactive_ms = cBaseTask::GetTickCount() - btSrv.last_client_active_t;
		return time_inactive_ms > to_minutes * 60000;
	}

	bool server_existance_timeout(uint32_t to_minutes){
		uint32_t time_existance_ms = cBaseTask::GetTickCount() - btSrv.server_create_t;
		return time_existance_ms > to_minutes * 60000;
	}

	void cancel_calibration(void);
};

#endif /* COMPONENTS_M_BT_CBLUETOOTH_H_ */
