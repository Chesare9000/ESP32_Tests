/*
 * cMqttClient.cpp
 *
 *  Created on: 22.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "cMqttClient.h"
#include <esp_log.h>
#include <esp_err.h>
#include "../../main/common/cBaseTask.h"

static const char *TAG = "cMqttClient";
cMqttClient *cMqttClient::pInst = nullptr;

cMqttClient::cMqttClient():m_callbacks(nullptr), m_client(nullptr), lastDataT(0), connect_startT(0) {
	pInst = this;
	bConnected = false;
}

cMqttClient::~cMqttClient() {
	Stop();
	pInst = nullptr;
}

bool cMqttClient::IsConnected(){
	if(!m_client)
		return false;
	return m_client->socket != -1 && bConnected;
}

bool cMqttClient::Start(const std::string &srv_host, const uint16_t srv_port, const std::string &client_id,
		const std::string &username, const std::string &password,
		const std::string &lwt_topic, const std::string &lwt_message, const bool bForceTLS){
	ESP_LOGD(TAG, ">>Start()");
	Stop();
	if(!m_callbacks)
		return false;

	bStopByUser = false;
	iDisconnectCnt = 0;

	//core_settings
	memset(&core_settings, 0, sizeof core_settings); // clear

	srv_host.copy(core_settings.host, sizeof(core_settings.host) - 1);
	core_settings.port = srv_port;
	core_settings.b_secure = srv_port == 8883 || bForceTLS;
	client_id.copy(core_settings.client_id, sizeof(core_settings.client_id) - 1);

	username.copy(core_settings.username, sizeof(core_settings.username) - 1);
	password.copy(core_settings.password, sizeof(core_settings.password) - 1);

	lwt_topic.copy(core_settings.lwt_topic, sizeof(core_settings.lwt_topic) - 1);
	lwt_message.copy(core_settings.lwt_msg, sizeof(core_settings.lwt_msg) - 1);

	core_settings.auto_reconnect = true;
	core_settings.clean_session = 0;
	core_settings.keepalive = 120;
	core_settings.lwt_qos = 0;
	core_settings.lwt_retain = 0;
	core_settings.lwt_msg_len = lwt_message.length();
	core_settings.connected_cb = connected_cb;
	core_settings.data_cb = data_cb;
	core_settings.disconnected_cb = disconnected_cb;
	core_settings.publish_cb = publish_cb;
	core_settings.subscribe_cb = subscribe_cb;

	ESP_LOGD(TAG, "MQTT trying to connect %s:%d with client_id `%s`, username `%s`, password `%s`",
			core_settings.host, core_settings.port, core_settings.client_id, core_settings.username, core_settings.password);

	connect_startT = cBaseTask::GetTickCount();
	m_client = mqtt_start(&core_settings);
	ESP_LOGD(TAG, "<<Start()");
	return m_client != nullptr;
}

void cMqttClient::Stop(){
	ESP_LOGD(TAG, ">>Stop()");
	bStopByUser = true;
	if(m_client){
		ESP_LOGD(TAG, "MQTT is going down");
		mqtt_stop();
		m_client = nullptr;
		bConnected = false;
	}
	ESP_LOGD(TAG, "<<Stop()");
}

bool cMqttClient::Publish(const std::string &topic, const std::string &data, uint8_t qos, uint8_t retain){
	if(!m_client)
		return false;
	ESP_LOGD(TAG, "MQTT Publish to topic: %s\r\nmessage: %s", topic.c_str(), data.c_str());
	mqtt_publish(m_client, topic.c_str(), data.c_str(), data.length() + 1, qos, retain);
	lastDataT = cBaseTask::GetTickCount();
	return true;
}

bool cMqttClient::Subscribe(const std::string &topic, uint8_t qos){
	if(!m_client)
		return false;
	mqtt_subscribe(m_client, topic.c_str(), qos);
	return true;
}

bool cMqttClient::UnSubscribe(const std::string &topic){
	if(!m_client)
		return false;
	mqtt_unsubscribe(m_client, topic.c_str());
	return true;
}

uint32_t cMqttClient::NoExchangeTms()const{
	if(!lastDataT)
		return 0;
	return cBaseTask::GetTickCount() - lastDataT;
}

uint32_t cMqttClient::NoConnectionTms()const{
	//ESP_LOGD(TAG, "connect_startT %d; m_client %d, bConnected %d", connect_startT, (int)m_client, bConnected);
	if(!connect_startT)
		return 0;
	if(!m_client) // no client instance at all
		return 1000000;
	if(bConnected)// already connected
		return 0;
	return cBaseTask::GetTickCount() - connect_startT;
}

void cMqttClient::onDisconnected(mqtt_event_data_t *params){
	if(!bStopByUser){
		// this disconnection was forced by network
		if(core_settings.auto_reconnect){
			// internal mqtt library autoreconnect does not work correctly sometimes
			iDisconnectCnt ++;
			if(iDisconnectCnt > 10){ // give to the library a chance
				Stop(); // just force stop
			}else
				return;
		}
	}

	bConnected = false;
	if(m_callbacks){
		m_callbacks->OnDisconnected(this, params);
	}
}

void cMqttClient::connected_cb(mqtt_client *self, mqtt_event_data_t *params){
	if(!pInst)
		return;
	if(!pInst->m_callbacks)
		return;
	pInst->lastDataT = cBaseTask::GetTickCount();
	pInst->m_callbacks->OnConnected(pInst, params);
	pInst->bConnected = true;
}

void cMqttClient::disconnected_cb(mqtt_client *self, mqtt_event_data_t *params){
	if(!pInst)
		return;
	pInst->onDisconnected(params);
}

void cMqttClient::subscribe_cb(mqtt_client *self, mqtt_event_data_t *params){
	if(!pInst)
		return;
	if(!pInst->m_callbacks)
		return;
	pInst->lastDataT = cBaseTask::GetTickCount();
	pInst->m_callbacks->OnSubscribe(pInst, params);
}

void cMqttClient::publish_cb(mqtt_client *self, mqtt_event_data_t *params){
	if(!pInst)
		return;
	if(!pInst->m_callbacks)
		return;
	pInst->lastDataT = cBaseTask::GetTickCount();
	pInst->m_callbacks->OnPublish(pInst, params);
}

void cMqttClient::data_cb(mqtt_client *self, mqtt_event_data_t *params){
	if(!pInst)
		return;
	if(!pInst->m_callbacks)
		return;
	pInst->lastDataT = cBaseTask::GetTickCount();
	pInst->iDisconnectCnt = 0;
	pInst->m_callbacks->OnData(pInst, params);
}
