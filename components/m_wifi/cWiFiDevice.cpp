/*
 * cWiFiDevice.cpp
 *
 *  Created on: 12.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "cWiFiDevice.h"

#include <string.h>

#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

static const char* TAG = "cWiFiDevice";

cWiFiDevice* cWiFiDevice::pActiveInst = nullptr;
bool cWiFiDevice::b_tcp_adapter_was_init = false;
eWiFiState cWiFiDevice::CurrentState = eWiFiState::e_disconnected;
int cWiFiDevice::ref_cnt(0);

cWiFiDevice::cWiFiDevice():start_counter(0) {
	if(ref_cnt == 0){
		pActiveInst = this;
		if(!b_tcp_adapter_was_init){
			tcpip_adapter_init(); // do it only once
			b_tcp_adapter_was_init = true;
			// APPLICATION !!! event handler
			ESP_ERROR_CHECK( esp_event_loop_init(event_handler_stub, NULL) );
		}
	}
	ref_cnt++;
	ConnectTryCount = 5;
	cur_connect_try = 0;
}

cWiFiDevice::~cWiFiDevice() {
	ref_cnt--;
	if(ref_cnt == 0){
		Stop();
		pActiveInst = nullptr;
	}
}


bool cWiFiDevice::IsConnectionFinished(){
	return CurrentState != eWiFiState::e_busy;
}


// Initialize and start STA (client)
void cWiFiDevice::Start(const char* ssid, const char* pass){
	if(CurrentState == eWiFiState::e_connected || CurrentState == eWiFiState::e_busy){
		// already connected
		ESP_LOGD(TAG, "WiFi is busy or already connected");
		return;
	}
	if(CurrentState != eWiFiState::e_disconnected)
		Stop(); // restart

	if(strlen(ssid) < 2)
	{
		ESP_LOGE(TAG, "WiFi SSID %s is too short", ssid);
		CurrentState = eWiFiState::e_failed;
		cur_connect_try = 100;
		return;
	}

	CurrentState = eWiFiState::e_busy;
	ESP_LOGD(TAG, ">> Start");

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

	wifi_config_t wifi_config;
	memset(&wifi_config, 0, sizeof(wifi_config));
	strcpy((char*)wifi_config.sta.ssid, ssid);
	strcpy((char*)wifi_config.sta.password, pass);

	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s PASS %s", wifi_config.sta.ssid, wifi_config.sta.password);
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "SmartRing")); // Reza: Change IPv4/IPv6 hostname to “LivySmartRing”
	ESP_ERROR_CHECK( esp_wifi_start() );
	esp_wifi_set_ps(WIFI_PS_NONE); // set power saving mode
	ESP_LOGD(TAG, "<< Start");
	cur_connect_try = 1;
}

// stop WiFi processing and the device
void cWiFiDevice::Stop(){
	// cleanup
	CurrentState = eWiFiState::e_disconnected;
	//ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
	ESP_ERROR_CHECK(esp_wifi_stop());
	//ESP_ERROR_CHECK(esp_wifi_deinit());
}

esp_err_t cWiFiDevice::event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) {
	case SYSTEM_EVENT_STA_START:
		ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "SmartRing")); // Reza: Change DNS name to SmartRing. Currently the DNS name is espressif
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		CurrentState = eWiFiState::e_connected;
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		// try to reconnect automatically
		if(cur_connect_try <= ConnectTryCount && CurrentState != eWiFiState::e_disconnected){
			cur_connect_try ++;
			esp_wifi_connect();
			CurrentState = eWiFiState::e_busy;
		}
		else{
			CurrentState = eWiFiState::e_disconnected;
		}
		break;
	default:
		break;
	}
	return ESP_OK;
}


