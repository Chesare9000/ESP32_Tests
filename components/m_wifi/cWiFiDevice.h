/*
 * cWiFiDevice.h
 *
 *  Created on: 12.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 *  WiFi device incapsulation C++ helper classes
 */

#ifndef COMPONENTS_M_WIFI_CWIFIDEVICE_H_
#define COMPONENTS_M_WIFI_CWIFIDEVICE_H_
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_err.h"

enum class eWiFiState{e_disconnected, e_busy, e_connected, e_failed};

class cWiFiDevice {
	static int ref_cnt; // references counter
	static cWiFiDevice *pActiveInst; // pointer to the active instance
	static bool b_tcp_adapter_was_init; // flag
	int start_counter;
	int cur_connect_try;
public:
	int ConnectTryCount; // how many attempts to connect is allowed, default is 3
	static eWiFiState CurrentState; // check this to discover how our device is feeling itself
	cWiFiDevice();
	~cWiFiDevice();
	// Initialize and start STA (client)
	void Start(const char* ssid, const char * pass);
	// stop WiFi processing and the device
	void Stop();

	bool IsConnectionFinished();

private:
	esp_err_t event_handler(void *ctx, system_event_t *event);
	static esp_err_t event_handler_stub(void *ctx, system_event_t *event){
		if(pActiveInst)
			return pActiveInst->event_handler(ctx, event);
		return 0;
	}
};

#endif /* COMPONENTS_M_WIFI_CWIFIDEVICE_H_ */
