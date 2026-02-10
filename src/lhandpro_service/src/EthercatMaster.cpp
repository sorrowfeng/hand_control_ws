// EthercatMaster.cpp
#include "EthercatMaster.h"

#include <cstring>
#include <iomanip>
#include <iostream>

#if ETHERCAT_DEBUG
#define ECOUT std::cout
#else
#define ECOUT \
  if (0)      \
  std::cout
#endif

// 平台特定的网口过滤关键词
#ifdef _WIN32
// Windows特定关键词
static const std::vector<std::string> EXCLUDE_KEYWORDS = {
    "wan miniport",  // Windows WAN Miniport
    "wi-fi",         // Windows WiFi
    "wifi",          // WiFi接口
    "wireless",      // 无线
    "bluetooth",     // 蓝牙
    "vmware",        // VMware虚拟网卡
    "virtual",       // 虚拟设备
    "loopback",      // 回环(Windows)
    "tap-",          // TAP虚拟设备
    "vpn",           // VPN接口
    "wintun",        // Windows TUN设备
    "teredo",        // Teredo隧道
    "isatap"         // ISATAP隧道
};
#else
// Linux特定关键词
static const std::vector<std::string> EXCLUDE_KEYWORDS = {
    "lo",       // Linux 回环接口
    "docker",   // Docker虚拟网卡
    "veth",     // 虚拟以太网设备
    "br-",      // 网桥接口
    "virbr",    // 虚拟网桥
    "vmnet",    // VMware虚拟网卡
    "tap",      // TAP虚拟设备
    "tun",      // TUN虚拟设备
    "wlan",     // 无线网卡
    "wlp",      // 无线网卡(新命名)
    "wlx",      // 无线网卡
    "wwan",     // 无线广域网
    "vboxnet",  // VirtualBox虚拟网卡
    "p2p",      // P2P连接
    "teredo",   // Teredo隧道
    "isatap"    // ISATAP隧道
};
#endif

std::string SlaveInfo::toString() const {
  // 状态字符串映射
  const char* state_str;
  switch (state & 0x0F) {
    case EC_STATE_INIT:
      state_str = "INIT";
      break;
    case EC_STATE_PRE_OP:
      state_str = "PRE_OP";
      break;
    case EC_STATE_SAFE_OP:
      state_str = "SAFE_OP";
      break;
    case EC_STATE_OPERATIONAL:
      state_str = "OP";
      break;
    default:
      state_str = "UNKNOWN";
      break;
  }

  char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "Slave %2d [%-16s] State: %8s (0x%02X) AL: 0x%04X (%s)%s", index,
           name.substr(0, 16).c_str(), state_str, state, al_status,
           al_status_str.c_str(), is_lost ? " [LOST]" : "");

  return std::string(buffer);
}

std::ostream& operator<<(std::ostream& os, const SlaveInfo& info) {
  return os << info.toString();
}

EthercatMaster::EthercatMaster()
    : group_(0),
      roundtrip_time_(0),
      initialized_(false),
      started_(false),
      running_(false),
      outputs_(nullptr),
      inputs_(nullptr),
      output_bytes_(0),
      input_bytes_(0) {
  memset(&context_, 0, sizeof(context_));
}

EthercatMaster::~EthercatMaster() {
  if (running_) {
    stop();
  }
  if (initialized_) {
    ecx_close(&context_);
  }
}

std::vector<std::string> EthercatMaster::scanNetworkInterfaces() {
  interfaces_.clear();
  std::vector<std::string> descrptions;
  ec_adaptert* adapter = ec_find_adapters();
  ec_adaptert* head = adapter;

  ECOUT << "Available adapters:\n";
  while (adapter != nullptr) {
    // 检查是否需要排除
    bool should_exclude = false;
    std::string adapter_name(adapter->name);
    std::string adapter_desc(adapter->desc);
    std::string adapter_name_lower = adapter_name;
    std::string adapter_desc_lower = adapter_desc;

    // 转换为小写便于比较
    for (char& c : adapter_name_lower) {
      c = std::tolower(static_cast<unsigned char>(c));
    }
    for (char& c : adapter_desc_lower) {
      c = std::tolower(static_cast<unsigned char>(c));
    }

    for (const std::string& keyword : EXCLUDE_KEYWORDS) {
      // 检查名称
      if (adapter_name_lower.find(keyword) != std::string::npos) {
        should_exclude = true;
        break;
      }
#ifdef _WIN32
      // 检查描述
      if (adapter_desc_lower.find(keyword) != std::string::npos) {
        should_exclude = true;
        break;
      }
#endif
    }

    if (!should_exclude) {
      ECOUT << "    - " << adapter->name << "  (" << adapter->desc << ")\n";
      interfaces_.push_back(std::string(adapter->name));
      descrptions.push_back(std::string(adapter->desc));
    }

    adapter = adapter->next;
  }
  ec_free_adapters(head);

  return descrptions;
}

bool EthercatMaster::init(int index) {
  if (index < 0 || index >= interfaces_.size()) {
    ECOUT << "Index not available\n";
    return false;
  }

  iface_ = interfaces_.at(index);

  ECOUT << "Initializing SOEM on '" << iface_ << "'... " << std::flush;
  if (!ecx_init(&context_, iface_.c_str())) {
    ECOUT << "no socket connection\n";
    return false;
  }
  ECOUT << "done\n";

  ECOUT << "Finding autoconfig slaves... " << std::flush;
  if (ecx_config_init(&context_) <= 0) {
    ECOUT << "no slaves found\n";
    return false;
  }
  ECOUT << context_.slavecount << " slaves found\n";

  ECOUT << "Sequential mapping of I/O... " << std::flush;
  ecx_config_map_group(&context_, map_, group_);
  ec_groupt* grp = context_.grouplist + group_;
  output_bytes_ = grp->Obytes;
  input_bytes_ = grp->Ibytes;
  outputs_ = grp->outputs;
  inputs_ = grp->inputs;

  //  初始化双缓冲区大小
  output_buffer_.resize(output_bytes_);

  ECOUT << "mapped " << grp->Obytes << "O+" << grp->Ibytes << "I bytes from "
        << grp->nsegments << " segments";

  if (grp->nsegments > 1) {
    ECOUT << " (";
    for (int i = 0; i < grp->nsegments; ++i) {
      if (i > 0)
        ECOUT << "+";
      ECOUT << grp->IOsegment[i];
    }
    ECOUT << " slaves)";
  }
  ECOUT << "\n";

  ECOUT << "Configuring distributed clock... " << std::flush;
  ecx_configdc(&context_);
  ECOUT << "done\n";

  connected_index_ = index;
  initialized_ = true;
  return true;
}

bool EthercatMaster::start() {
  if (!initialized_)
    return false;

  // ec_groupt* grp = context_.grouplist + group_;
  ec_slavet* slave = context_.slavelist;

  ECOUT << "Waiting for all slaves in safe operational... " << std::flush;
  ecx_statecheck(&context_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
  ECOUT << "done\n";

  ECOUT << "Send a roundtrip to make outputs in slaves happy... " << std::flush;
  ec_timet start = osal_current_time();
  ecx_send_processdata(&context_);
  ecx_receive_processdata(&context_, EC_TIMEOUTRET);
  ec_timet end = osal_current_time();
  ec_timet diff;
  osal_time_diff(&start, &end, &diff);
  roundtrip_time_ = (int)(diff.tv_sec * 1000000 + diff.tv_nsec / 1000);
  ECOUT << "done\n";

  ECOUT << "Setting operational state.." << std::flush;

  slave->state = EC_STATE_OPERATIONAL;
  ecx_writestate(&context_, 0);

  for (int i = 0; i < 10; ++i) {
    ECOUT << "." << std::flush;
    start = osal_current_time();
    ecx_send_processdata(&context_);
    ecx_receive_processdata(&context_, EC_TIMEOUTRET);
    end = osal_current_time();
    osal_time_diff(&start, &end, &diff);
    roundtrip_time_ = (int)(diff.tv_sec * 1000000 + diff.tv_nsec / 1000);

    ecx_statecheck(&context_, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
    if (slave->state == EC_STATE_OPERATIONAL) {
      ECOUT << " all slaves are now operational\n";
      started_ = true;
      is_reconnecting_.exchange(false);
      resetLostFrames();
      return true;
    }
  }

  ECOUT << " failed,";
  ecx_readstate(&context_);
  for (int i = 1; i <= context_.slavecount; ++i) {
    slave = context_.slavelist + i;
    if (slave->state != EC_STATE_OPERATIONAL) {
      ECOUT << " slave " << i << " is 0x" << std::hex << std::setfill('0')
            << std::setw(4) << slave->state << " (AL-status=0x" << std::setw(4)
            << slave->ALstatuscode << std::dec << " "
            << ec_ALstatuscode2string(slave->ALstatuscode) << ")";
    }
  }
  ECOUT << "\n";

  return false;
}

void EthercatMaster::run() {
  if (!started_ || running_) {
    return;
  }

  running_ = true;

  worker_thread_ = std::thread([this]() {
    int min_time = 0, max_time = 0;
    int iteration = 1;
    int consecutive_lost_frames = 0;     // 连续丢帧计数
    constexpr int max_lost_frames = 10;  // 最大断线丢帧判断数
    bool is_reconnecting = false;        // 重连状态标志
    int reconnect_counter = 0;           // 重连计数器

    while (running_) {
      // 如果正在重连中
      if (is_reconnecting) {
        reconnect_counter++;
        lost_frames_++;  // 增加丢帧计数

        // 每200ms尝试重连一次 (40个周期)
        if (reconnect_counter >= 40) {
          ECOUT << "Attempting to reconnect... ";
          if (attemptReconnect()) {
            ECOUT << "Success!\n";
            is_reconnecting = false;
            consecutive_lost_frames = 0;
            reconnect_counter = 0;
            iteration = 1;
          } else {
            ECOUT << "Failed\n";
            reconnect_counter = 0;
          }
        } else {
          osal_usleep(5000);
        }
        continue;
      }

      ECOUT << "Iteration " << std::setw(4) << std::setfill(' ') << iteration
            << ":";

      //  检测是否有新数据，有则同步到 SOEM 输出缓冲区
      if (uint8_t* latest = output_buffer_.getLatest()) {
        std::lock_guard<std::mutex> lock(io_mutex_);
        std::memcpy(outputs_, latest, output_bytes_);
      }

      ec_timet start = osal_current_time();
      ecx_send_processdata(&context_);
      int wkc = ecx_receive_processdata(&context_, EC_TIMEOUTRET);
      ec_timet end = osal_current_time();
      ec_timet diff;
      osal_time_diff(&start, &end, &diff);
      roundtrip_time_ = (int)(diff.tv_sec * 1000000 + diff.tv_nsec / 1000);

      // 初始化期望的WKC值
      ec_groupt* grp = context_.grouplist + group_;
      int expected_wkc = grp->outputsWKC * 2 + grp->inputsWKC;

      // 统计丢帧：WKC小于(期望值-1, 增加容错)就认为是丢帧
      if (wkc < expected_wkc - 1) {
        if (lost_frames_ > 100000) {
          lost_frames_ = 0;
          return;
        }
        lost_frames_++;             // 增加丢帧计数
        consecutive_lost_frames++;  // 增加连续丢帧计数

        ECOUT << std::setw(6) << roundtrip_time_ << " usec  WKC " << wkc;
        ECOUT << " wrong (expected " << expected_wkc
              << "), consecutive: " << consecutive_lost_frames << "\n";

        // 连续丢帧超过10次，开始重连
        if (consecutive_lost_frames >= max_lost_frames) {
          ECOUT << "Connection lost! Starting auto-reconnect...\n";
          is_reconnecting = true;
          reconnect_counter = 0;
        }
      } else {
        // WKC正常，重置连续丢帧计数
        consecutive_lost_frames = 0;

        ECOUT << std::setw(6) << roundtrip_time_ << " usec  WKC " << wkc;
        ECOUT << "  O:";
        for (int n = 0; n < grp->Obytes; ++n) {
          ECOUT << " " << std::hex << std::setw(2) << std::setfill('0')
                << (int)grp->outputs[n] << std::dec;
        }
        ECOUT << "  I:";
        for (int n = 0; n < grp->Ibytes; ++n) {
          ECOUT << " " << std::hex << std::setw(2) << std::setfill('0')
                << (int)grp->inputs[n] << std::dec;
        }
        ECOUT << "  T: " << std::dec << context_.DCtime << "\r" << std::flush;
      }

      if (iteration == 1) {
        min_time = max_time = roundtrip_time_;
      } else {
        if (roundtrip_time_ < min_time)
          min_time = roundtrip_time_;
        if (roundtrip_time_ > max_time)
          max_time = roundtrip_time_;
      }

      iteration++;
      osal_usleep(5000);
    }

    ECOUT << "\nRoundtrip time (usec): min " << min_time << " max " << max_time
          << "\n";
  });
}

bool EthercatMaster::attemptReconnect() {
  ECOUT << "Starting reconnection process";
  is_reconnecting_.exchange(true);
  // 只停止通信，不停止工作线程
  if (started_) {
    ECOUT << "Requesting init state on all slaves... " << std::flush;
    ec_slavet* slave = context_.slavelist;
    slave->state = EC_STATE_INIT;
    ecx_writestate(&context_, 0);
    ECOUT << "done\n";
    started_ = false;
    // 发射信号（异步）
    if (stateSwitchCallback_) {
      // 发送已断开, 只在持续尝试重连信号
      std::thread([this]() {
        stateSwitchCallback_((int)EthercatMaster::EthercatState::Reconnecting,
                             getLostFrames());
      }).detach();  // 分离线程，让其独立运行
    }
  }

  if (initialized_) {
    ECOUT << "Closing socket... " << std::flush;
    ecx_close(&context_);
    ECOUT << "done\n";
    initialized_ = false;
  }

  // 提前获取需要传递的值
  uint64_t lostFrames = getLostFrames();

  // 重新初始化
  bool init_success = init(connected_index_);
  if (!init_success) {
    ECOUT << "Reconnect init failed";
    return false;
  }

  // 重新启动
  bool start_success = start();
  if (!start_success) {
    ECOUT << "Reconnect start failed";
    // 启动失败时清理
    if (initialized_) {
      ecx_close(&context_);
      initialized_ = false;
    }
    return false;
  }

  // 发射信号（异步）
  if (stateSwitchCallback_) {
    // 发送已成功重连信号
    std::thread([this, lostFrames]() {
      stateSwitchCallback_((int)EthercatMaster::EthercatState::Operational,
                           lostFrames);
    }).detach();  // 分离线程，让其独立运行
  }

  resetLostFrames();
  is_reconnecting_.exchange(false);
  ECOUT << "Reconnect successful";
  return true;
}

void EthercatMaster::setStateSwitchCallback(StateSwitchCallback callback) {
  stateSwitchCallback_ = callback;
}

EthercatMaster::EthercatState EthercatMaster::getState() const {
  if (!running_ || !initialized_) {
    return EthercatState::Disconnected;
  }

  if (is_reconnecting_) {
    return EthercatState::Reconnecting;
  }

  EthercatMaster* mutable_this = const_cast<EthercatMaster*>(this);
  ecx_readstate(&mutable_this->context_);

  // 查找从站
  const ec_slavet* target_slave = nullptr;
  for (int i = 1; i <= context_.slavecount; ++i) {
    const ec_slavet* slave = context_.slavelist + i;
    if (slave->group == group_) {
      target_slave = slave;
      break;
    }
  }

  if (target_slave == nullptr) {
    return EthercatState::Disconnected;
  }

  // 针对单个从站的稳定状态检测
  uint16_t slave_state = target_slave->state;

  // 错误状态优先级最高
  if (slave_state & EC_STATE_ERROR) {
    return EthercatState::Error;
  }

  // NONE状态处理 - 增加重试机制
  static int none_state_count = 0;
  constexpr int MAX_NONE_COUNT = 3;  // 连续3次NONE状态才认为是断开

  if (slave_state == EC_STATE_NONE) {
    none_state_count++;
    if (none_state_count >= MAX_NONE_COUNT) {
      none_state_count = 0;  // 重置计数器
      return EthercatState::Disconnected;
    }
    return EthercatState::Reconnecting;
  } else {
    none_state_count = 0;  // 正常状态时重置计数器
  }

  // 正常状态转换
  switch (slave_state) {
    case EC_STATE_INIT:
    case EC_STATE_PRE_OP:
      return EthercatState::Initializing;

    case EC_STATE_SAFE_OP:
      return EthercatState::SafeOperational;

    case EC_STATE_OPERATIONAL:
      return EthercatState::Operational;

    default:
      return EthercatState::Initializing;
  }
}

SlaveInfo EthercatMaster::getSlaveInfo() const {
  SlaveInfo info{};

  if (context_.slavecount < 1)
    return info;

  const ec_slavet* slave = context_.slavelist + 1;

  if (slave->group != group_)
    return info;

  info.index = 1;
  info.name = std::string(slave->name);
  info.state = slave->state;
  info.al_status = slave->ALstatuscode;
  info.al_status_str = std::string(ec_ALstatuscode2string(slave->ALstatuscode));
  info.is_lost = slave->islost;

  return info;
}

uint64_t EthercatMaster::getLostFrames() const {
  return lost_frames_;
}

void EthercatMaster::resetLostFrames() {
  lost_frames_ = 0;
}

void EthercatMaster::stop() {
  if (running_) {
    ECOUT << "Stopping EtherCAT thread... " << std::flush;
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    ECOUT << "done\n";

    // 显示最终的丢帧统计
    ECOUT << "Total lost frames: " << lost_frames_ << "\n";
  }

  if (started_) {
    ec_slavet* slave = context_.slavelist;
    ECOUT << "Requesting init state on all slaves... " << std::flush;
    slave->state = EC_STATE_INIT;
    ecx_writestate(&context_, 0);
    ECOUT << "done\n";
    started_ = false;
  }

  if (initialized_) {
    ECOUT << "Close socket... " << std::flush;
    ecx_close(&context_);
    ECOUT << "done\n";
    initialized_ = false;
  }

  resetLostFrames();
}

void EthercatMaster::setOutput(int index, uint8_t value) {
  if (!started_ || outputs_ == nullptr)
    return;
  if (index >= 0 && index < output_bytes_) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    outputs_[index] = value;
  }
}

uint8_t EthercatMaster::getInput(int index) {
  if (!started_ || inputs_ == nullptr)
    return 0;
  if (index >= 0 && index < input_bytes_) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    return inputs_[index];
  }
  return 0;
}

bool EthercatMaster::setOutputs(const uint8_t* data, unsigned int len) {
  if (!started_ || !data || len <= 0)
    return false;
  if (len > output_bytes_) {
    ECOUT << "Error: setOutputs length " << len
          << " exceeds configured output bytes " << output_bytes_ << "\n";
    return false;
  }

  // 保证数据能在下一周期发出
  return output_buffer_.write(data, len);
}

bool EthercatMaster::getInputs(uint8_t* buffer, unsigned int len) {
  if (!started_ || !buffer || len <= 0)
    return false;
  if (len > input_bytes_) {
    ECOUT << "Error: getInputs buffer length " << len
          << " exceeds configured input bytes " << input_bytes_ << "\n";
    return false;
  }

  std::lock_guard<std::mutex> lock(io_mutex_);
  memcpy(buffer, inputs_, len);
  return true;
}

bool EthercatMaster::getOutputs(uint8_t* buffer, unsigned int len) {
  if (!started_ || !buffer || len <= 0)
    return false;
  if (len > output_bytes_) {
    ECOUT << "Error: getOutputs buffer length " << len
          << " exceeds configured input bytes " << output_bytes_ << "\n";
    return false;
  }

  std::lock_guard<std::mutex> lock(io_mutex_);
  memcpy(buffer, outputs_, len);
  return true;
}

int EthercatMaster::getOutputSize() const {
  return output_bytes_;
}

int EthercatMaster::getInputSize() const {
  return input_bytes_;
}

bool EthercatMaster::sdoRead(uint16_t index, uint8_t subindex,
                             uint32_t* value) {
  if (!initialized_) {
    ECOUT << "SOEM not initialized\n";
    return false;
  }

  const int slave_id = 1;
  uint8_t buffer[4];
  int size = sizeof(buffer);

  int result = ecx_SDOread(&context_, slave_id, index, subindex, false, &size,
                           buffer, EC_TIMEOUTRXM * 3);

  // 检查执行结果
  if (result <= 0) {
    ECOUT << "SDO Read [Slave " << slave_id << " 0x" << std::setfill('0')
          << std::setw(4) << std::hex << index << ":" << std::setw(2)
          << static_cast<int>(subindex) << "] failed. Error: " << result
          << "\n";
    return false;
  }
  if (size < sizeof(uint32_t)) {  // 检查实际读取的数据长度
    ECOUT << "SDO data too short (Expected 4, got " << size << ")\n";
    return false;
  }

  // 拷贝并转换字节序（小端→主机序）
  *value = (static_cast<uint32_t>(buffer[3]) << 24) |
           (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[1]) << 8) | buffer[0];

  return true;
}

bool EthercatMaster::sdoWrite(uint16_t index, uint8_t subindex,
                              uint32_t value) {
  if (!initialized_) {
    ECOUT << "SOEM not initialized\n";
    return false;
  }

  const int slave_id = 1;
  uint8_t buffer[4];

  // 主机序转小端字节序（适用于EtherCAT设备）
  buffer[0] = static_cast<uint8_t>(value & 0xFF);  // LSB
  buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  buffer[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  buffer[3] = static_cast<uint8_t>((value >> 24) & 0xFF);  // MSB

  int size = sizeof(buffer);
  int result = ecx_SDOwrite(&context_, slave_id, index, subindex, FALSE, size,
                            buffer, EC_TIMEOUTRXM * 3);

  // 检查执行结果（ecx_SDOwrite返回1表示成功）
  if (result != 1) {
    ECOUT << "SDO Write [Slave " << slave_id << " 0x" << std::setfill('0')
          << std::setw(4) << std::hex << index << ":" << std::setw(2)
          << static_cast<int>(subindex) << "] failed. Error: " << result
          << " (Value: 0x" << std::setw(8) << value << ")\n";
    return false;
  }

  return true;
}