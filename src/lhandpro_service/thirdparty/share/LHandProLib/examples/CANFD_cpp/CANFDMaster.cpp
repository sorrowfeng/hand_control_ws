#include "CANFDMaster.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

#ifdef _WIN32
// ============================================================
// Windows 实现：使用 HCanbus SDK
// ============================================================
#include <HCanbus.h>

// DLC到数据长度映射表
static char dlc2len[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

// 数据长度到DLC转换
static char lenToDLC(char length) {
  if (length <= 8)
    return length;
  else if (length <= 12)
    return 9;
  else if (length <= 16)
    return 10;
  else if (length <= 20)
    return 11;
  else if (length <= 24)
    return 12;
  else if (length <= 32)
    return 13;
  else if (length <= 48)
    return 14;
  else
    return 15;
}

CANFDMaster::CANFDMaster()
    : dev_index_(-1), nom_baud_(0), data_baud_(0),
      is_connected_(false), receive_thread_running_(false) {}

CANFDMaster::~CANFDMaster() {
  if (is_connected_) {
    disconnect();
  }
}

std::vector<std::string> CANFDMaster::scanDevices() {
  std::vector<std::string> devices;

  int devCount = CAN_ScanDevice();
  for (int i = 0; i < devCount; i++) {
    Dev_Info info;
    CAN_ReadDevInfo(i, &info);

    std::string deviceStr = std::to_string(i) + " " + std::string(info.HW_Type);
    devices.push_back(deviceStr);
  }

  return devices;
}

bool CANFDMaster::connect(int deviceIndex, int nomBaud, int dataBaud) {
  if (is_connected_) {
    disconnect();
  }

  // 打开设备
  int ret = CAN_OpenDevice(deviceIndex);
  if (ret != 0) {
    return false;
  }

  // CANFD配置
  CanFD_Config cancfg;
  cancfg.Model = 0;                           // 0 正常模式
  cancfg.NomBaud = nomBaud * 1000;            // 仲裁段波特率
  cancfg.DatBaud = dataBaud * 1000;           // 数据段波特率
  cancfg.Config = 0x0001 | 0x0002 | 0x0004;  // 终端电阻 + 唤醒 + 重传
  cancfg.Cantype = 1;                         // ISO CANFD

  // 仲裁段采样点 80%
  cancfg.NomPre = 2;
  cancfg.NomTseg1 = 31;
  cancfg.NomTseg2 = 8;
  cancfg.NomSJW = 5;

  // 数据段采样点 75%
  cancfg.DatPre = 1;
  cancfg.DatTseg1 = 11;
  cancfg.DatTseg2 = 4;
  cancfg.DatSJW = 2;

  // 初始化CANFD
  ret = CANFD_Init(deviceIndex, &cancfg);
  if (ret != 0) {
    CAN_CloseDevice(deviceIndex);
    return false;
  }

  dev_index_ = deviceIndex;
  nom_baud_ = nomBaud;
  data_baud_ = dataBaud;
  is_connected_ = true;

  // 启动接收线程
  receive_thread_running_ = true;
  receive_thread_ = std::thread(&CANFDMaster::receiveThreadFunc, this);

  resetLostFrames();
  return true;
}

bool CANFDMaster::disconnect() {
  if (!is_connected_) {
    return true;
  }

  // 停止接收线程
  receive_thread_running_ = false;
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  int ret = CAN_CloseDevice(dev_index_);
  if (ret == 0) {
    dev_index_ = -1;
    is_connected_ = false;
    resetLostFrames();
    return true;
  }

  return false;
}

bool CANFDMaster::attemptReconnect() {
  disconnect();
  return connect(dev_index_, nom_baud_, data_baud_);
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

  CanFD_Msg msg;

  msg.DLC = lenToDLC(64);       // 固定64字节
  msg.ExternFlag = externflag;  // 扩展帧标志
  msg.RemoteFlag = 0;           // 数据帧
  msg.ID = id;
  msg.FrameType = 0x04;  // CANFD帧

  // 清零并填充数据
  memset(msg.Data, 0, 64);
  size_t copySize = std::min(data.size(), size_t(64));
  memcpy(msg.Data, data.data(), copySize);

  // 发送数据
  int ret = CANFD_Transmit(dev_index_, &msg, 1, 100);
  bool success = (ret == 1);
  if (!success)
    lost_frames_++;
  return success;
}

void CANFDMaster::setReceiveCallback(ReceiveCallback callback) {
  receive_callback_ = callback;
}

void CANFDMaster::receiveThreadFunc() {
  while (receive_thread_running_) {
    if (!is_connected_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    CanFD_Msg msgs[500];
    int count = CANFD_Receive(dev_index_, msgs, 500, 50);  // 50ms超时

    if (count > 0 && receive_callback_) {
      for (int i = 0; i < count; i++) {
        const CanFD_Msg& msg = msgs[i];
        int dataLength = dlc2len[msg.DLC];

        std::vector<uint8_t> rxdata(dataLength);
        memcpy(rxdata.data(), msg.Data, dataLength);

        if (msg.ErrSatus != 0)
          lost_frames_++;

        receive_callback_(msg.ID, rxdata, msg.TimeStamp);
      }
    } else if (count > 0 && !receive_callback_) {
      std::cout << "Received " << count << " messages but no callback set"
                << std::endl;
    } else if (count <= 0) {
      lost_frames_++;
    }

    if (lost_frames_ > (1 << 20)) {
      lost_frames_ = 0;
      return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

bool CANFDMaster::isConnected() const {
  return is_connected_;
}

int CANFDMaster::getCurrentDeviceIndex() const {
  return dev_index_;
}

uint64_t CANFDMaster::getLostFrames() const {
  return lost_frames_;
}

void CANFDMaster::resetLostFrames() {
  lost_frames_ = 0;
}

#else
// ============================================================
// Linux 实现：SocketCAN 或 libcanbus（运行时选择）
// ============================================================
#include <dirent.h>
#include <dlfcn.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// libcanbus 结构体定义（与 HCanbus.h 一致，无 __stdcall）
#pragma pack(push, 1)
struct LibCanFD_Config {
  unsigned int NomBaud;
  unsigned int DatBaud;
  unsigned short NomPre;
  unsigned char NomTseg1;
  unsigned char NomTseg2;
  unsigned char NomSJW;
  unsigned char DatPre;
  unsigned char DatTseg1;
  unsigned char DatTseg2;
  unsigned char DatSJW;
  unsigned char Config;
  unsigned char Model;
  unsigned char Cantype;
};

struct LibCanFD_Msg {
  unsigned int ID;
  unsigned int TimeStamp;
  unsigned char FrameType;
  unsigned char DLC;
  unsigned char ExternFlag;
  unsigned char RemoteFlag;
  unsigned char BusSatus;
  unsigned char ErrSatus;
  unsigned char TECounter;
  unsigned char RECounter;
  unsigned char Data[64];
};
#pragma pack(pop)

CANFDMaster::CANFDMaster()
    : dev_index_(-1),
      nom_baud_(0),
      data_baud_(0),
      is_connected_(false),
      receive_thread_running_(false),
#ifdef USE_LIBCANBUS
      libcan_handle_(nullptr),
      libusb_handle_(nullptr),
      fn_CAN_ScanDevice_(nullptr),
      fn_CAN_OpenDevice_(nullptr),
      fn_CAN_CloseDevice_(nullptr),
      fn_CANFD_Init_(nullptr),
      fn_CANFD_Transmit_(nullptr),
      fn_CANFD_Receive_(nullptr)
#else
      socket_fd_(-1)
#endif
{}

CANFDMaster::~CANFDMaster() {
  if (is_connected_) {
    disconnect();
  }
#ifdef USE_LIBCANBUS
  unloadLibCanBus();
#endif
}

#ifndef USE_LIBCANBUS
bool CANFDMaster::setupCANInterface(const std::string& ifname, int nomBaud,
                                    int dataBaud) {
  std::string cmd = "ip link set " + ifname + " down";
  system(cmd.c_str());

  cmd = "ip link set " + ifname + " type can bitrate " +
        std::to_string(nomBaud * 1000) + " dbitrate " +
        std::to_string(dataBaud * 1000) + " fd on loopback off listen-only off";
  system(cmd.c_str());

  cmd = "ip link set " + ifname + " up";
  system(cmd.c_str());

  return true;
}
#endif

#ifdef USE_LIBCANBUS
bool CANFDMaster::loadLibCanBus() {
  if (libcan_handle_) {
    return true;  // 已加载
  }

  // 先加载 libusb-1.0.so（libcanbus 依赖它）
  libusb_handle_ = dlopen("/usr/local/lib/libusb-1.0.so", RTLD_NOW | RTLD_GLOBAL);
  if (!libusb_handle_) {
    std::cerr << "Failed to load libusb-1.0.so: " << dlerror() << std::endl;
    return false;
  }

  libcan_handle_ = dlopen("/usr/local/lib/libcanbus.so", RTLD_NOW);
  if (!libcan_handle_) {
    std::cerr << "Failed to load libcanbus.so: " << dlerror() << std::endl;
    dlclose(libusb_handle_);
    libusb_handle_ = nullptr;
    return false;
  }

  // 获取函数指针
  fn_CAN_ScanDevice_ = reinterpret_cast<int (*)()>(
      dlsym(libcan_handle_, "CAN_ScanDevice"));
  fn_CAN_OpenDevice_ = reinterpret_cast<int (*)(int, int)>(
      dlsym(libcan_handle_, "CAN_OpenDevice"));
  fn_CAN_CloseDevice_ = reinterpret_cast<int (*)(int, int)>(
      dlsym(libcan_handle_, "CAN_CloseDevice"));
  fn_CANFD_Init_ = reinterpret_cast<int (*)(int, int, void*)>(
      dlsym(libcan_handle_, "CANFD_Init"));
  fn_CANFD_Transmit_ = reinterpret_cast<int (*)(int, int, void*, int, int)>(
      dlsym(libcan_handle_, "CANFD_Transmit"));
  fn_CANFD_Receive_ = reinterpret_cast<int (*)(int, int, void*, int, int)>(
      dlsym(libcan_handle_, "CANFD_Receive"));

  if (!fn_CAN_ScanDevice_ || !fn_CAN_OpenDevice_ || !fn_CAN_CloseDevice_ ||
      !fn_CANFD_Init_ || !fn_CANFD_Transmit_ || !fn_CANFD_Receive_) {
    std::cerr << "Failed to resolve libcanbus symbols" << std::endl;
    unloadLibCanBus();
    return false;
  }

  return true;
}

void CANFDMaster::unloadLibCanBus() {
  fn_CAN_ScanDevice_ = nullptr;
  fn_CAN_OpenDevice_ = nullptr;
  fn_CAN_CloseDevice_ = nullptr;
  fn_CANFD_Init_ = nullptr;
  fn_CANFD_Transmit_ = nullptr;
  fn_CANFD_Receive_ = nullptr;

  if (libcan_handle_) {
    dlclose(libcan_handle_);
    libcan_handle_ = nullptr;
  }
  if (libusb_handle_) {
    dlclose(libusb_handle_);
    libusb_handle_ = nullptr;
  }
}
#endif  // USE_LIBCANBUS

std::vector<std::string> CANFDMaster::scanDevices() {
  std::vector<std::string> devices;

#ifdef USE_LIBCANBUS
  if (!loadLibCanBus()) {
    std::cerr << "Failed to load libcanbus library" << std::endl;
    return devices;
  }
  int count = fn_CAN_ScanDevice_();
  for (int i = 0; i < count; i++) {
    devices.push_back("libcanbus device " + std::to_string(i));
  }
#else
  DIR* dir;
  struct dirent* ent;
  if ((dir = opendir("/sys/class/net")) != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      std::string ifname = ent->d_name;
      if (ifname.substr(0, 3) == "can") {
        devices.push_back(ifname);
      }
    }
    closedir(dir);
  }
#endif

  return devices;
}

bool CANFDMaster::connect(int deviceIndex, int nomBaud, int dataBaud) {
  if (is_connected_) {
    disconnect();
  }

  dev_index_ = deviceIndex;
  nom_baud_ = nomBaud;
  data_baud_ = dataBaud;

#ifdef USE_LIBCANBUS
  if (!libcanbusConnect(deviceIndex, nomBaud, dataBaud)) {
    return false;
  }
#else
  std::vector<std::string> devices = scanDevices();
  if (deviceIndex < 0 || deviceIndex >= (int)devices.size()) {
    return false;
  }

  std::string ifname = devices[deviceIndex];
  current_interface_ = ifname;

  if (!setupCANInterface(ifname, nomBaud, dataBaud)) {
    std::cerr << "Failed to setup CAN interface" << std::endl;
    return false;
  }

  socket_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (socket_fd_ < 0) {
    perror("socket");
    return false;
  }

  int enable_canfd = 1;
  if (setsockopt(socket_fd_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd,
                 sizeof(enable_canfd)) < 0) {
    perror("setsockopt CAN_RAW_FD_FRAMES");
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

  struct ifreq ifr;
  strcpy(ifr.ifr_name, ifname.c_str());
  if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl SIOCGIFINDEX");
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

  struct sockaddr_can addr;
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }
#endif

  is_connected_ = true;
  receive_thread_running_ = true;
  receive_thread_ = std::thread(&CANFDMaster::receiveThreadFunc, this);

  resetLostFrames();
  return true;
}

#ifdef USE_LIBCANBUS
bool CANFDMaster::libcanbusConnect(int deviceIndex, int nomBaud, int dataBaud) {
  if (!loadLibCanBus()) {
    std::cerr << "Failed to load libcanbus library" << std::endl;
    return false;
  }

  int ret = fn_CAN_OpenDevice_(deviceIndex, 0);
  if (ret != 0) {
    std::cerr << "CAN_OpenDevice failed, error: " << ret << std::endl;
    return false;
  }

  LibCanFD_Config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.NomBaud = nomBaud * 1000;
  cfg.DatBaud = dataBaud * 1000;
  cfg.Config = 0x01 | 0x02 | 0x04;  // 终端电阻 + 唤醒 + 重传
  cfg.Cantype = 1;                   // ISO CANFD
  cfg.Model = 0;                     // 正常模式

  // 仲裁段采样点 80%
  cfg.NomPre = 2;
  cfg.NomTseg1 = 31;
  cfg.NomTseg2 = 8;
  cfg.NomSJW = 5;

  // 数据段采样点 75%
  cfg.DatPre = 1;
  cfg.DatTseg1 = 11;
  cfg.DatTseg2 = 4;
  cfg.DatSJW = 2;

  ret = fn_CANFD_Init_(deviceIndex, 0, &cfg);
  if (ret != 0) {
    std::cerr << "CANFD_Init failed, error: " << ret << std::endl;
    fn_CAN_CloseDevice_(deviceIndex, 0);
    return false;
  }

  return true;
}

bool CANFDMaster::libcanbusDisconnect() {
  if (dev_index_ >= 0 && fn_CAN_CloseDevice_) {
    int ret = fn_CAN_CloseDevice_(dev_index_, 0);
    if (ret != 0) {
      std::cerr << "CAN_CloseDevice failed, error: " << ret << std::endl;
      return false;
    }
  }
  return true;
}
#endif  // USE_LIBCANBUS

bool CANFDMaster::disconnect() {
  if (!is_connected_) {
    return true;
  }

  receive_thread_running_ = false;
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

#ifdef USE_LIBCANBUS
  libcanbusDisconnect();
#else
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  current_interface_.clear();
#endif

  dev_index_ = -1;
  is_connected_ = false;
  resetLostFrames();
  return true;
}

bool CANFDMaster::attemptReconnect() {
  disconnect();
  return connect(dev_index_, nom_baud_, data_baud_);
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

#ifdef USE_LIBCANBUS
  return libcanbusSendData(id, data, externflag);
#else
  if (socket_fd_ < 0) {
    return false;
  }

  struct canfd_frame frame;
  memset(&frame, 0, sizeof(frame));

  if (externflag) {
    frame.can_id = id | CAN_EFF_FLAG;
  } else {
    frame.can_id = id;
  }

  frame.len = 64;           // 固定64字节
  frame.flags = CANFD_BRS;  // 使用比特率切换

  size_t copySize = std::min(data.size(), size_t(64));
  memcpy(frame.data, data.data(), copySize);

  int ret = write(socket_fd_, &frame, sizeof(frame));
  bool success = (ret == (int)sizeof(frame));
  if (!success) {
    perror("write");
    lost_frames_++;
  }
  return success;
#endif
}

#ifdef USE_LIBCANBUS
bool CANFDMaster::libcanbusSendData(uint32_t id,
                                    const std::vector<uint8_t>& data,
                                    int externflag) {
  LibCanFD_Msg msg;
  memset(&msg, 0, sizeof(msg));

  msg.ID = id;
  msg.FrameType = 0x04;  // CANFD帧
  msg.DLC = 15;          // 固定64字节
  msg.ExternFlag = externflag;
  msg.RemoteFlag = 0;

  size_t copySize = std::min(data.size(), size_t(64));
  memcpy(msg.Data, data.data(), copySize);

  int ret = fn_CANFD_Transmit_(dev_index_, 0, &msg, 1, 100);
  bool success = (ret == 1);
  if (!success) {
    std::cerr << "CANFD_Transmit failed, ret: " << ret << std::endl;
    lost_frames_++;
  }
  return success;
}
#endif  // USE_LIBCANBUS

void CANFDMaster::setReceiveCallback(ReceiveCallback callback) {
  receive_callback_ = callback;
}

void CANFDMaster::receiveThreadFunc() {
#ifdef USE_LIBCANBUS
  libcanbusReceiveLoop();
#else
  while (receive_thread_running_) {
    if (!is_connected_ || socket_fd_ < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    struct canfd_frame frame;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000;  // 50ms超时

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_fd_, &readfds);

    int ret = select(socket_fd_ + 1, &readfds, NULL, NULL, &timeout);
    if (ret > 0 && FD_ISSET(socket_fd_, &readfds)) {
      ssize_t nbytes = read(socket_fd_, &frame, sizeof(frame));
      if (nbytes == (ssize_t)sizeof(frame)) {
        if (receive_callback_) {
          uint32_t id = frame.can_id & CAN_EFF_MASK;
          if (!(frame.can_id & CAN_EFF_FLAG)) {
            id = frame.can_id & CAN_SFF_MASK;
          }

          int dataLength = frame.len;
          std::vector<uint8_t> rxdata(dataLength);
          memcpy(rxdata.data(), frame.data, dataLength);

          uint64_t timestamp =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();

          receive_callback_(id, rxdata, timestamp);
        }
      } else {
        lost_frames_++;
      }
    } else if (ret < 0) {
      lost_frames_++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
#endif
}

#ifdef USE_LIBCANBUS
void CANFDMaster::libcanbusReceiveLoop() {
  static const int DLC2LEN[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
  const int RECV_BUF_SIZE = 500;
  const int RECV_TIMEOUT_MS = 50;

  LibCanFD_Msg msgs[RECV_BUF_SIZE];

  while (receive_thread_running_) {
    if (!is_connected_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    int count = fn_CANFD_Receive_(dev_index_, 0, msgs, RECV_BUF_SIZE, RECV_TIMEOUT_MS);

    if (count > 0 && receive_callback_) {
      for (int i = 0; i < count; i++) {
        const LibCanFD_Msg& msg = msgs[i];
        int dataLength = (msg.DLC < 16) ? DLC2LEN[msg.DLC] : 64;

        std::vector<uint8_t> rxdata(dataLength);
        memcpy(rxdata.data(), msg.Data, dataLength);

        if (msg.ErrSatus != 0) {
          lost_frames_++;
        }

        receive_callback_(msg.ID, rxdata, msg.TimeStamp);
      }
    } else if (count < 0) {
      lost_frames_++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
#endif  // USE_LIBCANBUS

bool CANFDMaster::isConnected() const {
  return is_connected_;
}

int CANFDMaster::getCurrentDeviceIndex() const {
  return dev_index_;
}

uint64_t CANFDMaster::getLostFrames() const {
  return lost_frames_;
}

void CANFDMaster::resetLostFrames() {
  lost_frames_ = 0;
}

#endif  // _WIN32
