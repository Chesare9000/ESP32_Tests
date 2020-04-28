#ifndef _MQTT_CONFIG_H_
#define _MQTT_CONFIG_H_
#include "sdkconfig.h"
#include <stdio.h>
#include <esp_log.h>

#define CONFIG_MQTT_PROTOCOL_311 1

#define CONFIG_MQTT_PRIORITY 5
#define CONFIG_MQTT_LOG_ERROR_ON
#define CONFIG_MQTT_LOG_WARN_ON
//#define CONFIG_MQTT_LOG_INFO_ON // comment this
#define CONFIG_MQTT_RECONNECT_TIMEOUT 60
#define CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD 1024
#define CONFIG_MQTT_BUFFER_SIZE_BYTE 1024
#define CONFIG_MQTT_MAX_HOST_LEN 80
#define CONFIG_MQTT_MAX_CLIENT_LEN 64
#define CONFIG_MQTT_MAX_USERNAME_LEN 64
#define CONFIG_MQTT_MAX_PASSWORD_LEN 64
#define CONFIG_MQTT_MAX_LWT_TOPIC 32
#define CONFIG_MQTT_MAX_LWT_MSG 32


#ifdef CONFIG_MQTT_LOG_ERROR_ON
#define mqtt_error(format, ... ) ESP_LOGE("[MQTT ERROR]", format, ##__VA_ARGS__)
#else
#define mqtt_error( format, ... )
#endif
#ifdef CONFIG_MQTT_LOG_WARN_ON
#define mqtt_warn(format, ... ) ESP_LOGW( "[MQTT WARN]", format, ##__VA_ARGS__)
#else
#define mqtt_warn( format, ... )
#endif
#ifdef CONFIG_MQTT_LOG_INFO_ON
#define mqtt_info(format, ... ) ESP_LOGI( "[MQTT INFO]", format, ##__VA_ARGS__)
#else
#define mqtt_info(format, ... )
#endif

#ifndef CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD
#define CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD 1024
#endif

#endif
