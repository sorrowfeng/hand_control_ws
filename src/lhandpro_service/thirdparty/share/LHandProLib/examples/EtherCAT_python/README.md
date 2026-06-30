# EtherCAT Python 示例

## 概述

基于 Python SDK 的 EtherCAT 通信示例，支持 **电机运动测试** 和 **传感器数据读取** 两种模式。

底层使用 SOEM（Simple Open EtherCAT Master）协议栈。

## 环境依赖

```bash
cd LHandProLibLib_EtherCAT_Test_python/
pip install -r requirements.txt
```

**Windows 额外依赖**：[Npcap](https://npcap.com/) 或 [WinPcap](https://www.winpcap.org/)
安装 Npcap 时务必勾选 **"WinPcap API‑Compatible Mode"**。

## 运行

```bash
python main.py
```

## 操作说明

1. 选择 EtherCAT 网口
2. 选择传感器格式：
   - `0` — 默认格式（6 传感器）
   - `1` — Channel 格式（7 传感器，手指指尖 + 指腹）
3. 程序自动使能 → 回零 → 进入循环运动
4. 按 `Esc` 退出程序

## 文件说明

| 文件 | 作用 |
|---|---|
| `main.py` | 示例主程序入口 |
| `ethercat_master.py` | EtherCAT 主站封装（基于 SOEM） |
| `requirements.txt` | Python 依赖 |
| `lhandprolib_python_sdk/` | Python SDK 封装库（详见该目录下 README） |
