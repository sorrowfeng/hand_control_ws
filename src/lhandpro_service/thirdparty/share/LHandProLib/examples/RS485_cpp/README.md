# RS485 C++ 示例

## 概述

基于 C++ SDK 的 RS485 串口通信示例，支持 **电机运动测试** 和 **传感器数据读取** 两种模式。

## 编译

```bash
cd LHandProLib_RS485_Test_cpp/
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

编译产物在 `build/bin/` 下。

## 运行

```bash
LHandProLib_RS485_Test.exe
```

## 操作说明

1. 扫描并选择串口
2. 选择测试模式：
   - `0` — 电机运动测试：使能 → 回零 → 逐轴循环运动
   - `1` — 传感器数据测试：直接读取传感器数据（不使能电机）
3. 按 `Esc` 退出程序

## 驱动说明

RS485 使用标准串口通信，无需额外驱动或 DLL。

## 文件说明

| 文件 | 作用 |
|---|---|
| `main.cpp` | 示例主程序入口 |
| `SerialPort.h / .cpp` | 串口通信封装 |
| `CMakeLists.txt` | CMake 构建配置 |
