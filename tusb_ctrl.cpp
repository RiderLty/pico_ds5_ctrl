#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include "hardware/uart.h"
#include <stdarg.h>
#include <stdbool.h>
#include "tusb.h" // TinyUSB 头文件
enum { ITF_NUM_HID, ITF_NUM_TOTAL };

#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_MOUSE 2



#define UART_ID uart0
#define BAUD_RATE 9600
#define UART_TX_PIN 0
#define UART_RX_PIN 1

void tusb_ctrl_init() {
  tusb_init(); // 初始化 TinyUSB 设备栈
}

void set_led_on() {
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
}
void set_led_off() {
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
}


//描述符：包含键盘和鼠标
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
  };

// 设备描述符
tusb_desc_device_t const desc_device = {.bLength =
sizeof(tusb_desc_device_t),
                                        .bDescriptorType = TUSB_DESC_DEVICE,
                                        .bcdUSB = 0x0200,
                                        .bDeviceClass = 0x00,
                                        .bDeviceSubClass = 0x00,
                                        .bDeviceProtocol = 0x00,
                                        .bMaxPacketSize0 =
                                            CFG_TUD_ENDPOINT0_SIZE,
                                        .idVendor = 0x2E8A, // Raspberry Pi
                                        .idProduct = 0x0001,
                                        .bcdDevice = 0x0100,
                                        .iManufacturer = 0x01,
                                        .iProduct = 0x02,
                                        .iSerialNumber = 0x03,
                                        .bNumConfigurations = 0x01};

// 配置描述符
uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), 0x81, CFG_TUD_HID_EP_BUFSIZE,
                       1)};

// 字符串描述符
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: 语言ID (English)
    "Raspberry Pi",             // 1: Manufacturer
    "Pico W HID Device",        // 2: Product
    "123456",                   // 3: Serials
};


// --- TinyUSB 回调函数 ---
uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  return desc_configuration;
}
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  static uint16_t _desc_str[32];
  uint8_t chr_count;
  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
      return NULL;
    const char *str = string_desc_arr[index];
    chr_count = strlen(str);
    if (chr_count > 31)
      chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++)
      _desc_str[1 + i] = str[i];
  }
  _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
  return _desc_str;
}
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
  return desc_hid_report;
}
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t
                               *buffer, uint16_t reqlen) {
  return 0;
}
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const
                           *buffer, uint16_t bufsize) {}


                           
void send_hid_key(uint8_t keycode) {
  while (!tud_hid_ready()) {
    tud_task(); // 在等待时必须保持调用 tud_task
  }
  uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
  tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, keycodes); // 按下
  sleep_ms(20); // 适当延时确保主机接收
  while (!tud_hid_ready()) {
    tud_task();
  }
  tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL); // 释放
  sleep_ms(20); // 释放后的短延时，防止字符粘连
}

void send_hello_enter() {
  send_hid_key(HID_KEY_H);
  send_hid_key(HID_KEY_E);
  send_hid_key(HID_KEY_L);
  send_hid_key(HID_KEY_L);
  send_hid_key(HID_KEY_O);
  send_hid_key(HID_KEY_ENTER);
}


static uint8_t curr_mouse_buttons = 0;
static uint8_t curr_keycodes[6] = {0};
static uint8_t curr_modifier = 0;

static void debug(char *fmt, ...){
  char buffer[500];
  va_list args;
  va_start(args, fmt);
  vsprintf(buffer, fmt, args);
  va_end(args);
  uart_puts(UART_ID, buffer);
}


static inline int32_t wait_for_hid_ready(void) {
    int32_t counter = 0 ;
    while (!tud_hid_ready()) {
        tud_task(); // 驱动 USB 协议栈，防止死锁
        counter++;
        if (counter > 1000) {
            debug("hid ready timeout\n");
            break;
        }
    }
    return counter;
}

int32_t hid_mouse_move(int8_t x, int8_t y, int8_t wheel) {
    int32_t counter = wait_for_hid_ready();
    tud_hid_mouse_report(REPORT_ID_MOUSE, curr_mouse_buttons, x, y, wheel, 0);
    return counter;
}



// 鼠标按键按下
bool hid_mouse_button_down(uint8_t button) {
    curr_mouse_buttons |= button;
    wait_for_hid_ready();
    tud_hid_mouse_report(REPORT_ID_MOUSE, curr_mouse_buttons, 0, 0, 0, 0);
    return true;
}

// 鼠标按键抬起
bool hid_mouse_button_up(uint8_t button) {
    curr_mouse_buttons &= ~button;
    wait_for_hid_ready();
    tud_hid_mouse_report(REPORT_ID_MOUSE, curr_mouse_buttons, 0, 0, 0, 0);
    return true;
}

// 键盘按键按下
bool hid_key_down(uint8_t keycode) {
    int slot = -1;
    bool already_pressed = false;

    // 扫描是否已存在或有空位
    for (int i = 0; i < 6; i++) {
        if (curr_keycodes[i] == keycode) already_pressed = true;
        if (curr_keycodes[i] == 0 && slot == -1) slot = i;
    }

    // 如果已经按下过，或者 6 个槽位已满，则直接返回不再响应
    if (already_pressed || slot == -1) return false;

    // 更新状态并发送
    curr_keycodes[slot] = keycode;
    
    wait_for_hid_ready();
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, curr_modifier, curr_keycodes);
    return true;
}

// 键盘按键抬起
bool hid_key_up(uint8_t keycode) {
    bool found = false;
    for (int i = 0; i < 6; i++) {
        if (curr_keycodes[i] == keycode) {
            curr_keycodes[i] = 0;
            found = true;
        }
    }
    // 只有状态确实发生改变时才发送报告，节省总线资源
    if (found) {
        wait_for_hid_ready();
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, curr_modifier, curr_keycodes);
    }
    return found;
}