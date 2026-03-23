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
#include "hardware/watchdog.h"
#include "bsp/board_api.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
extern "C" {
    #define delete delete_func 
    #include "kvstore.h"
    #undef delete 
}

#include "utils.h"
#include "bt.h"
#include "ds.h"
#include "tusb_ctrl.h"


#define UART_ID uart1
#define BAUD_RATE 115200
// 常用组合: TX=4/RX=5, TX=8/RX=9, TX=20/RX=21
#define UART_TX_PIN 4
#define UART_RX_PIN 5

static volatile uint8_t ready = 0;
static volatile uint32_t core1_last_heartbeat = 0;
static struct dualsense_input_report ds ,ds_now, ds_last;
// char display_buffer[500];
// void display_dsinfo(){
//   sprintf(display_buffer, "x:%03d y:%03d rx:%03d ry:%03d lt:%03d rt:%03d a:%d b:%d y:%d x:%d lb:%d rb:%d lt:%d rt:%d back:%d start:%d ls:%d rs:%d ps:%d touchpad:%d mute:%d dpad:%d gyro_x:%08d gyro_y:%08d gyro_z:%08d accel_x:%08d accel_y:%08d accel_z:%08d sensor_timestamp:%08d touchpad: points_1[%08b]:%06d,%06d points_2[%08b]:%06d,%06d\r\n\0", ds.ls_x, ds.ls_y, ds.rs_x, ds.rs_y, ds.lt, ds.rt, ds.buttons.a, ds.buttons.b, ds.buttons.y, ds.buttons.x, ds.buttons.lb, ds.buttons.rb, ds.buttons.lt, ds.buttons.rt, ds.buttons.back, ds.buttons.start, ds.buttons.ls, ds.buttons.rs, ds.buttons.ps, ds.buttons.touchpad, ds.buttons.mute, ds.buttons.dpad, ds.gyro_x, ds.gyro_y, ds.gyro_z, ds.accel_x, ds.accel_y, ds.accel_z, ds.sensor_timestamp, ds.points_1.contact, ds.points_1.x, ds.points_1.y, ds.points_2.contact, ds.points_2.x, ds.points_2.y);
//   uart_puts(UART_ID, display_buffer);
// }

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

void set_speed(float s1, float s2, float s3, float s4){
    float check_sum = s1 + s2 + s3 + s4;
    char buffer[sizeof(float)*5];
    memcpy(buffer, &s1, sizeof(float));
    memcpy(buffer + sizeof(float), &s2, sizeof(float));
    memcpy(buffer + sizeof(float) * 2, &s3, sizeof(float));
    memcpy(buffer + sizeof(float) * 3, &s4, sizeof(float));
    memcpy(buffer + sizeof(float) * 4, &check_sum, sizeof(float));
    if (kvs_set("ds_speed", buffer, sizeof(buffer)) != KVSTORE_SUCCESS){
        debug("set speed error\n");
        return;
    }
    debug("set speed success\n");
    return;
}

void get_speed(float *s1, float *s2, float *s3, float *s4){
    char buffer[sizeof(float)*5];
    size_t value_size = 0;  
    if (kvs_get("ds_speed", buffer, sizeof(buffer), &value_size) != KVSTORE_SUCCESS){
        debug("get speed error\n");
        return;
    }
    float ts1 , ts2, ts3, ts4, check_sum;
    memcpy(&ts1, buffer, sizeof(float));
    memcpy(&ts2, buffer + sizeof(float), sizeof(float));
    memcpy(&ts3, buffer + sizeof(float) * 2, sizeof(float));
    memcpy(&ts4, buffer + sizeof(float) * 3, sizeof(float));
    memcpy(&check_sum, buffer + sizeof(float) * 4, sizeof(float));
    if(check_sum != ts1 + ts2 + ts3 + ts4){
        debug("check_sum error\n");
        return;
    }
    memcpy(s1, &ts1, sizeof(float));
    memcpy(s2, &ts2, sizeof(float));
    memcpy(s3, &ts3, sizeof(float));
    memcpy(s4, &ts4, sizeof(float));
    debug("get speed success\n");
}


#define DEFAULT_LS_MAP_WHEEL_SPEED 0.064f // 默认滚轮映射速度系数
#define DEFAULT_RS_MAP_MOUSE_SPEED 8.0f // 默认鼠标映射速度系数
#define DEFAULT_TOUCHPAD_MAP_WHEEL_SPEED 0.03f // 默认触摸板映射速度系数
#define DEFAULT_TOUCHPAD_MAP_MOUSE_SPEED 1.0f // 默认触摸板映射速度系数

#define AXIS_CENTER 256 // 中心位置
#define AXIS_DEAD_ZONE 12 // 死区 是缩放过的区域
#define REPORT_RATE 1000 // 报告率

static  float ls_map_wheel_speed; // 滚轮映射速度系数
static  float rs_map_mouse_speed; // 鼠标映射速度系数
static  float touchpad_map_wheel_speed; // 触摸板映射速度系数
static  float touchpad_map_mouse_speed; // 触摸板映射速度系数
static  absolute_time_t last_write_time; // 上次写入时间
static  bool need_set_speed = false; // 是否需要写入映射速度系数
void mouse_keyboard_ctr_taskl() { // 鼠标键盘控制任务
    multicore_lockout_victim_init();
    uint32_t counter = 0;
    const uint64_t INTERVAL_US = 1000000 / REPORT_RATE;
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
        core1_last_heartbeat++;
        if (ready) {
            counter++;
            // absolute_time_t start_time = get_absolute_time();
            mutex_enter_blocking( & my_mutex);
            memcpy(&ds_now, &ds, sizeof(ds));
            mutex_exit( & my_mutex);// 在上锁状态下，复制最新的ds状态
            ls_x = (int32_t) ds_now.ls_x << 1;
            ls_y = (int32_t) ds_now.ls_y << 1;
            rs_x = (int32_t) ds_now.rs_x << 1;
            rs_y = (int32_t) ds_now.rs_y << 1;//缩放到0~512

            // absolute_time_t mutex_enter_time = get_absolute_time();
            /*----------滚轮部分----------*/
            if (((ls_x - AXIS_CENTER) * (ls_x - AXIS_CENTER) + (ls_y - AXIS_CENTER) * (ls_y - AXIS_CENTER) < AXIS_DEAD_ZONE * AXIS_DEAD_ZONE)) {
                ls_y = 0;
                current_speed_wheel = 0.0f;
            } else {
                current_speed_wheel = (ls_y - AXIS_CENTER) * (ls_y - AXIS_CENTER) * ls_map_wheel_speed / 65536.0f;
                wheel_move -= (ls_y > AXIS_CENTER ? current_speed_wheel : -current_speed_wheel);
            }
            /*----------鼠标部分----------*/
            if (((rs_x - AXIS_CENTER) * (rs_x - AXIS_CENTER) + (rs_y - AXIS_CENTER) * (rs_y - AXIS_CENTER) < AXIS_DEAD_ZONE * AXIS_DEAD_ZONE)) {
                current_speed_x = 0.0f;
                current_speed_y = 0.0f;
            } else {
                current_speed_x = float((rs_x - AXIS_CENTER) * (rs_x - AXIS_CENTER)) * rs_map_mouse_speed / 65536.0f;
                current_speed_y = float((rs_y - AXIS_CENTER) * (rs_y - AXIS_CENTER)) * rs_map_mouse_speed / 65536.0f;
                x_move += (rs_x > AXIS_CENTER ? current_speed_x : -current_speed_x);
                y_move += (rs_y > AXIS_CENTER ? current_speed_y : -current_speed_y);
                // debug("current_speed_x:%.2f current_speed_y:%.2f\n", current_speed_x, current_speed_y);
            }
            /*----------按键部分----------*/
            uint8_t dapd_now = 0x098C46231ULL >> (ds_now.buttons.dpad * 4) & 0x0f;
            uint8_t dapd_last = 0x098C46231ULL >> (ds_last.buttons.dpad * 4) & 0x0f;
            uint8_t dpad_pressed = dapd_now & ~dapd_last;
            uint8_t dpad_released = ~dapd_now & dapd_last;

            if(ds_now.buttons.mute ){//静音键按下的状态
                bool is_map_data_changed = false; // 是否更新了映射速度系数
                if(ds_now.buttons.x){
                    rs_map_mouse_speed -=0.005f;
                    rs_map_mouse_speed = rs_map_mouse_speed < 0.005f ? 0.005f : rs_map_mouse_speed;
                    debug("mouse:%.5f\n", rs_map_mouse_speed);
                    is_map_data_changed = true;
                }
                if(ds_now.buttons.b){
                    rs_map_mouse_speed +=0.005f;
                    // rs_map_mouse_speed = rs_map_mouse_speed > 127.0f ? 127.0f : rs_map_mouse_speed;
                    debug("mouse:%.5f\n", rs_map_mouse_speed);
                    is_map_data_changed = true;
                }
                if(ds_now.buttons.a){
                    ls_map_wheel_speed -=0.00005f;
                    ls_map_wheel_speed = ls_map_wheel_speed < 0.0001f ? 0.0001f : ls_map_wheel_speed;
                    debug("wheel:%.5f\n", ls_map_wheel_speed);
                    is_map_data_changed = true;
                }
                if(ds_now.buttons.y){
                    ls_map_wheel_speed +=0.00005f;
                    // ls_map_wheel_speed = ls_map_wheel_speed > 0.5f ? 0.5f : ls_map_wheel_speed;
                    debug("wheel:%.5f\n", ls_map_wheel_speed);
                    is_map_data_changed = true;
                }
                if(dapd_now & DPAD_UP){
                    touchpad_map_wheel_speed +=0.00005f;
                    // touchpad_map_wheel_speed = touchpad_map_wheel_speed > 0.5f ? 0.5f : touchpad_map_wheel_speed;
                    debug("touchpad_wheel:%.5f\n", touchpad_map_wheel_speed);
                    is_map_data_changed = true;
                }
                if(dapd_now & DPAD_DOWN){
                    touchpad_map_wheel_speed -=0.00005f;
                    touchpad_map_wheel_speed = touchpad_map_wheel_speed < 0.0001f ? 0.0001f : touchpad_map_wheel_speed;
                    debug("touchpad_wheel:%.5f\n", touchpad_map_wheel_speed);
                    is_map_data_changed = true;
                }
                if(dapd_now & DPAD_RIGHT){
                   touchpad_map_mouse_speed +=0.002f;
                //    touchpad_map_mouse_speed = touchpad_map_mouse_speed > 127.0f ? 127.0f : touchpad_map_mouse_speed;
                   debug("touchpad_mouse:%.5f\n", touchpad_map_mouse_speed);
                   is_map_data_changed = true;
                }
                if(dapd_now & DPAD_LEFT){
                   touchpad_map_mouse_speed -=0.002f;
                   touchpad_map_mouse_speed = touchpad_map_mouse_speed < 0.005f ? 0.005f : touchpad_map_mouse_speed;
                   debug("touchpad_mouse:%.5f\n", touchpad_map_mouse_speed);
                   is_map_data_changed = true;
                }
                if(!ds_now.buttons.rs && ds_last.buttons.rs){//重设摇杆控制速度
                    rs_map_mouse_speed = DEFAULT_RS_MAP_MOUSE_SPEED;
                    ls_map_wheel_speed = DEFAULT_LS_MAP_WHEEL_SPEED;
                    debug("reset mouse:%.5f wheel:%.5f\n", rs_map_mouse_speed, ls_map_wheel_speed);
                    is_map_data_changed = true;
                }
                if(!ds_now.buttons.ls && ds_last.buttons.ls){//重设触摸板控制速度
                    touchpad_map_wheel_speed = DEFAULT_TOUCHPAD_MAP_WHEEL_SPEED;
                    touchpad_map_mouse_speed = DEFAULT_TOUCHPAD_MAP_MOUSE_SPEED;
                    debug("reset touchpad mouse:%.5f wheel:%.5f\n", touchpad_map_mouse_speed, touchpad_map_wheel_speed);
                    is_map_data_changed = true;
                }
                if(!ds_now.buttons.ps && ds_last.buttons.ps ){//重启
                    debug("restarting device.\n");
                    sleep_ms(100);
                    watchdog_reboot(0, 0, 0);
                }

                if(is_map_data_changed){
                    last_write_time = get_absolute_time();
                    need_set_speed = true;
                }
            }else{
                if(ds_now.buttons.rt && !ds_last.buttons.rt){
                    hid_mouse_button_down(MouseBtnLeft);
                }
                if(!ds_now.buttons.rt && ds_last.buttons.rt){
                    hid_mouse_button_up(MouseBtnLeft);
                }
                if(ds_now.buttons.lt && !ds_last.buttons.lt){
                    hid_mouse_button_down(MouseBtnRight);
                }
                if(!ds_now.buttons.lt && ds_last.buttons.lt){
                    hid_mouse_button_up(MouseBtnRight);
                }
                if(ds_now.buttons.a && !ds_last.buttons.a){
                    hid_key_down(KeyEnter);
                }
                if(!ds_now.buttons.a && ds_last.buttons.a){
                    hid_key_up(KeyEnter);
                }
                if(ds_now.buttons.b && !ds_last.buttons.b){
                    hid_key_down(KeyEscape);
                }
                if(!ds_now.buttons.b && ds_last.buttons.b){
                    hid_key_up(KeyEscape);
                }
                if(ds_now.buttons.touchpad && !ds_last.buttons.touchpad){
                    hid_mouse_button_down(MouseBtnLeft);
                }
                if(!ds_now.buttons.touchpad && ds_last.buttons.touchpad){
                    hid_mouse_button_up(MouseBtnLeft);
                }
      
                /*----------DPAD部分----------*/
                if(dpad_pressed & DPAD_UP){
                    hid_key_down(KeyUp);
                }
                if(dpad_released & DPAD_UP){
                    hid_key_up(KeyUp);
                }
                if(dpad_pressed & DPAD_RIGHT){
                    hid_key_down(KeyRight);
                }
                if(dpad_released & DPAD_RIGHT){
                    hid_key_up(KeyRight);
                }
                if(dpad_pressed & DPAD_DOWN){
                    hid_key_down(KeyDown);
                }
                if(dpad_released & DPAD_DOWN){
                    hid_key_up(KeyDown);
                }
                if(dpad_pressed & DPAD_LEFT){
                    hid_key_down(KeyLeft);
                }
                if(dpad_released & DPAD_LEFT){
                    hid_key_up(KeyLeft);
                }
            }
            /*----------触摸板部分----------*/
            bool state1 = (ds_now.points_1.contact & 0x80) == 0;
            uint8_t id1 = ds_now.points_1.contact & 0x7f;
            bool state2 = (ds_now.points_2.contact & 0x80) == 0;
            uint8_t id2 = ds_now.points_2.contact & 0x7f;
            bool last_state1 = (ds_last.points_1.contact & 0x80) == 0;
            uint8_t l_id1 = ds_last.points_1.contact & 0x7f;
            bool last_state2 = (ds_last.points_2.contact & 0x80) == 0;
            uint8_t l_id2 = ds_last.points_2.contact & 0x7f;
            if (state1 && state2) {
                if (last_state1 && last_state2 && id1 == l_id1 && id2 == l_id2) {
                    float current_mid_y = (ds_now.points_1.y + ds_now.points_2.y) / 2.0f;
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
                        x_move += (float)(ds_now.points_1.x - ds_last.points_1.x) * touchpad_map_mouse_speed;
                        y_move += (float)(ds_now.points_1.y - ds_last.points_1.y) * touchpad_map_mouse_speed;
                    }
                } 
                else if (state2) {
                    // 只有第二点在（比如第一根手指抬起了），逻辑同上
                    if (last_state2 && !last_state1 && id2 == l_id2) {
                        x_move += (float)(ds_now.points_2.x - ds_last.points_2.x) * touchpad_map_mouse_speed;
                        y_move += (float)(ds_now.points_2.y - ds_last.points_2.y) * touchpad_map_mouse_speed;
                    }
                }
                wheel_move = 0;
            }
            else {
            }
            /*----------OVER----------*/
            while (x_move != 0.0f || y_move != 0.0f || wheel_move != 0.0f) {
                int8_t report_x = (x_move > 127.0f) ? 127 : (x_move < -127.0f) ? -127 : (int8_t)x_move;
                int8_t report_y = (y_move > 127.0f) ? 127 : (y_move < -127.0f) ? -127 : (int8_t)y_move;
                int8_t report_wheel = (wheel_move > 127.0f) ? 127 : (wheel_move < -127.0f) ? -127 : (int8_t)wheel_move;
                if (report_x != 0 || report_y != 0 || report_wheel != 0) {
                    int32_t count = hid_mouse_move(report_x, report_y, report_wheel);
                    // debug("(%d,%d)[%d]\n", report_x, report_y, count);
                    x_move -= (float)report_x;
                    y_move -= (float)report_y;
                    wheel_move -= (float)report_wheel;
                    // sleep_ms(1); 
            } else {
                break;
            }
        }
            // absolute_time_t finish_time = get_absolute_time();
            // debug("mutex_enter_time:%d exec_time:%d\n", absolute_time_diff_us(start_time, mutex_enter_time), absolute_time_diff_us(finish_time, mutex_enter_time));
        }

        memcpy( & ds_last, & ds_now, sizeof(ds));//更新上一帧状态
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
  kvs_init();
  ls_map_wheel_speed = DEFAULT_LS_MAP_WHEEL_SPEED; 
  rs_map_mouse_speed = DEFAULT_RS_MAP_MOUSE_SPEED; 
  touchpad_map_wheel_speed = DEFAULT_TOUCHPAD_MAP_WHEEL_SPEED; 
  touchpad_map_mouse_speed = DEFAULT_TOUCHPAD_MAP_MOUSE_SPEED;
  last_write_time = get_absolute_time();
  get_speed(&ls_map_wheel_speed, &rs_map_mouse_speed, &touchpad_map_wheel_speed, &touchpad_map_mouse_speed);
  debug("read ls_map_wheel_speed: %f, rs_map_mouse_speed: %f, touchpad_map_wheel_speed: %f, touchpad_map_mouse_speed: %f\n", ls_map_wheel_speed, rs_map_mouse_speed, touchpad_map_wheel_speed, touchpad_map_mouse_speed);
  tusb_ctrl_init();
  bt_init();
  bt_register_data_callback(on_bt_data);
  bt_register_event_callback(on_bt_event);
  multicore_launch_core1(mouse_keyboard_ctr_taskl);
  uint32_t core1_last_heartbeat_record = 0;
  absolute_time_t ckeck_core1_alive_time = get_absolute_time();
  while (1)
  {
    cyw43_arch_poll();
    tud_task(); // tinyusb device task
    if(need_set_speed){
        if(absolute_time_diff_us(last_write_time, get_absolute_time()) >= 3 * 1000000){//等待3s后设置速度
            debug("write ls_map_wheel_speed: %f, rs_map_mouse_speed: %f, touchpad_map_wheel_speed: %f, touchpad_map_mouse_speed: %f\n", ls_map_wheel_speed, rs_map_mouse_speed, touchpad_map_wheel_speed, touchpad_map_mouse_speed);
            multicore_lockout_start_blocking();
            set_speed(ls_map_wheel_speed, rs_map_mouse_speed, touchpad_map_wheel_speed, touchpad_map_mouse_speed);
            multicore_lockout_end_blocking();
            need_set_speed = false; 
        }
    }
    if(absolute_time_diff_us(ckeck_core1_alive_time, get_absolute_time()) >= 1000 * 200){//每隔200ms 检查core1是否存活
        if(core1_last_heartbeat_record == core1_last_heartbeat){
            debug("core1 not alive, reset it\n");
            multicore_reset_core1();
            multicore_launch_core1(mouse_keyboard_ctr_taskl);
        }
        // else{
        //     debug("core1 alive %d vs %d\n", core1_last_heartbeat_record, core1_last_heartbeat);
        // }
        core1_last_heartbeat_record = core1_last_heartbeat;
        ckeck_core1_alive_time = get_absolute_time();
    }
  }
}