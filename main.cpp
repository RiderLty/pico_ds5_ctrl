/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pico/cyw43_arch.h"

#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"

#include "utils.h"
#include "bt.h"
#include "ds.h"
#include "tusb_ctrl.h"


#define UART_ID uart1
#define BAUD_RATE 115200
// 常用组合: TX=4/RX=5, TX=8/RX=9, TX=20/RX=21
#define UART_TX_PIN 4
#define UART_RX_PIN 5

static uint8_t ready = 0;

enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

char display_buffer[500];
static struct dualsense_input_report ds , ds_last;
void display_dsinfo(){
  sprintf(display_buffer, "x:%03d y:%03d rx:%03d ry:%03d lt:%03d rt:%03d a:%d b:%d y:%d x:%d lb:%d rb:%d lt:%d rt:%d back:%d start:%d ls:%d rs:%d ps:%d touchpad:%d mute:%d dpad:%d gyro_x:%08d gyro_y:%08d gyro_z:%08d accel_x:%08d accel_y:%08d accel_z:%08d sensor_timestamp:%08d touchpad: points_1[%08b]:%06d,%06d points_2[%08b]:%06d,%06d\r\n\0", ds.ls_x, ds.ls_y, ds.rs_x, ds.rs_y, ds.lt, ds.rt, ds.buttons.a, ds.buttons.b, ds.buttons.y, ds.buttons.x, ds.buttons.lb, ds.buttons.rb, ds.buttons.lt, ds.buttons.rt, ds.buttons.back, ds.buttons.start, ds.buttons.ls, ds.buttons.rs, ds.buttons.ps, ds.buttons.touchpad, ds.buttons.mute, ds.buttons.dpad, ds.gyro_x, ds.gyro_y, ds.gyro_z, ds.accel_x, ds.accel_y, ds.accel_z, ds.sensor_timestamp, ds.points_1.contact, ds.points_1.x, ds.points_1.y, ds.points_2.contact, ds.points_2.x, ds.points_2.y);
  uart_puts(UART_ID, display_buffer);
}

void debug(char *fmt, ...){
  char buffer[500];
  va_list args;
  va_start(args, fmt);
  vsprintf(buffer, fmt, args);
  va_end(args);
  uart_puts(UART_ID, buffer);
}

auto_init_mutex(my_mutex);

void on_bt_data(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    if (channel == INTERRUPT && data[1] == 0x31) {

      mutex_enter_blocking(&my_mutex);
      memcpy(&ds, data + 3, sizeof(ds));
      mutex_exit(&my_mutex);
    }
}

void on_bt_event(BT_EVENT event) {
    if (event == BT_CONNECTED) {
        debug("[Main] Bluetooth Connected\n");
        ready = 1;
    } else if (event == BT_DISCONNECTED) {
        debug("[Main] Bluetooth Disconnected\n");
        ready = 0;
    }
}

#define DPAD_UP 0x01
#define DPAD_RIGHT 0x02
#define DPAD_DOWN 0x04
#define DPAD_LEFT 0x08

static uint8_t last_id1 = 0xFF;
static uint8_t last_id2 = 0xFF;

void mouse_keyboard_ctr_taskl() { // 鼠标键盘控制任务
    uint32_t counter = 0;
    #define AXIS_CENTER 128 // 中心位置
    #define AXIS_DEAD_ZONE 6 // 死区
    #define REPORT_RATE 100 // 报告率
    #define ls_map_wheel_speed 0.0005f // 滚轮映射速度系数
    #define rs_map_mouse_speed 0.4f // 鼠标映射速度系数
    #define touchpad_map_wheel_speed 0.03f // 触摸板映射速度系数
    #define touchpad_map_mouse_speed 1.0f // 触摸板映射速度系数
    const uint64_t INTERVAL_US = 100000 / REPORT_RATE;
    absolute_time_t next_run_time = get_absolute_time();
    float current_speed_x = 0;
    float current_speed_y = 0;
    float current_speed_wheel = 0;
    float x_move = 0.0f;
    float y_move = 0.0f;
    float wheel_move = 0.0f;

    int32_t ls_x = 0;
    int32_t ls_y = 0;
    int32_t rs_x = 0;
    int32_t rs_y = 0;

    while (1) {
        if (ready) {
            counter++;
            // absolute_time_t start_time = get_absolute_time();
            mutex_enter_blocking( & my_mutex);
            // absolute_time_t mutex_enter_time = get_absolute_time();
            /*----------滚轮部分----------*/
            if (((ds.ls_y - AXIS_CENTER) * (ds.ls_y - AXIS_CENTER) < AXIS_DEAD_ZONE * AXIS_DEAD_ZONE)) {
                ls_y = 0;
                current_speed_wheel = 0.0f;
            } else {
                ls_y = (int32_t) ds.ls_y;
                current_speed_wheel = (ls_y - AXIS_CENTER) * (ls_y - AXIS_CENTER) * ls_map_wheel_speed / 128.0f;
                wheel_move -= (ls_y > AXIS_CENTER ? current_speed_wheel : -current_speed_wheel);
            }
            /*----------鼠标部分----------*/
            if (((ds.rs_x - AXIS_CENTER) * (ds.rs_x - AXIS_CENTER) + (ds.rs_y - AXIS_CENTER) * (ds.rs_y - AXIS_CENTER) < AXIS_DEAD_ZONE * AXIS_DEAD_ZONE)) {
                current_speed_x = 0.0f;
                current_speed_y = 0.0f;
            } else {
                rs_x = (int32_t) ds.rs_x;
                rs_y = (int32_t) ds.rs_y;
                current_speed_x = float((rs_x - AXIS_CENTER) * (rs_x - AXIS_CENTER)) * rs_map_mouse_speed / 128.0f;
                current_speed_y = float((rs_y - AXIS_CENTER) * (rs_y - AXIS_CENTER)) * rs_map_mouse_speed / 128.0f;
                x_move += (rs_x > AXIS_CENTER ? current_speed_x : -current_speed_x);
                y_move += (rs_y > AXIS_CENTER ? current_speed_y : -current_speed_y);
                // debug("current_speed_x:%.2f current_speed_y:%.2f\n", current_speed_x, current_speed_y);
            }
            /*----------按键部分----------*/
            if(ds.buttons.rt && !ds_last.buttons.rt){
                hid_mouse_button_down(MouseBtnLeft);
            }
            if(!ds.buttons.rt && ds_last.buttons.rt){
                hid_mouse_button_up(MouseBtnLeft);
            }
            if(ds.buttons.lt && !ds_last.buttons.lt){
                hid_mouse_button_down(MouseBtnRight);
            }
            if(!ds.buttons.lt && ds_last.buttons.lt){
                hid_mouse_button_up(MouseBtnRight);
            }
            if(ds.buttons.a && !ds_last.buttons.a){
                hid_key_down(KeyEnter);
            }
            if(!ds.buttons.a && ds_last.buttons.a){
                hid_key_up(KeyEnter);
            }
            if(ds.buttons.b && !ds_last.buttons.b){
                hid_key_down(KeyEscape);
            }
            if(!ds.buttons.b && ds_last.buttons.b){
                hid_key_up(KeyEscape);
            }
            if(ds.buttons.touchpad && !ds_last.buttons.touchpad){
                hid_mouse_button_down(MouseBtnLeft);
            }
            if(!ds.buttons.touchpad && ds_last.buttons.touchpad){
                hid_mouse_button_up(MouseBtnLeft);
            }
            /*----------DPAD部分----------*/
            uint8_t dapd_now = 0x098C46231ULL >> (ds.buttons.dpad * 4) & 0x0f;
            uint8_t dapd_last = 0x098C46231ULL >> (ds_last.buttons.dpad * 4) & 0x0f;
            uint8_t pressed = dapd_now & ~dapd_last;
            uint8_t released = ~dapd_now & dapd_last;
            if(pressed & DPAD_UP){
              hid_key_down(KeyUp);
            }
            if(released & DPAD_UP){
              hid_key_up(KeyUp);
            }
            if(pressed & DPAD_RIGHT){
              hid_key_down(KeyRight);
            }
            if(released & DPAD_RIGHT){
              hid_key_up(KeyRight);
            }
            if(pressed & DPAD_DOWN){
              hid_key_down(KeyDown);
            }
            if(released & DPAD_DOWN){
              hid_key_up(KeyDown);
            }
            if(pressed & DPAD_LEFT){
              hid_key_down(KeyLeft);
            }
            if(released & DPAD_LEFT){
              hid_key_up(KeyLeft);
            }
            /*----------触摸板部分----------*/
            bool state1 = (ds.points_1.contact & 0x80) == 0;
            uint8_t id1 = ds.points_1.contact & 0x7f;
            bool state2 = (ds.points_2.contact & 0x80) == 0;
            uint8_t id2 = ds.points_2.contact & 0x7f;
            bool last_state1 = (ds_last.points_1.contact & 0x80) == 0;
            uint8_t l_id1 = ds_last.points_1.contact & 0x7f;
            bool last_state2 = (ds_last.points_2.contact & 0x80) == 0;
            uint8_t l_id2 = ds_last.points_2.contact & 0x7f;
            if (state1 && state2) {
                if (last_state1 && last_state2 && id1 == l_id1 && id2 == l_id2) {
                    float current_mid_y = (ds.points_1.y + ds.points_2.y) / 2.0f;
                    float last_mid_y = (ds_last.points_1.y + ds_last.points_2.y) / 2.0f;
                    wheel_move -= (last_mid_y - current_mid_y) * touchpad_map_wheel_speed;
                } else {
                    // 不执行任何位移计算
                }
            }

            // B. 单指模式：只有第一点或第二点在触摸（处理释放第一根手指的情况）
            else if (state1 ^ state2) { // 使用异或，确保只有一个点
                if (state1) {
                    // 只有第一点在，且上一帧第一点也在，且 ID 没变，且上一帧没有第二点干扰
                    if (last_state1 && !last_state2 && id1 == l_id1) {
                        x_move += (float)(ds.points_1.x - ds_last.points_1.x) * touchpad_map_mouse_speed;
                        y_move += (float)(ds.points_1.y - ds_last.points_1.y) * touchpad_map_mouse_speed;
                    }
                } 
                else if (state2) {
                    // 只有第二点在（比如第一根手指抬起了），逻辑同上
                    if (last_state2 && !last_state1 && id2 == l_id2) {
                        x_move += (float)(ds.points_2.x - ds_last.points_2.x) * touchpad_map_mouse_speed;
                        y_move += (float)(ds.points_2.y - ds_last.points_2.y) * touchpad_map_mouse_speed;
                    }
                }
                wheel_move = 0;
            }
            else {
            }
            /*----------OVER----------*/
            memcpy( & ds_last, & ds, sizeof(ds));
            mutex_exit( & my_mutex);
            int8_t report_x = (int8_t) x_move;
            int8_t report_y = (int8_t) y_move;
            int8_t report_wheel = (int8_t) wheel_move;
            if (report_x != 0 || report_y != 0 || report_wheel != 0) {
                int32_t count = hid_mouse_move(report_x, report_y, report_wheel);
                debug("(%03d,%03d)[%d,%d]\n", report_x, report_y, count, absolute_time_diff_us(next_run_time, get_absolute_time()));
                // debug("counter:%d & waitcount:%d\n", counter, count);
                // tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, report_x, report_y, 0, 0);
                x_move -= (float) report_x;
                y_move -= (float) report_y;
                wheel_move -= (float) report_wheel;
            }
            // absolute_time_t finish_time = get_absolute_time();
            // debug("mutex_enter_time:%d exec_time:%d\n", absolute_time_diff_us(start_time, mutex_enter_time), absolute_time_diff_us(finish_time, mutex_enter_time));
        }
        next_run_time = delayed_by_us(next_run_time, INTERVAL_US);
        busy_wait_until(next_run_time);
    }
}
/*------------- MAIN -------------*/
int main(void)
{
  board_init();
  uart_init(UART_ID, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
  uart_set_hw_flow(UART_ID, false, false);
  uart_puts(UART_ID, "port open\r\n");
  tusb_ctrl_init();
  bt_init();
  bt_register_data_callback(on_bt_data);
  bt_register_event_callback(on_bt_event);
  multicore_launch_core1(mouse_keyboard_ctr_taskl);
  while (1)
  {
    cyw43_arch_poll();
    tud_task(); // tinyusb device task
  }
}