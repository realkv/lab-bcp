#ifndef __GATTC_DEMO_H__
#define __GATTC_DEMO_H__

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

typedef struct
{
	void (*give_tx_confirm)(void);
	bool (*wait_tx_confirm)(uint32_t block_time);
}ble_tx_sem;

void ble_tx_sem_register(ble_tx_sem sem);

typedef void (*connect_handle_t)(uint8_t con_id);


uint8_t get_ble_connect_id(void);

void connect_register(connect_handle_t conn, connect_handle_t disconn);

void ble_init();

bool get_ble_connect_flag(void);

int32_t ble_tx(uint8_t conn_id, uint8_t *data, uint16_t len);
void ble_rx_register(void (*rx)(uint8_t conn_id, uint8_t *data, uint16_t len));

void ble_mtu_update_cb_register(void (*mtu_update_handle)(uint8_t conn_id, uint16_t mtu));

#endif
