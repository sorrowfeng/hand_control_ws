# CANFD C++ 示例

## 概述

基于 C++ SDK 的 CANFD 通信示例，支持 **电机运动测试** 和 **传感器数据读取** 两种模式。

提供 C++ API（`main.cpp`）和 C API（`main_c.cpp`）两个版本。

## 编译

```bash
cd LHandProLib_CANFD_Test_cpp/
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

编译产物在 `build/bin/` 下。

## 运行

```bash
LHandProLib_CANFD_Test.exe     # C++ API 版本
# 或
LHandProLib_CANFD_Test_C.exe   # C API 版本
```

## 操作说明

1. 选择 CANFD 通道
2. 输入 CANFD 节点 ID（Node ID，默认 1）
3. 选择测试模式：
   - `0` — 电机运动测试：使能 → 回零 → 逐轴循环运动
   - `1` — 传感器数据测试：直接读取传感器数据（不使能电机）
4. 按 `Esc` 退出程序

## Windows DLL 部署

将 `x64/bin/HCanbus.dll`（运行）或 `x64/lib/HCanbus.lib`（链接）放置到：

1. 与 `LHandProLib_CANFD_Test.exe` 相同的目录
2. 或添加到系统 PATH

`x64/` 目录结构：

| 路径 | 说明 |
|---|---|
| `x64/bin/HCanbus.dll` | CANFD 运行时 DLL |
| `x64/lib/HCanbus.lib` | CANFD 静态链接库 |
| `x64/include/HCanbus.h` | CANFD C 语言头文件 |

## 文件说明

| 文件 | 作用 |
|---|---|
| `main.cpp` | C++ API 示例入口 |
| `main_c.cpp` | C API 示例入口 |
| `CANFDMaster.h / .cpp` | CANFD 通信封装（Windows HCanbus / Linux socketcan） |
| `CMakeLists.txt` | CMake 构建配置 |
| `x64/lib/` | Windows 64位依赖库 |
| `x64/include/` | Windows 64位头文件 |
| `x86/lib/` | Windows 32位依赖库 |
| `x86/include/` | Windows 32位头文件 |
