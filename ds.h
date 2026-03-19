struct touch_point {
    uint8_t contact;
    union {
        uint8_t raw[3];
        struct {
            uint32_t x : 12; 
            uint32_t y : 12;
        } __attribute__((packed)); 
    };
} __attribute__((packed));

union ds_buttons {
    uint32_t raw;
    struct {
        /* 按照 DualSense 硬件实际顺序定义位宽 */
        uint32_t dpad      : 4;  // 方向键 (枚举值)
        uint32_t x         : 1;  // Xbox X
        uint32_t a         : 1;  // Xbox A
        uint32_t b         : 1;  // Xbox B
        uint32_t y         : 1;  // Xbox Y
        uint32_t lb        : 1;  // L1
        uint32_t rb        : 1;  // R1
        uint32_t lt        : 1;  // L2 (数字)
        uint32_t rt        : 1;  // R2 (数字)
        uint32_t back      : 1;  // Create
        uint32_t start     : 1;  // Options
        uint32_t ls        : 1;  // L3
        uint32_t rs        : 1;  // R3
        uint32_t ps     : 1;  // PS
        uint32_t touchpad  : 1;  // Pad Click
        uint32_t mute      : 1;  // Mic
        uint32_t reserved  : 13; // 剩余位填充
    } __attribute__((packed)) ;
};

struct dualsense_input_report {
    uint8_t ls_x, ls_y;
    uint8_t rs_x, rs_y;
    uint8_t lt, rt;
    uint8_t seq_number;
    union ds_buttons buttons;
    uint32_t reserved;
    /* Motion sensors */
    uint16_t gyro_x; 
    uint16_t gyro_y; 
    uint16_t gyro_z; 
    uint16_t accel_x;
    uint16_t accel_y;
    uint16_t accel_z;
    uint32_t sensor_timestamp;
    uint8_t reserved2;
    /* Touchpad */
    struct touch_point points_1;
    struct touch_point points_2;
    uint8_t reserved3[12];
    uint8_t status;
    uint8_t reserved4[10];
} __attribute__((packed));