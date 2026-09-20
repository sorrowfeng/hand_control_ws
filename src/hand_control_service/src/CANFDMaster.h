#ifndef CANFDMASTER_H
#define CANFDMASTER_H

// ============================================================================
// CANFD backend selection (compile time, backends are mutually exclusive)
// ----------------------------------------------------------------------------
//   backend              platform            dependency
//   ------------------   ----------------    --------------------------------
//   gsusb-canfd (default) Windows/Linux/macOS libusb-1.0 (userspace gs_usb)
//   SocketCAN            Linux               in-kernel (can0 ...), no extra deps
//   libcanbus            Linux               /usr/local/lib/libcanbus.so
//                                             + libusb-1.0.so
//   HCanbus              Windows             vendor HCanbus SDK
//
// The default backend is gsusb-canfd. Switching is done with the CMake options:
//   -DHAND_CONTROL_USE_GSUSB_CANFD=ON   <- default
//   -DHAND_CONTROL_USE_SOCKETCAN=ON
//   -DHAND_CONTROL_USE_LIBCANBUS=ON
//   -DHAND_CONTROL_USE_HCANBUS=ON
//
// If more than one backend is selected the build fails at compile time instead
// of silently picking one.
// ============================================================================

// --- normalise the CMake-provided HAND_CONTROL_* macros to USE_* ---
#if defined(HAND_CONTROL_CANFD_USE_GSUSB) && !defined(USE_GSUSB_CANFD)
#define USE_GSUSB_CANFD
#endif
#if defined(HAND_CONTROL_CANFD_USE_SOCKETCAN) && !defined(USE_SOCKETCAN)
#define USE_SOCKETCAN
#endif
#if defined(HAND_CONTROL_CANFD_USE_LIBCANBUS) && !defined(USE_LIBCANBUS)
#define USE_LIBCANBUS
#endif
#if defined(HAND_CONTROL_CANFD_USE_HCANBUS) && !defined(USE_HCANBUS)
#define USE_HCANBUS
#endif

// --- mutual exclusion check ---
#if (defined(USE_GSUSB_CANFD) + defined(USE_SOCKETCAN) + defined(USE_LIBCANBUS) + \
     defined(USE_HCANBUS)) > 1
#error "CANFD backend can only be one of: USE_GSUSB_CANFD / USE_SOCKETCAN / USE_LIBCANBUS / USE_HCANBUS"
#endif

// --- default backend: gsusb-canfd ---
#if !defined(USE_GSUSB_CANFD) && !defined(USE_SOCKETCAN) && \
    !defined(USE_LIBCANBUS) && !defined(USE_HCANBUS)
#define USE_GSUSB_CANFD
#endif

// --- platform validity ---
#if defined(USE_HCANBUS) && !defined(_WIN32)
#error "USE_HCANBUS is Windows-only; use gsusb-canfd, or socketcan/libcanbus on Linux"
#endif
#if (defined(USE_SOCKETCAN) || defined(USE_LIBCANBUS)) && \
    (defined(_WIN32) || defined(__APPLE__))
#error "USE_SOCKETCAN / USE_LIBCANBUS are Linux-only; use gsusb-canfd on Windows/macOS"
#endif

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#if defined(USE_GSUSB_CANFD)
// The userspace libusb gs_usb driver only needs a forward declaration here.
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

#if defined(USE_GSUSB_CANFD)
  // gsusb-canfd (userspace libusb gs_usb driver) bus handle.
  std::unique_ptr<canfd::CanFdBus> gsusb_bus_;
#elif defined(USE_LIBCANBUS)
  void* libcan_handle_;
  void* libusb_handle_;
  int (*fn_CAN_ScanDevice_)();
  int (*fn_CAN_OpenDevice_)(int, int);
  int (*fn_CAN_CloseDevice_)(int, int);
  int (*fn_CANFD_Init_)(int, int, void*);
  int (*fn_CANFD_Transmit_)(int, int, void*, int, int);
  int (*fn_CANFD_Receive_)(int, int, void*, int, int);

  bool loadLibCanBus();
  void unloadLibCanBus();
  bool libcanbusConnect(int deviceIndex, int nomBaud, int dataBaud);
  bool libcanbusDisconnect();
  bool libcanbusSendData(uint32_t id, const std::vector<uint8_t>& data,
                         int externflag);
  void libcanbusReceiveLoop();
#elif defined(USE_SOCKETCAN)
  int socket_fd_;
  std::string current_interface_;
#endif
  // USE_HCANBUS needs no extra members (HCanbus SDK global functions).
};

#endif  // CANFDMASTER_H
