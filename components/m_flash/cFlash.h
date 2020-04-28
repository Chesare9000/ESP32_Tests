/*
 * cFlash.h
 *
 *  Created on: 8.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 *  Non Volatile Storage (flash memory) helpers
 */

#ifndef COMPONENTS_M_FLASH_CFLASH_H_
#define COMPONENTS_M_FLASH_CFLASH_H_

#include "../../main/common/cBaseTask.h"
#include <nvs.h>
#include <string>
#include <vector>

class cFlash {
public:
	cFlash(const std::string &storageName, nvs_open_mode openMode = NVS_READWRITE);
	~cFlash();

	bool Commit();
	bool EraseAll();
	bool Erase(const std::string &key);
	std::string GetVal(const std::string &key);
	bool SetVal(const std::string &key, const std::string &val);
	bool GetVal(const std::string &key, std::vector<uint8_t> &ret);
	bool SetVal(const std::string &key, const std::vector<uint8_t> &val);

	bool IsHandleOk()const;
private:
	std::string m_name;
	nvs_handle m_handle;
};

#endif /* COMPONENTS_M_FLASH_CFLASH_H_ */
