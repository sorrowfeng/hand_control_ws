#ifndef CANFDMASTER_H
#define CANFDMASTER_H

// ============================================================================
// CANFD 后端选择（编译期，两个后端互斥）
// ----------------------------------------------------------------------------
//   后端                  平台                 依赖
//   -------------------   ------------------   -------------------------------
//   gsusb-canfd（默认）   Windows/Linux/macOS  libusb-1.0
//                                              （Windows 需自备 libusb）
//   SocketCAN             Linux                内核自带（can0 等），零额外依赖
//
// 默认后端是 gsusb-canfd：三平台走同一套 userspace gs_usb 代码路径，行为一致。
//
// 切换方式（任选其一）:
//   1) CMake 选项（推荐）
//        -DHAND_CONTROL_USE_GSUSB_CANFD=ON    ← 默认
//        -DHAND_CONTROL_USE_SOCKETCAN=ON
//   2) 手工取消下面对应行的注释（不经 CMake 直接编译本文件时使用）
//
// 若同时命中两个后端，编译期直接 #error，不会静默取其一。
// ============================================================================

// #define USE_GSUSB_CANFD
// #define USE_SOCKETCAN

// --- 归一化: CMake 传来的 HAND_CONTROL_* 宏与历史宏统一到 USE_* 形式 ---
#if defined(HAND_CONTROL_CANFD_USE_GSUSB) && !defined(USE_GSUSB_CANFD)
#define USE_GSUSB_CANFD
#endif
#if defined(HAND_CONTROL_CANFD_USE_SOCKETCAN) && !defined(USE_SOCKETCAN)
#define USE_SOCKETCAN
#endif

// --- 互斥校验 ---
#if (defined(USE_GSUSB_CANFD) + defined(USE_SOCKETCAN)) > 1
#error "CANFD 后端只能选一个: USE_GSUSB_CANFD / USE_SOCKETCAN"
#endif

// --- 默认后端: gsusb-canfd ---
#if !defined(USE_GSUSB_CANFD) && !defined(USE_SOCKETCAN)
#define USE_GSUSB_CANFD
#endif

// --- 平台合法性 ---
#if defined(USE_SOCKETCAN) && (defined(_WIN32) || defined(__APPLE__))
#error "USE_SOCKETCAN 仅支持 Linux；Windows/macOS 请用默认的 gsusb-canfd"
#endif

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#if defined(USE_GSUSB_CANFD)
// gsusb-canfd(userspace libusb gs_usb 驱动)仅需前向声明，避免污染包含者。
#include <memory>

namespace canfd {
class CanFdBus;
}  // namespace canfd
#endif

using ReceiveCallback = std::function<void(
    uint32_t id, const std::vector<uint8_t>& data, uint64_t timestamp)>;

class CANFDMaster {
 public:
  CANFDMaster();
  ~CANFDMaster();

  std::vector<std::string> scanDevices();

  bool connect(int deviceIndex, int nomBaud = 1000000,
               int dataBaud = 5000000);
  bool disconnect();
  bool attemptReconnect();

  bool sendData(unsigned int id, const unsigned char* data, unsigned int size,
                int externflag = 0);
  bool sendData(uint32_t id, const std::vector<uint8_t>& data,
                int externflag = 0);

  void setReceiveCallback(ReceiveCallback callback);

  bool isConnected() const;
  int getCurrentDeviceIndex() const;
  uint64_t getLostFrames() const;
  void resetLostFrames();

 private:
#if defined(USE_SOCKETCAN)
  bool setupCANInterface(const std::string& ifname, int nomBaud, int dataBaud);
#endif

  void receiveThreadFunc();

 private:
  int dev_index_;
  int nom_baud_;
  int data_baud_;
  std::atomic<bool> is_connected_;
  std::atomic<bool> receive_thread_running_;
  std::thread receive_thread_;
  ReceiveCallback receive_callback_;
  uint64_t lost_frames_{0};
  // 总线健康判断：超过阈值没有收到任何帧就认为链路已断，交给上层重连。
  std::atomic<int64_t> connected_at_ms_{0};
  std::atomic<int64_t> last_rx_ms_{0};

#if defined(USE_GSUSB_CANFD)
  // gsusb-canfd(userspace libusb gs_usb 驱动)总线句柄。
  std::unique_ptr<canfd::CanFdBus> gsusb_bus_;
#elif defined(USE_SOCKETCAN)
  int socket_fd_;
  std::string current_interface_;
#endif
};

#endif  // CANFDMASTER_H
