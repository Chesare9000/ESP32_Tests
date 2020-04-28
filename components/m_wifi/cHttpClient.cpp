/*
 * cHttpClient.cpp
 *
 *  Created on:  30.10.2017 (c) EmSo
 *      Author: D. Pavlenko
 * TLS: Copyright (C) 2006-2015, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * TLS: Copyright (C) 2017 Evandro Luis Copercini, Apache 2.0 License.
 */

#include "cHttpClient.h"

#include "sdkconfig.h"

#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <algorithm>    // find
#include <sstream>

#include <posix/sys/socket.h>

#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>

#include <errno.h>
#include <mbedtls/base64.h>

#include "../../main/common/Utils.h"

#include <esp_log.h>
#include <esp_err.h>

static const char* TAG = "cHttpSClient";

static const char *pers = "esp32-tls";

// Split the url to parts
bool ParseUrlToParts(const std::string& uri, std::string &retHost, std::string &retPort, std::string &retPath, std::string &retQueryString, std::string &retProtocol){

	typedef std::string::const_iterator iterator_t;

	if (uri.length() == 0)
		return false;

	iterator_t uriEnd = uri.end();

	// get query start
	iterator_t queryStart = std::find(uri.begin(), uriEnd, '?');

	// protocol
	iterator_t protocolStart = uri.begin();
	iterator_t protocolEnd = std::find(protocolStart, uriEnd, ':');            //"://");

	if (protocolEnd != uriEnd)
	{
		std::string prot = &*(protocolEnd);
		if ((prot.length() > 3) && (prot.substr(0, 3) == "://"))
		{
			retProtocol = std::string(protocolStart, protocolEnd);
			protocolEnd += 3;   //      ://
		}
		else
			protocolEnd = uri.begin();  // no protocol
	}
	else
		protocolEnd = uri.begin();  // no protocol

	// host
	iterator_t hostStart = protocolEnd;
	iterator_t pathStart = std::find(hostStart, uriEnd, '/');  // get pathStart

	iterator_t hostEnd = std::find(protocolEnd,
			(pathStart != uriEnd) ? pathStart : queryStart,
					L':');  // check for port

	retHost = std::string(hostStart, hostEnd);

	// port
	if ((hostEnd != uriEnd) && ((&*(hostEnd))[0] == ':'))  // we have a port
	{
		hostEnd++;
		iterator_t portEnd = (pathStart != uriEnd) ? pathStart : queryStart;
		retPort = std::string(hostEnd, portEnd);
	}else
		retPort = retProtocol == "https" ? "443" : "80";

	// path
	if (pathStart != uriEnd)
		retPath = std::string(pathStart, queryStart);
	else
		retPath = "/";

	// query
	if (queryStart != uriEnd)
		retQueryString = std::string(queryStart, uri.end());

	return true;
}


// DNS resolve helpers
std::string HostNameToIP(const std::string &hname){
	std::string result = hname;
	bool bIp = true;
	// check for already IP or not
	for(int i = 0; i < hname.length(); i++){
		char c = hname[i];
		if(c == '.' || (c >= '0' && c <= '9'))
			continue;
		bIp = false;
		break;
	}
	if(!bIp){
		hostent *he = gethostbyname(hname.c_str());
		if(he){
			ip4_addr_t retAddr = *(ip4_addr_t *)(he->h_addr_list[0]);
			result = ip4addr_ntoa(&retAddr);
		}
	}
	ESP_LOGD(TAG, "DNS resolve: host %s -> IP %s", hname.c_str(), result.c_str());
	return result;
}


//===========================================================================================================

cHttpClient::cHttpClient(cWiFiDevice &dev):CurrentStatus(eHttpClientStatus::e_shutdown), socket_id(-1) {
	pCallbacks = nullptr;
	m_pwifi = &dev;
	bAutoCalcSha1 = false;
	bShaWasInit = false;
	b_allow_data_processing = false;
	_CA_cert = NULL;
	cli_cert = NULL;
	cli_private_key = NULL;
	bModeHttps = false;
	ssl_init();
}

cHttpClient::~cHttpClient() {
	Shutdown();
}


void cHttpClient::StartTask(){
	if(!IsTaskExists()){
		// start task
		TaskCreate("HTTP(S) Client", 5, 4096);
	}
}

bool cHttpClient::IsFailed(){
	StartTask();
	return CurrentStatus == eHttpClientStatus::e_http_failed || CurrentStatus == eHttpClientStatus::e_wifi_failed;
}

bool cHttpClient::IsReadyToGet(){
	StartTask();
	return CurrentStatus == eHttpClientStatus::e_ok || CurrentStatus == eHttpClientStatus::e_http_failed;
}

void cHttpClient::TaskHandler(){
	int rnrn(0); // to divide headers and body
	bool bSleep;
	while(true){
		bSleep = true;

		// WiFi state checkers
		if(CurrentStatus == eHttpClientStatus::e_shutdown){
			if(m_pwifi->CurrentState == eWiFiState::e_connected){
				CurrentStatus = eHttpClientStatus::e_busy_wifi; // allow to process this state and advance
			}
		}

		if(CurrentStatus == eHttpClientStatus::e_busy_wifi){
			// check WiFi
			switch(m_pwifi->CurrentState){
			case eWiFiState::e_connected:
				CurrentStatus = eHttpClientStatus::e_ok;
				break;
			case eWiFiState::e_disconnected:
			case eWiFiState::e_failed:
				CurrentStatus = eHttpClientStatus::e_wifi_failed;
				if(pCallbacks)
					pCallbacks->OnError(this);
				break;
			default:
				break;
			}
		}else if(CurrentStatus == eHttpClientStatus::e_busy_http){
			// check wifi state
			if(m_pwifi->CurrentState == eWiFiState::e_disconnected || m_pwifi->CurrentState == eWiFiState::e_failed){
				CurrentStatus = eHttpClientStatus::e_wifi_failed;
				ESP_LOGE(TAG, "WiFi unexpectedly fails!");
				if(pCallbacks)
					pCallbacks->OnError(this);
				continue;
			}

			while(!b_allow_data_processing){
				vTaskDelay(1);
				continue;
			}

			// initialize sha if required
			if(bAutoCalcSha1 && !bShaWasInit){
				mbedtls_sha1_init(&sha);
				mbedtls_sha1_starts(&sha);
				bShaWasInit = true;
			}
			// process HTTP response
			uint8_t buf[256];
			int buff_len = bModeHttps ? ssl_receive(buf, sizeof buf) : recv(socket_id, buf, sizeof buf, MSG_DONTWAIT);
			if(bModeHttps && buff_len == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY){
				// connection closed
				buff_len = 0;
			}
			if (buff_len < 0) { /*receive error*/
				if(GetTickCount() - recv_start_t > 25000){
					// timeout
					ESP_LOGE(TAG, "Error: Timeout happens while receiving data!");
					CurrentStatus = eHttpClientStatus::e_http_failed;
					if(pCallbacks){
						if(body_data.size()){
							// finalize sha
							sha_finish();
							pCallbacks->OnResponseComplete(this);
						}
						pCallbacks->OnError(this);
					}
					continue;
				}
				if((bModeHttps && (buff_len == MBEDTLS_ERR_SSL_WANT_READ || buff_len == MBEDTLS_ERR_SSL_WANT_WRITE)) ||
						(!bModeHttps && errno == EAGAIN)){
					vTaskDelay(0); // give the time to other tasks
					continue;
				}
				ESP_LOGE(TAG, "Error: receive data error! errno=%d", buff_len);
				CurrentStatus = eHttpClientStatus::e_http_failed;
				if(pCallbacks)
					pCallbacks->OnError(this);

			} else if (buff_len > 0) {
				bSleep = false;
				if(b_resp_body_start){ // headers are already received
					int cursz = body_data.size();
					if(cursz > 10000){
						ESP_LOGE(TAG, "Body data overflow - dropping! Use callbacks, please!");
						cursz = 0;
					}
					body_data.resize(cursz + buff_len);
					memcpy(&body_data[cursz], buf, buff_len);
					// add this data to sha
					if(bShaWasInit){
						mbedtls_sha1_update(&sha, buf, buff_len);
					}
					if(pCallbacks){// process user callback
						pCallbacks->OnNewData(this);
					}
				}else{ // check for headers
					for(int ic = 0; ic < buff_len; ic++){
						uint8_t c = buf[ic];
						if(b_resp_body_start){
							// put this bytes to the body_data
							int cursz = body_data.size();
							body_data.resize(cursz + buff_len - ic);
							memcpy(&body_data[cursz], buf + ic, buff_len - ic);
							// add this data to sha
							if(bShaWasInit){
								mbedtls_sha1_update(&sha, buf + ic, buff_len - ic);
							}
							if(pCallbacks){	// process callback
								pCallbacks->OnNewData(this);
							}
							break;
						}else{ // append to headers data
							char tc[]={c,0};
							if(c == '\r' || c == '\n'){ // search for \r\n\r\n
								rnrn ++;
								if(rnrn == 4){
									b_resp_body_start = true;
									rnrn = 0;
									if(pCallbacks)
										pCallbacks->OnHeaders(this);
								}
							}else {
								// reset
								rnrn = 0;
							}
							headers_data += tc;
						}
					}
				}
				// update our watchdog
				recv_start_t = GetTickCount();
			} else if (buff_len == 0) {  /*packet is over*/
				CurrentStatus = eHttpClientStatus::e_ok;
				if(bModeHttps){
					stop_ssl_socket();
				}
				else{
					close(socket_id);
					socket_id = -1;
				}
				// finish sha processing
				sha_finish();
				if(pCallbacks)
					pCallbacks->OnResponseComplete(this);

				ESP_LOGD(TAG, "Connection closed, all packets was received");
			}
		}
		if(bSleep) vTaskDelay(10);
	}
}

bool cHttpClient::HttpsRequest(const std::string &req_body, const std::string &server_host, const std::string &server_port, bool bCheckOnly){
	ESP_LOGD(TAG, ">> HttpsRequest");
	if(!server_host.length() || !server_port.length()){
		ESP_LOGE(TAG, "<< HttpsRequest, wrong host and|or port!");
		return false;
	}

	if(socket_id != -1){
		if(bModeHttps)
			stop_ssl_socket();
		else
			close(socket_id);
	}
	bModeHttps = true;

	// cleanup
	headers_data.clear();
	body_data.clear();
	// maybe we already have wifi connection?
	CurrentStatus = eHttpClientStatus::e_busy_wifi;
	b_allow_data_processing = !pCallbacks; // deny implicit data processing when callbacks are in use

	StartTask();

	cTimeout to(10000);
	while(CurrentStatus == eHttpClientStatus::e_busy_wifi && !to()){ // wait for WiFi status
		vTaskDelay(10);
	}

	// check our state
	bool bCanTry = CurrentStatus == eHttpClientStatus::e_ok || CurrentStatus == eHttpClientStatus::e_http_failed;
	if(!bCanTry){
		ESP_LOGE(TAG, "My Status is not good enough to execute HTTP request");
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	}

	CurrentStatus = eHttpClientStatus::e_ok;

	ESP_LOGD(TAG, "HTTPS Request: %s", req_body.c_str());

	// connect to http server
	int http_connect_flag = start_ssl_client(server_host, server_port);
	if (http_connect_flag == -1) {
		if(socket_id >= 0)
			close(socket_id);
		socket_id = -1;
		CurrentStatus = eHttpClientStatus::e_http_failed;
		ESP_LOGE(TAG, "<< HttpSRequest Connect to the server failed! errno=%d", errno);
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	} else {
		ESP_LOGD(TAG, "Connected to the server OK");
		//return true;
	}

	if(bCheckOnly){ // no need to make request, check only if server is available
		stop_ssl_socket();
		ESP_LOGD(TAG, "<< HttpSRequest CheckOnly OK");
		return true;
	}

	//Send the request
	int res = ssl_send_data((uint8_t*)req_body.c_str(), req_body.size());
	if (res == -1) {
		CurrentStatus = eHttpClientStatus::e_http_failed;
		ESP_LOGE(TAG, "<< HttpSRequest Send request to the server failed");
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	} else {
		ESP_LOGD(TAG, "Send request to the server succeeded");
	}

	recv_start_t = GetTickCount();
	b_resp_body_start = false;
	CurrentStatus = eHttpClientStatus::e_busy_http;
	ESP_LOGD(TAG, "<< HttpSRequest OK");
	return true;
}


bool cHttpClient::HttpRequest(const std::string &req_body, const std::string &server_host, const std::string &server_port, bool bCheckOnly){
	ESP_LOGD(TAG, ">> HttpRequest");
	if(!server_host.length() || !server_port.length()){
		ESP_LOGE(TAG, "<< HttpRequest, wrong host and|or port!");
		return false;
	}

	if(socket_id != -1){
		if(bModeHttps)
			stop_ssl_socket();
		else
			close(socket_id);
	}
	bModeHttps = false;
	// cleanup
	headers_data.clear();
	body_data.clear();
	// maybe we already have wifi connection?
	CurrentStatus = eHttpClientStatus::e_busy_wifi;
	b_allow_data_processing = !pCallbacks; // deny implicit data processing when callbacks are in use

	StartTask();

	cTimeout to(10000);
	while(CurrentStatus == eHttpClientStatus::e_busy_wifi && !to()){ // wait for WiFi status
		vTaskDelay(10);
	}

	// check our state
	bool bCanTry = CurrentStatus == eHttpClientStatus::e_ok || CurrentStatus == eHttpClientStatus::e_http_failed;
	if(!bCanTry){
		ESP_LOGE(TAG, "My Status is not good enough to execute HTTP request");
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	}

	CurrentStatus = eHttpClientStatus::e_ok;

	ESP_LOGD(TAG, "HTTP Request: %s", req_body.c_str());

	struct sockaddr_in sock_info;

	socket_id = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_id == -1) {
		ESP_LOGE(TAG, "<< HttpRequest Create socket failed!");
		CurrentStatus = eHttpClientStatus::e_http_failed;
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	}

	// set connect info
	memset(&sock_info, 0, sizeof(struct sockaddr_in));
	sock_info.sin_family = AF_INET;
	sock_info.sin_addr.s_addr = inet_addr(HostNameToIP(server_host).c_str());
	sock_info.sin_port = htons((uint16_t)atoi(server_port.c_str()));

	// connect to http server
	int http_connect_flag = connect(socket_id, (sockaddr *)&sock_info, sizeof(sock_info));
	if (http_connect_flag == -1) {
		close(socket_id);
		socket_id = -1;
		CurrentStatus = eHttpClientStatus::e_http_failed;
		ESP_LOGE(TAG, "<< HttpRequest Connect to the server failed! errno=%d", errno);
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	} else {
		ESP_LOGD(TAG, "Connected to the server OK");
		//return true;
	}

	if(bCheckOnly){ // no need to make request, check only if server is available
		close(socket_id);
		socket_id = -1;
		ESP_LOGD(TAG, "<< HttpRequest CheckOnly OK");
		return true;
	}

	//Send the request
	int res = send(socket_id, req_body.c_str(), req_body.size(), 0);
	if (res == -1) {
		CurrentStatus = eHttpClientStatus::e_http_failed;
		ESP_LOGE(TAG, "<< HttpRequest Send request to the server failed");
		if(pCallbacks)
			pCallbacks->OnError(this);
		return false;
	} else {
		ESP_LOGD(TAG, "Send request to the server succeeded");
	}

	recv_start_t = GetTickCount();
	b_resp_body_start = false;
	CurrentStatus = eHttpClientStatus::e_busy_http;
	ESP_LOGD(TAG, "<< HttpRequest OK");
	return true;
}

bool cHttpClient::HttpGet(const std::string& uri, const std::string &more_headers, bool bCheckOnly){
	// parse URL to the parts
	ESP_LOGD(TAG, "HttpGet URL %s", uri.c_str());
	//http://server:port/file
	std::string QueryString, Path, Protocol, Host, Port;
	if(!ParseUrlToParts(uri, Host, Port, Path, QueryString, Protocol))
		return false;

	std::string req_body =
			"GET " + Path + (QueryString.length() ? "?" + QueryString : "") + " HTTP/1.1\r\n"
			"Host: " + Host + "\r\n" +
			more_headers + (more_headers.length() ? "\r\n" : "") +
			"Connection: close\r\n\r\n";
	return Protocol == "https" || Port == "443" ?  HttpsRequest(req_body, Host, Port, bCheckOnly) : HttpRequest(req_body, Host, Port, bCheckOnly);
}

bool cHttpClient::HttpPost(const std::string& uri, const std::string &data, const std::string &more_headers){
	/*
	POST /foo.php?someVar=123&anotherVar=TRUE HTTP/1.1
	Host: example.org
	Content-Type: application/x-www-form-urlencoded
	Content-Length: 7

	foo=bar
	 */
	// parse URL to the parts
	ESP_LOGD(TAG, "HttpPost URL: %s \n data: %s", uri.c_str(), data.c_str());
	//http://server:port/file
	std::string QueryString, Path, Protocol, Host, Port;
	if(!ParseUrlToParts(uri, Host, Port, Path, QueryString, Protocol))
		return false;

	std::string req_body =
			"POST " + Path + (QueryString.length() ? "?" + QueryString : "") + " HTTP/1.1\r\n"
			"Host: " + Host + "\r\n"+
			more_headers + (more_headers.length() ? "\r\n" : "") +
			"Content-Length: " + IntToStr(data.length()) + "\r\n"
			"Connection: close\r\n\r\n" + data;

	return Protocol == "https" || Port == "443" ?  HttpsRequest(req_body, Host, Port) : HttpRequest(req_body, Host, Port);
}


void cHttpClient::Shutdown(){
	ESP_LOGD(TAG, ">> Shutdown");

	if(bShaWasInit){
		// deinitialize sha internals
		mbedtls_sha1_free(&sha);
		bShaWasInit = false;
	}

	if(CurrentStatus == eHttpClientStatus::e_shutdown){
		ESP_LOGD(TAG, "<< Shutdown - already down!");
		return;
	}

	TaskDelete();
	if(socket_id != -1){
		if(bModeHttps){
			stop_ssl_socket();
		}else{
			close(socket_id);
			socket_id = -1;
		}
	}
	headers_data.clear();
	body_data.clear();
	CurrentStatus = eHttpClientStatus::e_shutdown;
	ESP_LOGD(TAG, "<< Shutdown");
}


void cHttpClient::sha_finish(){
	if(bShaWasInit){
		unsigned char output[20], base64_out[64];
		mbedtls_sha1_finish(&sha, output);
		// deinitialize sha internals
		mbedtls_sha1_free(&sha);
		bShaWasInit = false;
		// convert sha1 to the hex string
		std::stringstream ss;
		ss << std::hex;
		for(int i = 0; i < sizeof output; i++)
			ss << std::setw(2) << std::setfill('0') << (int)output[i];
		// now get the base64-encoded string
		size_t out_len(0);
		mbedtls_base64_encode(base64_out, sizeof base64_out,  &out_len,
				(uint8_t*)ss.str().c_str(), ss.str().size());

		base64_out[out_len] = 0; // null-terminate
		DataSha1Hash = (char*) base64_out;
	}
}


// SSL =======================================

// helper
static int handle_error(int err)
{
	if(err == -30848){
		return err;
	}
#ifdef MBEDTLS_ERROR_C
	char error_buf[100];
	mbedtls_strerror(err, error_buf, 100);
	ESP_LOGE(TAG, "%s", error_buf);
#endif
	ESP_LOGE(TAG, "MbedTLS message code: %d", err);
	return err;
}


void cHttpClient::ssl_init()
{
	mbedtls_ssl_init(&ssl_ctx);
	mbedtls_ssl_config_init(&ssl_conf);
	mbedtls_ctr_drbg_init(&drbg_ctx);
}


int cHttpClient::start_ssl_client(const std::string& host, const std::string& port)
{
	char buf[512];
	int ret, flags, timeout;
	int enable = 1;
	ESP_LOGD(TAG, "Free heap before TLS %u", xPortGetFreeHeapSize());

	ESP_LOGD(TAG, "Starting socket");

	if(socket_id != -1)
		close(socket_id);

	socket_id = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_id < 0) {
		ESP_LOGE(TAG, "ERROR opening socket");
		CurrentStatus = eHttpClientStatus::e_http_failed;
		if(pCallbacks)
			pCallbacks->OnError(this);
		return socket_id;
	}


	struct sockaddr_in sock_info;


	// set connect info
	memset(&sock_info, 0, sizeof(struct sockaddr_in));
	sock_info.sin_family = AF_INET;
	sock_info.sin_addr.s_addr = inet_addr(HostNameToIP(host).c_str());
	sock_info.sin_port = htons((uint16_t)atoi(port.c_str()));

	if (lwip_connect(socket_id, (struct sockaddr *)&sock_info, sizeof(sock_info)) == 0) {
		timeout = 30000;
		lwip_setsockopt(socket_id, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		lwip_setsockopt(socket_id, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		lwip_setsockopt(socket_id, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
		lwip_setsockopt(socket_id, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
	} else {
		ESP_LOGE(TAG, "Connect to Server failed!");
		return -1;
	}

	fcntl( socket_id, F_SETFL, fcntl( socket_id, F_GETFL, 0 ) | O_NONBLOCK );

	ESP_LOGI(TAG, "Seeding the random number generator");
	mbedtls_entropy_init(&entropy_ctx);

	ret = mbedtls_ctr_drbg_seed(&drbg_ctx, mbedtls_entropy_func,
			&entropy_ctx, (const unsigned char *) pers, strlen(pers));
	if (ret < 0) {
		return handle_error(ret);
	}

	ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

	if ((ret = mbedtls_ssl_config_defaults(&ssl_conf,
			MBEDTLS_SSL_IS_CLIENT,
			MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
		return handle_error(ret);
	}

	// MBEDTLS_SSL_VERIFY_REQUIRED if a CA certificate is defined and
	// MBEDTLS_SSL_VERIFY_NONE if not.

	if (_CA_cert != NULL) {
		ESP_LOGI(TAG, "Loading CA cert");
		mbedtls_x509_crt_init(&ca_cert);
		mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
		ret = mbedtls_x509_crt_parse(&ca_cert, (const unsigned char *)_CA_cert, strlen(_CA_cert) + 1);
		mbedtls_ssl_conf_ca_chain(&ssl_conf, &ca_cert, NULL);
		//mbedtls_ssl_conf_verify(&ssl_client->ssl_ctx, my_verify, NULL );
		if (ret < 0) {
			return handle_error(ret);
		}
	} else {
		mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
		//ESP_LOGD(TAG, "WARNING: Use certificates for a more secure communication!");
	}

	if (cli_cert != NULL && cli_private_key != NULL) { // it is not our case ;)
		mbedtls_x509_crt_init(&client_cert);
		mbedtls_pk_init(&client_key);

		ESP_LOGI(TAG, "Loading CRT cert");

		ret = mbedtls_x509_crt_parse(&client_cert, (const unsigned char *)cli_cert, strlen(cli_cert) + 1);
		if (ret < 0) {
			return handle_error(ret);
		}

		ESP_LOGI(TAG, "Loading private key");
		ret = mbedtls_pk_parse_key(&client_key, (const unsigned char *)cli_private_key, strlen(cli_private_key) + 1, NULL, 0);

		if (ret != 0) {
			return handle_error(ret);
		}

		mbedtls_ssl_conf_own_cert(&ssl_conf, &client_cert, &client_key);
	}

	ESP_LOGI(TAG, "Setting hostname for TLS session...");

	// Hostname set here should match CN in server certificate
	if((ret = mbedtls_ssl_set_hostname(&ssl_ctx, host.c_str())) != 0){
		return handle_error(ret);
	}

	mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &drbg_ctx);

	if ((ret = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf)) != 0) {
		return handle_error(ret);
	}

	mbedtls_ssl_set_bio(&ssl_ctx, &socket_id, mbedtls_net_send, mbedtls_net_recv, NULL );

	ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

	while ((ret = mbedtls_ssl_handshake(&ssl_ctx)) != 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			return handle_error(ret);
		}
	}


	if (cli_cert != NULL && cli_private_key != NULL) {
		ESP_LOGD(TAG, "Protocol is %s Ciphersuite is %s", mbedtls_ssl_get_version(&ssl_ctx), mbedtls_ssl_get_ciphersuite(&ssl_ctx));
		if ((ret = mbedtls_ssl_get_record_expansion(&ssl_ctx)) >= 0) {
			ESP_LOGD(TAG, "Record expansion is %d", ret);
		} else {
			ESP_LOGW(TAG, "Record expansion is unknown (compression)");
		}
	}

	ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

	if ((flags = mbedtls_ssl_get_verify_result(&ssl_ctx)) != 0) {
		memset(buf, 0, sizeof(buf));
		mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
		ESP_LOGE(TAG, "Failed to verify peer certificate! verification info: %s", buf);
		stop_ssl_socket();  //It's not safe to continue
		return handle_error(ret);
	} else {
		ESP_LOGI(TAG, "Certificate verified.");
	}

	if (_CA_cert != NULL) {
		mbedtls_x509_crt_free(&ca_cert);
	}

	if (cli_cert != NULL) {
		mbedtls_x509_crt_free(&client_cert);
	}

	if (cli_private_key != NULL) {
		mbedtls_pk_free(&client_key);
	}

	ESP_LOGD(TAG, "Free heap after TLS %u", xPortGetFreeHeapSize());

	return socket_id;
}


void cHttpClient::stop_ssl_socket()
{
	ESP_LOGI(TAG, "Cleaning SSL connection.");

	if (socket_id >= 0) {
		close(socket_id);
		socket_id = -1;
	}

	mbedtls_ssl_free(&ssl_ctx);
	mbedtls_ssl_config_free(&ssl_conf);
	mbedtls_ctr_drbg_free(&drbg_ctx);
	mbedtls_entropy_free(&entropy_ctx);
}


int cHttpClient::ssl_data_to_read()
{
	int ret, res;
	ret = mbedtls_ssl_read(&ssl_ctx, NULL, 0);
	res = mbedtls_ssl_get_bytes_avail(&ssl_ctx);
	if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret < 0) {
		return handle_error(ret);
	}

	return res;
}


int cHttpClient::ssl_send_data(const uint8_t *data, int len)
{
	ESP_LOGD(TAG, "Writing HTTPS request...");
	int ret = -1;

	while ((ret = mbedtls_ssl_write(&ssl_ctx, data, len)) <= 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			return handle_error(ret);
		}else{
			vTaskDelay(0);
		}
	}
	return ret;
}


int cHttpClient::ssl_receive(uint8_t *data, int length)
{
	return mbedtls_ssl_read(&ssl_ctx, data, length);
}


