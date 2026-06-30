#ifndef CANFDMASTER_H
#define CANFDMASTER_H
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Linux 驱动选择：取消注释下一行使用 libcanbus，注释掉则使用 socketcan
// #define USE_LIBCANBUS

// 回调函数类型定义
using ReceiveCallback = std::function<void(
    uint32_t id, const std::vector<uint8_t>& data, uint64_t timestamp)>;

class CANFDMaster {
 public:
  CANFDMaster();
  ~CANFDMaster();

  // 扫描可用设备
  std::vector<std::string> scanDevices();

  // 连接设备（仲裁段 80%，数据段 75%）
  bool connect(int deviceIndex, int nomBaud = 1000, int dataBaud = 5000);

  // 断开连接
  bool disconnect();

  // 重连
  bool attemptReconnect();

  // 发送数据（默认64字节）
  bool sendData(unsigned int id, const unsigned char* data, unsigned int size,
                int externflag = 0);
  bool sendData(uint32_t id, const std::vector<uint8_t>& data,
                int externflag = 0);

  // 设置接收数据回调函数
  void setReceiveCallback(ReceiveCallback callback);

  // 检查是否已连接
  bool isConnected() const;

  // 获取当前设备索引
  int getCurrentDeviceIndex() const;

  // 获取丢帧统计
  uint64_t getLostFrames() const;

  // 重置丢帧统计
  void resetLostFrames();

 private:
#ifndef _WIN32
  // 设置CAN接口参数（仅 Linux）
  bool setupCANInterface(const std::string& ifname, int nomBaud, int dataBaud);
#endif

  // 接收线程函数
  void receiveThreadFunc();

 private:
  int dev_index_;                             // 当前设备索引
  int nom_baud_;                              // 当前仲裁段波特率（给 reconnect 用）
  int data_baud_;                             // 当前数据段波特率（给 reconnect 用）
  std::atomic<bool> is_connected_;            // 连接状态
  std::atomic<bool> receive_thread_running_;  // 接收线程运行状态
  std::thread receive_thread_;                // 接收线程

  ReceiveCallback receive_callback_;  // 接收回调函数

  uint64_t lost_frames_{0};  // 丢帧计数器

#ifndef _WIN32
  // Linux 特有成员
#ifdef USE_LIBCANBUS
  // libcanbus 动态加载
  void* libcan_handle_;            // dlopen handle
  void* libusb_handle_;            // libusb dlopen handle
  int (*fn_CAN_ScanDevice_)();
  int (*fn_CAN_OpenDevice_)(int, int);
  int (*fn_CAN_CloseDevice_)(int, int);
  int (*fn_CANFD_Init_)(int, int, void*);
  int (*fn_CANFD_Transmit_)(int, int, void*, int, int);
  int (*fn_CANFD_Receive_)(int, int, void*, int, int);

  // libcanbus 辅助方法
  bool loadLibCanBus();
  void unloadLibCanBus();
  bool libcanbusConnect(int deviceIndex, int nomBaud, int dataBaud);
  bool libcanbusDisconnect();
  bool libcanbusSendData(uint32_t id, const std::vector<uint8_t>& data, int externflag);
  void libcanbusReceiveLoop();
#else
  int socket_fd_;                  // SocketCAN 文件描述符
  std::string current_interface_;  // 当前使用的CAN接口名称
#endif
#endif
};

#endif  // CANFDMASTER_H
