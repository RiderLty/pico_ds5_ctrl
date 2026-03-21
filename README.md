# DS5手柄转键鼠转换器

## 项目简介

本项目使用树莓派Pico 2W将PlayStation 5的DualSense手柄（DS5）转换为键盘鼠标输出，实现通过DS5手柄控制电脑的功能。

## 硬件要求

- 树莓派Pico 2W
- PlayStation 5 DualSense手柄
- Micro USB数据线（用于连接Pico到电脑）
- 电脑（Windows、macOS或Linux）

## 软件依赖

- Pico SDK
- BTstack（用于蓝牙通信）
- TinyUSB（用于USB HID设备模拟）
- pico-kvstore（用于存储配置）

## 安装步骤

1. **克隆项目**
   ```bash
   git clone <项目地址>
   cd pico_ds5_ctrl
   ```

2. **安装依赖**
   ```bash
   mkdir lib && cd lib && git clone --depth 1 --recursive https://github.com/oyama/pico-kvstore
   ```

3. **配置Pico SDK**
   确保环境变量`PICO_SDK_PATH`已正确设置指向Pico SDK的安装路径。

4. **编译项目**
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

5. **烧录固件**
   - 将Pico 2W按住BOOTSEL按钮插入电脑
   - 将生成的`pico_ds5_ctrl.uf2`文件复制到Pico的U盘驱动器中

## 功能说明

1. **手柄控制**
   - 左摇杆：控制鼠标滚轮
   - 右摇杆：控制鼠标移动
   - R2按钮：鼠标左键
   - L2按钮：鼠标右键
   - A按钮：Enter键
   - B按钮：Escape键
   - 触摸板：鼠标左键
   - D-pad：方向键

2. **触摸板功能**
   - 单指触摸：控制鼠标移动
   - 双指触摸：控制鼠标滚轮

3. **速度调节，在按住麦克风静音按键的状态下**
   - X,B 调整右摇杆映射鼠标速度
   - A,Y 调整左摇杆映射滚轮速度
   - DPAD上下：调整触控板映射鼠标速度
   - DPAD左右：调整触控板映射滚轮速度
   - 按下右摇杆，恢复左右摇杆默认速度
   - 按下左摇杆，恢复触摸板默认映射速度
## 使用方法

1. **连接手柄**
   - 上电后，Pico 2W会自动进入蓝牙配对模式
   - 打开DS5手柄的配对模式（同时按住Share和PS按钮）
   - 等待Pico 2W与DS5手柄配对成功

2. **控制电脑**
   - 配对成功后，Pico 2W会被识别为USB键盘鼠标设备
   - 使用DS5手柄控制电脑

3. **调节速度**
   - 按特定按键组合调节鼠标和滚轮速度
   - 速度设置会自动保存到Pico的KV存储中

## 代码结构

- `main.cpp`：主程序，包含蓝牙连接、手柄数据处理和键鼠控制逻辑
- `bt.cpp`：蓝牙通信相关代码
- `bt.h`：蓝牙通信头文件
- `ds.h`：DualSense手柄数据结构定义
- `tusb_ctrl.cpp`：USB HID设备控制代码
- `tusb_ctrl.h`：USB HID设备控制头文件
- `utils.h`：工具函数

## 许可证

本项目基于MIT许可证开源，详见LICENSE.TXT文件。

## 注意事项

- 首次使用时，需要先与DS5手柄配对
- 速度设置会保存在Pico的KV存储中，重启后仍然有效
- 如果遇到连接问题，可以尝试重启Pico 2W
- 本项目仅支持DS5手柄，不支持其他型号的手柄

## 故障排除

- **无法配对**：确保DS5手柄处于配对模式，且Pico 2W已上电
- **控制不灵敏**：可以调节速度设置
- **卡死**：尝试重启Pico 2W

## 贡献

欢迎提交Issue和Pull Request来改进本项目！

## 致谢

[蓝牙部分代码来自](https://github.com/awalol/DS5Dongle)