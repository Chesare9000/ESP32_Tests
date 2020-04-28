/*
 * cUart.h
 *
 *  Created on: 07.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 *      UART hardware shell class
 */

#ifndef COMPONENTS_M_UART_CUART_H_
#define COMPONENTS_M_UART_CUART_H_

#include "../../main/common/cBaseTask.h"
#include <stdint.h>
#include <vector>

#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "soc/uart_struct.h"

/**
 * - port: user defined
 * - rx buffer: on
 * - tx buffer: on
 * - flow control: off
 * - event queue: on
 * - pin assignment: txd(default), rxd(default)
 */

// helper for extraction dangerous events from uart even queue
class cUartEventChecker:public cBaseTask{
	uart_port_t uartPort;
public:
	QueueHandle_t uart_ev_queue; // UART event queue
	bool bWasError;
	cUartEventChecker(const uart_port_t aPort): uartPort(aPort){

	}
	void Start(){
		bWasError = false;
		TaskCreate("UartEventChecker", 3, 1024);
	}

private:
	void TaskHandler();
};


// UART C++ shell
class cUart: public cBaseTask {
	cMutex mux; // object mutex
	int uartBufSize; // size of UART buffers in bytes
	uart_port_t uartPort;
	cUartEventChecker *pEv;
	std::vector<uint8_t> TxBuf, RxBuf;
	uint32_t lastRxTick; // last data timestamp
public:

	cUart();
	~cUart();
	// (re)initialize uart and start data handling
	void Init(uart_port_t aPort);
	// add the outgoing data
	bool Write(void *from, int bytes);

	// get the count of new received bytes
	int RxDataReadyBytes()const;

	// check for incoming data
	uint32_t RxDataReadyTicks()const;

	// read received data
	int Read(void *to, int bytes);

private:
	void TaskHandler();
};

#endif /* COMPONENTS_M_UART_CUART_H_ */
