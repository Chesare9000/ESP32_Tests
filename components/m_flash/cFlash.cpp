/*
 * cFlash.cpp
 *
 *  Created on: 08.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "cFlash.h"
#include "sdkconfig.h"
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

static const char* LOG_TAG = "cFlash";
static bool bNvsWasInit = false;

cFlash::cFlash(const std::string &storageName, nvs_open_mode openMode) {
	if(!bNvsWasInit){ // init NVS only once
		ESP_ERROR_CHECK(nvs_flash_init());
		bNvsWasInit = true;
	}
	m_name = storageName;
	int res = nvs_open(m_name.c_str(), openMode, &m_handle);
	if(ESP_OK != res){
		ESP_LOGE(LOG_TAG, "nvs_open error, %d", res);
		if(res == ESP_ERR_NVS_NOT_FOUND){
			ESP_LOGW(LOG_TAG, "Missing entry for key: %s", m_name.c_str());
		}
		m_handle = -1;
	}
}

cFlash::~cFlash() {
	if(m_handle == -1) return;
	nvs_close(m_handle);
}

bool cFlash::IsHandleOk()const{
	return m_handle != -1;
}

bool cFlash::Commit(){
	if(m_handle == -1) return false;
	return nvs_commit(m_handle) == ESP_OK;
}

bool cFlash::EraseAll(){
	if(m_handle == -1) return false;
	return nvs_erase_all(m_handle) == ESP_OK;
}

bool cFlash::Erase(const std::string &key){
	if(m_handle == -1) return false;
	return nvs_erase_key(m_handle, key.c_str()) == ESP_OK;
}

std::string cFlash::GetVal(const std::string &key){
	if(key.size() > 15){
		ESP_LOGE(LOG_TAG, "Key `%s` is too long (max 15 characters)", key.c_str());
	}
	if(m_handle == -1) return "";
	size_t length;
	std::string res;
	int ires = nvs_get_str(m_handle, key.c_str(), NULL, &length);
	if(ires == ESP_OK && length > 0){
		char *data = (char *)malloc(length);
		nvs_get_str(m_handle, key.c_str(), data, &length);
		res = data;
		free(data);
	}else{
		ESP_LOGE(LOG_TAG, "nvs_get_str error %d for key %s", ires, key.c_str());
	}
	return res;
}

bool cFlash::SetVal(const std::string &key, const std::string &val){
	if(key.size() > 15){
		ESP_LOGE(LOG_TAG, "Key `%s` is too long (max 15 characters)", key.c_str());
	}
	if(m_handle == -1)
		return false;
	return nvs_set_str(m_handle, key.c_str(), val.c_str()) == ESP_OK;
}

bool cFlash::GetVal(const std::string &key, std::vector<uint8_t> &ret){
	if(key.size() > 15){
		ESP_LOGE(LOG_TAG, "Key `%s` is too long (max 15 characters)", key.c_str());
	}
	if(m_handle == -1)
		return false;
	size_t length;

	int ires = nvs_get_blob(m_handle, key.c_str(), NULL, &length);
	if(ires == ESP_OK && length > 0){
		ret.resize(length);
		nvs_get_blob(m_handle, key.c_str(), &ret[0], &length);
	}else{
		ESP_LOGE(LOG_TAG, "nvs_get_blob error %d for key `%s`", ires, key.c_str());
		return false;
	}
	return true;
}

bool cFlash::SetVal(const std::string &key, const std::vector<uint8_t> &val){
	if(key.size() > 15){
		ESP_LOGE(LOG_TAG, "Key `%s` is too long (max 15 characters)", key.c_str());
	}
	if(m_handle == -1) return false;
	//Maximum length is 1984 bytes
	if(val.size() > 1980)
		ESP_LOGE(LOG_TAG, "Value (for key `%s`) is too long (max 1980 bytes): %d bytes", key.c_str(), val.size());
	return nvs_set_blob(m_handle, key.c_str(), &val[0], val.size()) == ESP_OK;
}
