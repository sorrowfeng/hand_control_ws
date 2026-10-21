#include "CANFDMaster.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

// 后端分派：由 CANFDMaster.h 的互斥检查保证下方两个分支恰好命中一个。
// 分支顺序不影响结果（各条件互斥），此处保持与历史文件一致的排列顺序。

namespace {
// 断开与重连之间的延时(ms)：设备复位(如 OTA 进入 bootloader)后，USB-CAN
// 适配器/USB 句柄需要一点时间才能重新打开，立即重连常失败。保持与 Python
// 版 controller_canfd.attempt_reconnect(delay_s) 一致。
constexpr int kCanfdReconnectDelayMs = 200;
}  // namespace


#if defined(USE_GSUSB_CANFD)

// 默认后端: gsusb-canfd(userspace libusb gs_usb 驱动,支持 CAN FD)。
// Windows / Linux / macOS 共用这一段——库内没有任何平台条件编译,
// 三平台走同一条代码路径。Windows 上需自备 libusb-1.0。
#include "canfd/canfd.hpp"

CANFDMaster::CANFDMaster()
    : dev_index_(-1),
      nom_baud_(0),
      data_baud_(0),
      is_connected_(false),
      receive_thread_running_(false) {}

CANFDMaster::~CANFDMaster() {
  if (is_connected_) {
    disconnect();
  }
}

std::vector<std::string> CANFDMaster::scanDevices() {
  std::vector<std::string> devices;
  try {
    const std::vector<canfd::AdapterInfo> adapters = canfd::scanAdapters();
    for (size_t i = 0; i < adapters.size(); ++i) {
      // 库提供 uniqueName(): "name (VID:PID) SN:serial"。index 仍由本工程
      // 自己带上,便于用户按扫描顺序选择。
      devices.push_back(std::to_string(i) + " " + adapters[i].uniqueName());
    }
  } catch (const std::exception& exception) {
    std::cerr << "gsusb scan failed: " << exception.what() << std::endl;
  }
  return devices;
}

bool CANFDMaster::connect(int deviceIndex, int nomBaud, int dataBaud) {
  if (is_connected_) {
    disconnect();
  }

  try {
    // 复用 scanDevices() 的枚举顺序按索引定位适配器,再以 vid/pid(+serial)
    // 精确打开,避免只识别厂商默认 VID/PID。
    const std::vector<canfd::AdapterInfo> adapters = canfd::scanAdapters();
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(adapters.size())) {
      std::cerr << "gsusb device index out of range: " << deviceIndex
                << std::endl;
      return false;
    }

    const canfd::AdapterInfo& target = adapters[deviceIndex];

    // 按扫描结果精确打开该适配器:库内部优先用 serial(跨端口稳定),无 serial
    // 时回退 USB bus/address。channel 默认 0——本型号适配器为单通道。
    gsusb_bus_ = std::make_unique<canfd::CanFdBus>();
    gsusb_bus_->open(target);

    canfd::BusConfig config;
    config.bitrate = static_cast<uint32_t>(nomBaud);
    config.sample_point = 0.80;
    config.data_bitrate = static_cast<uint32_t>(dataBaud);
    config.data_sample_point = 0.75;
    config.fd = true;
    // 本工程不 echo-tag 发送(send(frame) 默认),回环帧不由本机产生;开启
    // drop_echo 让库在接收路径直接丢弃任何 echo-tagged 回环。
    config.drop_echo = true;

    gsusb_bus_->configure(config);
  } catch (const std::exception& exception) {
    std::cerr << "gsusb connect failed: " << exception.what() << std::endl;
    gsusb_bus_.reset();
    return false;
  }

  dev_index_ = deviceIndex;
  nom_baud_ = nomBaud;
  data_baud_ = dataBaud;
  is_connected_ = true;

  receive_thread_running_ = true;
  receive_thread_ = std::thread(&CANFDMaster::receiveThreadFunc, this);

  resetLostFrames();
  return true;
}

bool CANFDMaster::disconnect() {
  if (!is_connected_) {
    return true;
  }

  receive_thread_running_ = false;
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  gsusb_bus_.reset();
  dev_index_ = -1;
  is_connected_ = false;
  resetLostFrames();
  return true;
}

bool CANFDMaster::attemptReconnect() {
  const int device_index = dev_index_;
  const int nom_baud = nom_baud_;
  const int data_baud = data_baud_;
  disconnect();
  // 断开后稍等再重连：设备复位后适配器/USB 句柄需要一点时间才能重新打开。
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kCanfdReconnectDelayMs));
  return connect(device_index, nom_baud, data_baud);
}

bool CANFDMaster::sendData(unsigned int id, const unsigned char* data,
                           unsigned int size, int externflag) {
  return sendData(static_cast<uint32_t>(id),
                  std::vector<uint8_t>(data, data + size), externflag);
}

bool CANFDMaster::sendData(uint32_t id, const std::vector<uint8_t>& data,
                           int externflag) {
  if (!is_connected_ || !gsusb_bus_) {
    return false;
  }

  canfd::CanFrame frame;
  frame.id = id;
  frame.extended = (externflag != 0);
  frame.fd = true;
  frame.brs = true;
  frame.remote = false;
  // 与其它后端一致:固定发送 64 字节 CAN FD 帧,实际数据在前 copy_size 字节。
  frame.size = 64;

  const size_t copy_size = std::min(data.size(), size_t(64));
  if (copy_size > 0) {
    std::memcpy(frame.data.data(), data.data(), copy_size);
  }

  try {
    gsusb_bus_->send(frame);
    return true;
  } catch (const std::exception& exception) {
    std::cerr << "gsusb send failed: " << exception.what() << std::endl;
    ++lost_frames_;
    return false;
  }
}

void CANFDMaster::setReceiveCallback(ReceiveCallback callback) {
  receive_callback_ = callback;
}

void CANFDMaster::receiveThreadFunc() {
  while (receive_thread_running_) {
    if (!is_connected_ || !gsusb_bus_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 连续排空适配器已缓存的帧: gs_usb 每个 USB 包只承载 1 帧, 而灵巧手
    // 反馈流约 2kHz; 若每收一帧就 sleep(1ms), 接收线程永远追不上到达速率,
    // 适配器 RX FIFO 持续溢出, SDO 应答(如 0x581)会被随机丢弃, 使
    // initial() 在 50ms 超时后报 LER_COMM_RECV。改为“队列不空就接着收”。
    for (int drained = 0; drained < 4096 && receive_thread_running_; ++drained) {
      canfd::CanFrame frame;
      bool received = false;
      try {
        received = gsusb_bus_->receive(
            frame, std::chrono::milliseconds(drained == 0 ? 20 : 1));
      } catch (const std::exception& exception) {
        std::cerr << "gsusb receive failed: " << exception.what() << std::endl;
        ++lost_frames_;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        break;
      }
      if (!received) {
        break;
      }

      // drop_echo 已在 configure() 中开启,回环帧由库的接收路径丢弃,
      // 这里不会收到 echo 帧。
      if (!receive_callback_) {
        continue;
      }
      const size_t data_length =
          std::min(static_cast<size_t>(frame.size), size_t(64));
      std::vector<uint8_t> rxdata(data_length);
      if (data_length > 0) {
        std::memcpy(rxdata.data(), frame.data.data(), data_length);
      }

      const uint32_t id = frame.extended ? (frame.id & canfd::kCanEffMask)
                                         : (frame.id & 0x7FFu);
      // 库现在保证 timestamp 始终有效(取不到硬件时间戳时回退宿主单调时钟),
      // 直接换算成微秒即可。
      const uint64_t timestamp = static_cast<uint64_t>(frame.timestamp * 1e6);

      receive_callback_(id, rxdata, timestamp);
    }
  }
}

bool CANFDMaster::isConnected() const { return is_connected_; }

int CANFDMaster::getCurrentDeviceIndex() const { return dev_index_; }

uint64_t CANFDMaster::getLostFrames() const { return lost_frames_; }

void CANFDMaster::resetLostFrames() { lost_frames_ = 0; }

#elif defined(USE_SOCKETCAN)

// Linux 可选后端: SocketCAN。
#include <dirent.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>


CANFDMaster::CANFDMaster()
    : dev_index_(-1),
      nom_baud_(0),
      data_baud_(0),
      is_connected_(false),
      receive_thread_running_(false),
      socket_fd_(-1)
{
}

CANFDMaster::~CANFDMaster() {
  if (is_connected_) {
    disconnect();
  }
}

bool CANFDMaster::setupCANInterface(const std::string& ifname, int nomBaud,
                                    int dataBaud) {
  std::string cmd = "ip link set " + ifname + " down";
  system(cmd.c_str());

  cmd = "ip link set " + ifname + " type can bitrate " +
        std::to_string(nomBaud) + " dbitrate " + std::to_string(dataBaud) +
        " fd on loopback off listen-only off";
  system(cmd.c_str());

  cmd = "ip link set " + ifname + " up";
  system(cmd.c_str());

  return true;
}


std::vector<std::string> CANFDMaster::scanDevices() {
  std::vector<std::string> devices;

  DIR* dir = opendir("/sys/class/net");
  if (dir == nullptr) {
    return devices;
  }

  struct dirent* ent = nullptr;
  while ((ent = readdir(dir)) != nullptr) {
    const std::string ifname = ent->d_name;
    if (ifname.rfind("can", 0) == 0) {
      devices.push_back(ifname);
    }
  }
  closedir(dir);

  return devices;
}

bool CANFDMaster::connect(int deviceIndex, int nomBaud, int dataBaud) {
  if (is_connected_) {
    disconnect();
  }

  dev_index_ = deviceIndex;
  nom_baud_ = nomBaud;
  data_baud_ = dataBaud;

  std::vector<std::string> devices = scanDevices();
  if (deviceIndex < 0 || deviceIndex >= static_cast<int>(devices.size())) {
    dev_index_ = -1;
    return false;
  }

  const std::string ifname = devices[deviceIndex];
  current_interface_ = ifname;

  if (!setupCANInterface(ifname, nomBaud, dataBaud)) {
    std::cerr << "Failed to setup CAN interface" << std::endl;
    dev_index_ = -1;
    current_interface_.clear();
    return false;
  }

  socket_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (socket_fd_ < 0) {
    perror("socket");
    dev_index_ = -1;
    current_interface_.clear();
    return false;
  }

  // 读取路径用 select 等待 + 非阻塞 drain，避免高速反馈时丢帧。
  const int fd_flags = fcntl(socket_fd_, F_GETFL, 0);
  if (fd_flags >= 0) {
    fcntl(socket_fd_, F_SETFL, fd_flags | O_NONBLOCK);
  }

  int enable_canfd = 1;
  if (setsockopt(socket_fd_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd,
                 sizeof(enable_canfd)) < 0) {
    perror("setsockopt CAN_RAW_FD_FRAMES");
    close(socket_fd_);
    socket_fd_ = -1;
    dev_index_ = -1;
    current_interface_.clear();
    return false;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
  if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl SIOCGIFINDEX");
    close(socket_fd_);
    socket_fd_ = -1;
    dev_index_ = -1;
    current_interface_.clear();
    return false;
  }

  struct sockaddr_can addr;
  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr),
           sizeof(addr)) < 0) {
    perror("bind");
    close(socket_fd_);
    socket_fd_ = -1;
    dev_index_ = -1;
    current_interface_.clear();
    return false;
  }

  is_connected_ = true;
  receive_thread_running_ = true;
  receive_thread_ = std::thread(&CANFDMaster::receiveThreadFunc, this);

  resetLostFrames();
  return true;
}


bool CANFDMaster::disconnect() {
  if (!is_connected_) {
    return true;
  }

  receive_thread_running_ = false;
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  current_interface_.clear();

  dev_index_ = -1;
  is_connected_ = false;
  resetLostFrames();
  return true;
}

bool CANFDMaster::attemptReconnect() {
  const int device_index = dev_index_;
  const int nom_baud = nom_baud_;
  const int data_baud = data_baud_;
  disconnect();
  // 断开后稍等再重连：设备复位后适配器/USB 句柄需要一点时间才能重新打开。
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kCanfdReconnectDelayMs));
  return connect(device_index, nom_baud, data_baud);
}

bool CANFDMaster::sendData(unsigned int id, const unsigned char* data,
                           unsigned int size, int externflag) {
  return sendData(static_cast<uint32_t>(id),
                  std::vector<uint8_t>(data, data + size), externflag);
}

bool CANFDMaster::sendData(uint32_t id, const std::vector<uint8_t>& data,
                           int externflag) {
  if (!is_connected_) {
    return false;
  }

  if (socket_fd_ < 0) {
    return false;
  }

  struct canfd_frame frame;
  memset(&frame, 0, sizeof(frame));

  frame.can_id = externflag ? (id | CAN_EFF_FLAG) : id;
  frame.len = 64;
  frame.flags = CANFD_BRS;

  const size_t copy_size = std::min(data.size(), size_t(64));
  memcpy(frame.data, data.data(), copy_size);

  const ssize_t ret = write(socket_fd_, &frame, sizeof(frame));
  const bool success = (ret == static_cast<ssize_t>(sizeof(frame)));
  if (!success) {
    perror("write");
    ++lost_frames_;
  }
  return success;
}


void CANFDMaster::setReceiveCallback(ReceiveCallback callback) {
  receive_callback_ = callback;
}

void CANFDMaster::receiveThreadFunc() {
  while (receive_thread_running_) {
    if (!is_connected_ || socket_fd_ < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_fd_, &readfds);

    const int ret =
        select(socket_fd_ + 1, &readfds, nullptr, nullptr, &timeout);
    if (ret < 0) {
      if (errno != EINTR) {
        ++lost_frames_;
      }
      continue;
    }
    if (ret == 0) {
      continue;
    }

    // select 只负责等待第一帧；随后非阻塞地把内核接收队列排空。
    // 灵巧手反馈约 2kHz，逐帧 sleep(5ms) 会让接收线程永远追不上。
    for (int drained = 0; drained < 4096 && receive_thread_running_;
         ++drained) {
      struct canfd_frame frame;
      const ssize_t nbytes = read(socket_fd_, &frame, sizeof(frame));
      if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        if (errno != EINTR) {
          ++lost_frames_;
        }
        break;
      }
      if (nbytes == 0) {
        break;
      }
      if (nbytes != static_cast<ssize_t>(sizeof(frame))) {
        ++lost_frames_;
        continue;
      }
      if (!receive_callback_) {
        continue;
      }

      uint32_t id = frame.can_id & CAN_EFF_MASK;
      if (!(frame.can_id & CAN_EFF_FLAG)) {
        id = frame.can_id & CAN_SFF_MASK;
      }

      const int data_length = frame.len;
      std::vector<uint8_t> rxdata(data_length);
      memcpy(rxdata.data(), frame.data, data_length);

      const uint64_t timestamp =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();

      receive_callback_(id, rxdata, timestamp);
    }
  }
}


bool CANFDMaster::isConnected() const { return is_connected_; }

int CANFDMaster::getCurrentDeviceIndex() const { return dev_index_; }

uint64_t CANFDMaster::getLostFrames() const { return lost_frames_; }

void CANFDMaster::resetLostFrames() { lost_frames_ = 0; }

#endif  // USE_GSUSB_CANFD / USE_SOCKETCAN
