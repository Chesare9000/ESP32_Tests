/*
 * cHttpClient.h
 *
 *  Created on:  30.10.2017 (c) EmSo
 *  Based on cHttpClient 12.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

// HTTPS protocol client support

#ifndef COMPONENTS_M_WIFI_CHTTPSCLIENT_H_
#define COMPONENTS_M_WIFI_CHTTPSCLIENT_H_

#include "cWiFiDevice.h"
#include "../../main/common/cBaseTask.h"

#define MBEDTLS_SHA1_ALT // only this configuration is working
#include <mbedtls/sha1.h>

#include <vector>
#include <string>

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

class cHttpClient;

// Http client events callbacks shell
class cHttpCallbacks{
public:
	virtual void OnError(cHttpClient *pCaller){}
	virtual void OnHeaders(cHttpClient *pCaller){}
	virtual void OnNewData(cHttpClient *pCaller){}
	virtual void OnResponseComplete(cHttpClient *pCaller){}
};

// possible states
enum class eHttpClientStatus{e_shutdown, e_busy_wifi, e_wifi_failed, e_busy_http, e_ok, e_http_failed};

// C++ shell for HTTP and HTTPS communication
// don't try to create this objects on stack - they are too large (or use stack not smaller than 16 kB)
// also use calling task's stack size at least 8 kB when executing HTTPS requests
class cHttpClient : private cBaseTask {
	cWiFiDevice *m_pwifi; // pointer to the device instance
	mbedtls_sha1_context sha; // context for data hash calculation
	bool bShaWasInit; // internal sha state flag

	// SSL context
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;

    mbedtls_ctr_drbg_context drbg_ctx;
    mbedtls_entropy_context entropy_ctx;

    mbedtls_x509_crt ca_cert;
    mbedtls_x509_crt client_cert;
    mbedtls_pk_context client_key;

    const char *_CA_cert;
    const char *cli_cert;
    const char *cli_private_key;
public:
	bool bAutoCalcSha1; // set to true to automatically calculate sha1 hash, default is false
	std::string DataSha1Hash; // contains last base64(sha1(body_data)) if bAutoCalcSha1 is true
	cHttpCallbacks *pCallbacks; // if you want to use callbacks
	eHttpClientStatus CurrentStatus; // track this status to discover what happens
	std::string headers_data; // headers from a response
	std::vector <uint8_t> body_data; // response body data
	cHttpClient(cWiFiDevice &dev);
	~cHttpClient();

	// Attention!!! this methods is for making request, you have to wait for body polling  IsFailed() and IsReadyToGet()
	bool HttpGet(const std::string& uri, const std::string &more_headers, bool bCheckOnly = false);
	bool HttpPost(const std::string& uri, const std::string &data, const std::string &more_headers);
	void AllowDataProcessing() // call this together with callbacks use to begin data retrieval, after HttpGet or HttpPost call
	{b_allow_data_processing = true;}

	// cleanup
	void Shutdown();
	// status checkers
	bool IsFailed();
	bool IsReadyToGet();


private:
	int socket_id;
	bool b_resp_body_start;
	unsigned int recv_start_t;
	bool b_allow_data_processing;
	bool bModeHttps;
	void TaskHandler();
	void StartTask();

	// make a request
	bool HttpsRequest(const std::string &req_body, const std::string &server_host, const std::string &server_port, bool bCheckOnly = false);
	bool HttpRequest(const std::string &req_body, const std::string &server_host, const std::string &server_port, bool bCheckOnly = false);
	void sha_finish();
	// SSL methods
	void ssl_init();
	int start_ssl_client(const std::string& host, const std::string& port);
	void stop_ssl_socket();
	int ssl_data_to_read();
	int ssl_send_data(const uint8_t *data, int len);
	int ssl_receive(uint8_t *data, int length);
};

#endif /* COMPONENTS_M_WIFI_CHTTPSCLIENT_H_ */
