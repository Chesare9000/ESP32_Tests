/*
 * cUart.cpp
 *
 *  Created on: 07.09.2017 (c) EmSo
 *      Author: D. Pavlenko
 */

#include "../m_uart/cUart.h"
#include <string.h>
#include "sdkconfig.h"

static const char *TAG = "uart_events";

cUart::cUart() : uartBufSize(1024), pEv(nullptr), lastRxTick(0) {
	// start data processing task
	TaskCreate("cUartTask", 1, 2048);
}

cUart::~cUart() {
	TaskDelete();
	if(pEv) delete pEv;
}


void cUart::Init(uart_port_t aPort){
	if(pEv){ // reinit
		uart_driver_delete(uartPort);
		delete pEv;
		pEv = nullptr;
	}

	uartPort = aPort;
	RxBuf.clear();
	TxBuf.clear();

	uart_config_t uart_config = {
			.baud_rate = 115200,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
			.rx_flow_ctrl_thresh = 122,
			false
	};
	//Set UART parameters
	uart_param_config(aPort, &uart_config);
	//Set UART log level
	esp_log_level_set(TAG, ESP_LOG_INFO);
	// prepare event checker

	pEv = new cUartEventChecker(aPort);
	//Install UART driver, and get the queue.
	uart_driver_install(aPort, uartBufSize * 2, uartBufSize * 2, 10, &pEv->uart_ev_queue, 0);

	//Set UART pins (using UART0 default pins ie no changes.)
	uart_set_pin(aPort, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	//Set uart pattern detect function.
	//uart_enable_pattern_det_intr(EX_UART_NUM, '+', 3, 10000, 10, 10);

	//Create a task to handler UART event from ISR
	pEv->Start();
}

void cUart::TaskHandler(){
	//process data
	while(true) {
		esp_task_wdt_reset();
		if(!pEv){
			vTaskDelay( pdMS_TO_TICKS( 5 ) ); // just wait for initialization complete
			continue;
		}

		if(pEv->bWasError){ // process errors
			vTaskDelay( pdMS_TO_TICKS( 20 ) ); // insert a little pause
			// try to reinit the uart driver
			Init(uartPort);
			continue;
		}

		// process RX
		size_t buffered_size;
		if(uart_get_buffered_data_len(uartPort, &buffered_size) != ESP_OK){
			pEv->bWasError = true;
			continue;
		}

		if(buffered_size > 0){
			int cursz = RxBuf.size();
			if(cursz < 4096){ // prevent overflow
				cAutoLock lk(mux);
				RxBuf.resize(cursz + buffered_size);
				uint8_t* pbuf = &RxBuf[cursz];
				int len = uart_read_bytes(uartPort, pbuf, buffered_size, 1);
				if(len != buffered_size){
					// error
					pEv->bWasError = true;
				}
				lastRxTick = xTaskGetTickCount(); // data timestamp
			}else // RxBuf overflow
				uart_flush(uartPort);
		}

		int txlen = TxBuf.size();
		// process wait
		if(txlen == 0 && buffered_size == 0){
			// nothing to do
			vTaskDelay( pdMS_TO_TICKS(1) );
			continue;
		}

		// process TX
		if(txlen){
			cAutoLock lk(mux); // protect from other tasks intervention
			if(uart_wait_tx_done(uartPort, pdMS_TO_TICKS(10)) != ESP_OK) // we are busy (or error)
				continue;

			int txbytes = txlen < 256 ? txlen : 256; // to protect UART FIFO from overflow
			txbytes = uart_write_bytes(uartPort, (char*)&TxBuf[0], txbytes);
			if(txbytes < 0)
				pEv->bWasError = true;
			else{ // drop used data
				if(txlen == txbytes){
					TxBuf.clear();
				}else{
					memmove(&TxBuf[0], &TxBuf[txbytes], txlen - txbytes);
					TxBuf.resize(txlen - txbytes);
				}
			}
		} // TX

	} // while(true);
}


bool cUart::Write(void *from, int bytes){
	if(!from || bytes <= 0) // nothing to do
		return true;
	if(TxBuf.size() > 4096) // prevent from overflow
		return false;
	cAutoLock lk(mux);
	int cursz = TxBuf.size();
	TxBuf.resize(cursz + bytes);
	memcpy(&TxBuf[cursz], from, bytes);
	return true;
}

// get the count of new received bytes
int cUart::RxDataReadyBytes()const{
	return RxBuf.size();
}

// check for incoming data
uint32_t cUart::RxDataReadyTicks()const{
	return RxBuf.size() ? xTaskGetTickCount() - lastRxTick : 0;
}

// read received data
int cUart::Read(void *to, int bytes){
	cAutoLock lk(mux);
	int cursz = RxBuf.size();
	if(!cursz || !to || !bytes)
		return 0;
	if(bytes > cursz)
		bytes = cursz;
	memcpy(to, &RxBuf[0], bytes);
	if(bytes == cursz)
		RxBuf.clear();
	else{
		memmove(&RxBuf[0], &RxBuf[bytes], cursz - bytes);
		RxBuf.resize(cursz - bytes);
	}
	return bytes;
}

//==============================================================

void cUartEventChecker::TaskHandler(){
	// process uart events here
	esp_task_wdt_delete(handle); // disable task watchdog because this task may wait very long periods
	uart_event_t event;
	for(;;) {
		//Waiting for UART event.
		if(xQueueReceive(uart_ev_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
			//ESP_LOGI(TAG, "uart[%d] event:", uartPort);
			switch(event.type) {
			//Event of UART receving data
			//We'd better handler data event fast, there would be much more data events than
			//            other types of events. If we take too much time on data event, the queue might
			//            be full.
			//            in this example, we don't process data in event, but read data outside.
			case UART_DATA:
				// nothing interesting
				//uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
				break;
				//Event of HW FIFO overflow detected
			case UART_FIFO_OVF:
				bWasError = true;
				ESP_LOGI(TAG, "hw fifo overflow\n");
				//If fifo overflow happened, you should consider adding flow control for your application.
				//We can read data out out the buffer, or directly flush the rx buffer.
				uart_flush(uartPort);
				break;
				//Event of UART ring buffer full
			case UART_BUFFER_FULL:
				bWasError = true;
				ESP_LOGI(TAG, "ring buffer full\n");
				//If buffer full happened, you should consider increasing your buffer size
				//We can read data out out the buffer, or directly flush the rx buffer.
				uart_flush(uartPort);
				break;
				//Event of UART RX break detected
			case UART_BREAK:
				ESP_LOGI(TAG, "uart rx break\n");
				break;
				//Event of UART parity check error
			case UART_PARITY_ERR:
				bWasError = true;
				ESP_LOGI(TAG, "uart parity error\n");
				break;
				//Event of UART frame error
			case UART_FRAME_ERR:
				bWasError = true;
				ESP_LOGI(TAG, "uart frame error\n");
				break;
				//UART_PATTERN_DET
			case UART_PATTERN_DET:
				bWasError = true;
				ESP_LOGI(TAG, "uart pattern detected\n");
				break;
				//Others
			default:
				ESP_LOGI(TAG, "uart event type: %d\n", event.type);
				break;
			}
		}
	}
}
