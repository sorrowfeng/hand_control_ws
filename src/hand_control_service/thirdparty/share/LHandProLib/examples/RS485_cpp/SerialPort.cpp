#include "SerialPort.h"

#include <chrono>

#ifdef _WIN32
// Windows headers already included via SerialPort.h
#else
#include <fcntl.h>
#include <glob.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

// =============================================================================
// Windows
// =============================================================================
#ifdef _WIN32

bool SerialPort::open(const std::string& port_name) {
  if (is_open_.load()) return false;

  std::string full_port = "\\\\.\\" + port_name;
  hCom_ = CreateFileA(full_port.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                      nullptr, OPEN_EXISTING, 0, nullptr);
  if (hCom_ == INVALID_HANDLE_VALUE) return false;

  DCB dcb = {};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(hCom_, &dcb)) {
    CloseHandle(hCom_);
    hCom_ = INVALID_HANDLE_VALUE;
    return false;
  }

  dcb.BaudRate = 500000;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;

  if (!SetCommState(hCom_, &dcb)) {
    CloseHandle(hCom_);
    hCom_ = INVALID_HANDLE_VALUE;
    return false;
  }

  // Non-blocking read: ReadFile returns immediately when no data.
  COMMTIMEOUTS timeouts = {};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  SetCommTimeouts(hCom_, &timeouts);

  is_open_.store(true);
  running_ = true;
  read_thread_ = std::thread(&SerialPort::readLoop, this);
  return true;
}

void SerialPort::close() {
  if (!is_open_.load()) return;
  running_ = false;
  if (read_thread_.joinable()) read_thread_.join();
  CloseHandle(hCom_);
  hCom_ = INVALID_HANDLE_VALUE;
  is_open_.store(false);
}

bool SerialPort::platformWrite(const uint8_t* data, size_t size) {
  DWORD written = 0;
  return WriteFile(hCom_, data, (DWORD)size, &written, nullptr) &&
         written == (DWORD)size;
}

int SerialPort::platformRead(uint8_t* buf, int buf_size) {
  DWORD bytes_read = 0;
  if (ReadFile(hCom_, buf, (DWORD)buf_size, &bytes_read, nullptr)) {
    return (int)bytes_read;
  }
  return -1;
}

std::vector<std::string> SerialPort::scanAvailablePorts() {
  std::vector<std::string> ports;

  // 方法1: 从注册表读取系统已注册的所有串口（最可靠）
  HKEY hKey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    char valueName[256];
    BYTE valueData[256];
    DWORD index = 0;
    DWORD nameLen, dataLen, dataType;
    for (;;) {
      nameLen = sizeof(valueName);
      dataLen = sizeof(valueData);
      LONG ret = RegEnumValueA(hKey, index++, valueName, &nameLen, nullptr,
                               &dataType, valueData, &dataLen);
      if (ret != ERROR_SUCCESS) break;
      if (dataType == REG_SZ) {
        std::string portName(reinterpret_cast<char*>(valueData));
        if (!portName.empty()) ports.push_back(portName);
      }
    }
    RegCloseKey(hKey);
  }

  // 方法2: 回退 — 暴力扫描 COM1-COM256
  if (ports.empty()) {
    for (int i = 1; i <= 256; ++i) {
      std::string portName = "COM" + std::to_string(i);
      std::string fullPort = "\\\\.\\" + portName;
      HANDLE h = CreateFileA(fullPort.c_str(), 0, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
      if (h != INVALID_HANDLE_VALUE) {
        ports.push_back(portName);
        CloseHandle(h);
      }
    }
  }

  return ports;
}

// =============================================================================
// Linux
// =============================================================================
#else

bool SerialPort::open(const std::string& port_name) {
  if (is_open_.load()) return false;

  fd_ = ::open(port_name.c_str(), O_RDWR | O_NOCTTY);
  if (fd_ < 0) return false;

  struct termios tty;
  if (tcgetattr(fd_, &tty) != 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  cfsetospeed(&tty, B500000);
  cfsetispeed(&tty, B500000);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK);
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  is_open_.store(true);
  running_ = true;
  read_thread_ = std::thread(&SerialPort::readLoop, this);
  return true;
}

void SerialPort::close() {
  if (!is_open_.load()) return;
  running_ = false;
  if (read_thread_.joinable()) read_thread_.join();
  ::close(fd_);
  fd_ = -1;
  is_open_.store(false);
}

bool SerialPort::platformWrite(const uint8_t* data, size_t size) {
  return ::write(fd_, data, size) == (ssize_t)size;
}

int SerialPort::platformRead(uint8_t* buf, int buf_size) {
  // Use select() with 1ms timeout to avoid busy-waiting.
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(fd_, &read_fds);
  struct timeval tv = {0, 1000};
  int ret = select(fd_ + 1, &read_fds, nullptr, nullptr, &tv);
  if (ret > 0) {
    return (int)::read(fd_, buf, (size_t)buf_size);
  }
  return 0;
}

std::vector<std::string> SerialPort::scanAvailablePorts() {
  std::vector<std::string> ports;

  glob_t g = {};
  if (glob("/dev/tty*", 0, nullptr, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc; ++i) {
      int fd = ::open(g.gl_pathv[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
      if (fd >= 0) {
        struct termios tty;
        if (tcgetattr(fd, &tty) == 0) {
          ports.push_back(g.gl_pathv[i]);
        }
        ::close(fd);
      }
    }
  }
  globfree(&g);

  return ports;
}

#endif  // _WIN32

// =============================================================================
// Platform-independent
// =============================================================================

bool SerialPort::write(const uint8_t* data, size_t size) {
  if (!is_open_.load() || !data || size == 0) return false;
  std::lock_guard<std::mutex> lock(write_mutex_);
  return platformWrite(data, size);
}

void SerialPort::readLoop() {
  uint8_t chunk[1024];

  while (running_) {
    if (!is_open_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    int n = platformRead(chunk, sizeof(chunk));
    if (n <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    if (read_callback_)
      read_callback_(chunk, static_cast<size_t>(n));
  }
}
