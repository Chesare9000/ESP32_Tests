/*
 * cMqttClient.h
 *
 *  Created on: 22.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#ifndef COMPONENTS_M_MQTT_CMQTTCLIENT_H_
#define COMPONENTS_M_MQTT_CMQTTCLIENT_H_
#include <string>
extern "C"{
	#include "include/mqtt.h"
}


class cMqttClient; // forward declaration

// Inherit it to process callbacks
class cMqttCallbacks{
public:
	virtual void OnConnected(cMqttClient *pCaller, mqtt_event_data_t *params){}
	virtual void OnDisconnected(cMqttClient *pCaller, mqtt_event_data_t *params){}
	virtual void OnSubscribe(cMqttClient *pCaller, mqtt_event_data_t *params){}
	virtual void OnPublish(cMqttClient *pCaller, mqtt_event_data_t *params){}
	virtual void OnData(cMqttClient *pCaller, mqtt_event_data_t *params)=0;
};

class cMqttClient {
	static cMqttClient *pInst; // pointer to the active instance, for callbacks
	cMqttCallbacks *m_callbacks; // pointer to the callbacks class instance
	mqtt_client *m_client; // real library client instance, use it as read-only
	mqtt_settings core_settings; // mqtt client core settings
	uint32_t lastDataT; // timestamp of the last exchange
	uint32_t connect_startT; // timestamp of the last connection attempt (to diagnose server not available state)
	bool bConnected; // defined by callbacks
	bool bStopByUser; // flag that stop was forced by the user, not by the connection error
	int iDisconnectCnt; // counter of disconnects
public:
	void *pOwner; // used by the owner object
	void SetCallbacks(cMqttCallbacks *callbacks){m_callbacks = callbacks;}
	mqtt_client* GetClientCore(){return m_client;}
	bool Start(const std::string &srv_host, const uint16_t srv_port, const std::string &client_id,
			const std::string &username, const std::string &password,
			const std::string &lwt_topic, const std::string &lwt_message, const bool bForceTLS = false);
	void Stop();
	bool Publish(const std::string &topic, const std::string &data, uint8_t qos, uint8_t retain);
	bool Subscribe(const std::string &topic, uint8_t qos);
	bool UnSubscribe(const std::string &topic);
	// connection to the server state
	bool IsConnected();
	// how long there was no data exchange
	uint32_t NoExchangeTms()const;
	// how long we can't connect to a server
	uint32_t NoConnectionTms()const;
	cMqttClient();
	~cMqttClient();
private:
	void onDisconnected(mqtt_event_data_t *params);
	static void connected_cb(mqtt_client *self, mqtt_event_data_t *params);
	static void disconnected_cb(mqtt_client *self, mqtt_event_data_t *params);
	static void subscribe_cb(mqtt_client *self, mqtt_event_data_t *params);
	static void publish_cb(mqtt_client *self, mqtt_event_data_t *params);
	static void data_cb(mqtt_client *self, mqtt_event_data_t *params);
};

#endif /* COMPONENTS_M_MQTT_CMQTTCLIENT_H_ */
