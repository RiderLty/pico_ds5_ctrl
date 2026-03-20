//
// Created by awalol on 2026/3/4.
//

#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>
#include <vector>

enum CHANNEL_TYPE {
    INTERRUPT,
    CONTROL
};

enum BT_EVENT {
    BT_CONNECTED,
    BT_DISCONNECTED
};

typedef void (*bt_data_callback_t)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len);
typedef void (*bt_event_callback_t)(BT_EVENT event);

int bt_init();
void bt_register_data_callback(bt_data_callback_t callback);
void bt_register_event_callback(bt_event_callback_t callback);
void bt_send_packet(uint8_t *data, uint16_t len);
void bt_send_control(uint8_t *data, uint16_t len);
void bt_write(uint8_t* data,uint16_t len);
std::vector<uint8_t> get_feature_data(uint8_t reportId,uint16_t len);
void init_feature();
void bt_disconnect_device();


#endif //DS5_BRIDGE_BT_H