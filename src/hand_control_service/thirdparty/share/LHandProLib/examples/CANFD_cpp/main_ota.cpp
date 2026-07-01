// ============================================================================
// 文件说明:
// 该示例演示如何通过 C++ SDK 在 CANFD 主站下进行固件升级 (OTA)。
// 连接后选择 node id，然后输入固件文件路径开始升级。
// ============================================================================

#ifdef _WIN32
#define NOMINMAX
#endif

#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CANFDMaster.h"
#ifndef SDK_CPP_HEADER
#define SDK_CPP_HEADER "LHandProLib.hpp"
#endif
#include SDK_CPP_HEADER

#ifndef SDK_NS
#define SDK_NS lhplib
#endif
#ifndef SDK_CLASS
#define SDK_CLASS LHandProLib
#endif
#ifdef _WIN32
#include <conio.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

constexpr int kOtaResponseTimeoutMs = 1000;
constexpr int kOtaMaxRetryCount = 50;
constexpr int kOtaRetryIntervalMs = 100;
constexpr int kOtaDataPacketSize = 32;
constexpr int kOtaMaxPacketsPerBatch = 16;

void wait_before_exit() {
#ifdef _WIN32
  system("pause");
#else
  std::cin.get();
#endif
}

int select_number_in_range(const std::string& prompt, int min_value,
                           int max_value, int default_value) {
  std::string line;
  while (true) {
    std::cout << prompt << " [" << min_value << " - " << max_value << "]"
              << std::endl;
    if (!std::getline(std::cin, line)) {
      std::cin.clear();
      continue;
    }
    if (line.empty()) {
      return default_value;
    }
    try {
      int value = std::stoi(line);
      if (value >= min_value && value <= max_value) {
        return value;
      }
    } catch (...) {
    }
    std::cout << "Input out of range, please try again." << std::endl;
  }
}

int select_number_in_range(const std::string& prompt, int min_value,
                           int max_value) {
  return select_number_in_range(prompt, min_value, max_value, min_value - 1);
}

int send_ota_and_wait(SDK_NS::SDK_CLASS& lib,
                      const std::vector<uint8_t>& sendData,
                      std::vector<uint8_t>& responseData, int timeoutMs) {
  std::cout << "[OTA] >>> SEND " << sendData.size() << " bytes:";
  for (size_t i = 0; i < std::min(sendData.size(), size_t(16)); ++i)
    std::cout << " " << std::uppercase << std::hex << std::setfill('0')
              << std::setw(2) << static_cast<int>(sendData[i]);
  if (sendData.size() > 16)
    std::cout << " ...";
  std::cout << std::dec << std::endl;

  lib.set_ota_send_data(sendData.data(),
                        static_cast<unsigned int>(sendData.size()));

  responseData.clear();
  auto startTime = std::chrono::steady_clock::now();
  while (true) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - startTime)
                       .count();
    if (elapsed >= timeoutMs) {
      std::cout << "[OTA] <<< RECV timeout after " << timeoutMs << "ms"
                << std::endl;
      return -2;
    }

    int recvLen = 0;
    lib.get_ota_recv_data(nullptr, &recvLen);
    if (recvLen > 0) {
      responseData.resize(static_cast<size_t>(recvLen));
      lib.get_ota_recv_data(responseData.data(), &recvLen);
      if (!responseData.empty()) {
        std::cout << "[OTA] <<< RECV " << responseData.size() << " bytes:";
        for (size_t i = 0; i < std::min(responseData.size(), size_t(16)); ++i)
          std::cout << " " << std::uppercase << std::hex << std::setfill('0')
                    << std::setw(2) << static_cast<int>(responseData[i]);
        if (responseData.size() > 16)
          std::cout << " ...";
        std::cout << std::dec << std::endl;
        return 0;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

int run_ota_upgrade(SDK_NS::SDK_CLASS& lib,
                    const std::string& firmwarePath) {
  std::ifstream file(firmwarePath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "[OTA] Cannot open firmware file: " << firmwarePath
              << std::endl;
    return -1;
  }

  std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
  file.close();

  if (fileData.empty()) {
    std::cerr << "[OTA] Firmware file is empty." << std::endl;
    return -1;
  }

  std::string fileName = firmwarePath;
  auto pos = firmwarePath.find_last_of("/\\");
  if (pos != std::string::npos)
    fileName = firmwarePath.substr(pos + 1);

  std::cout << "[OTA] Firmware: " << fileName
            << " Size: " << fileData.size() << " bytes" << std::endl;

  std::vector<uint8_t> sendData;
  std::vector<uint8_t> responseData;

  // ── Step 1: Trigger burn ──────────────────────────────────────────────
  bool success = false;
  for (int retry = 0; retry < kOtaMaxRetryCount; ++retry) {
    std::cout << "[OTA] Step 1: Trigger burn, attempt " << retry + 1 << " / "
              << kOtaMaxRetryCount << std::endl;

    sendData.clear();
    sendData.push_back(0xFF);
    int length = static_cast<int>(fileName.length()) + 3;
    sendData.push_back(static_cast<uint8_t>(length));
    sendData.push_back(0x00);
    sendData.push_back(0x50);
    sendData.push_back(0x00);
    for (char ch : fileName)
      sendData.push_back(static_cast<uint8_t>(ch));

    int retn =
        send_ota_and_wait(lib, sendData, responseData, kOtaRetryIntervalMs);
    if (retn == 0 && responseData.size() >= 6 && responseData[0] == 0xFF &&
        responseData[4] == 0x50) {
      if (responseData[5] == 0x00) {
        success = true;
        break;
      }
      if (responseData[5] == 0xEE) {
        std::cerr << "[OTA] Step 1: Device returned file error (0xEE)"
                  << std::endl;
        return -1;
      }
    } else if (retn != -2) {
      std::cerr << "[OTA] Step 1: send failed, retn=" << retn << std::endl;
      return -1;
    }
    std::cout << "[OTA] Step 1: No response, retrying..." << std::endl;
  }

  if (!success) {
    std::cerr << "[OTA] Step 1: Trigger burn timeout." << std::endl;
    return -1;
  }
  std::cout << "[OTA] Step 1: Trigger burn succeeded." << std::endl;

  // ── Steps 2-4: Product code, version, prepare burn ────────────────────
  struct StepDef {
    int step;
    uint8_t cmd;
    uint8_t cmd2;
    const char* name;
    uint8_t expect_ack;
  };
  const StepDef steps[] = {
      {2, 0x51, 0x00, "Read product code", 0},
      {3, 0x52, 0x00, "Read version", 0},
      {4, 0x53, 0x02, "Prepare to burn", 0x5A},
  };

  for (const auto& s : steps) {
    success = false;
    std::cout << "[OTA] Step " << s.step << ": " << s.name << std::endl;
    for (int retry = 0; retry < kOtaMaxRetryCount; ++retry) {
      sendData.clear();
      sendData.push_back(0xFF);
      sendData.push_back(0x03);
      sendData.push_back(0x00);
      sendData.push_back(s.cmd);
      sendData.push_back(s.cmd2);

      int retn = send_ota_and_wait(lib, sendData, responseData,
                                   kOtaResponseTimeoutMs);
      if (retn == 0 && responseData.size() >= 6 && responseData[0] == 0xFF &&
          responseData[4] == s.cmd) {
        if (s.expect_ack == 0 || responseData[5] == s.expect_ack) {
          success = true;
          break;
        }
        std::cerr << "[OTA] Step " << s.step << ": Expected 0x" << std::hex
                  << static_cast<int>(s.expect_ack) << " got 0x"
                  << static_cast<int>(responseData[5]) << std::dec << std::endl;
      } else if (retn != 0 && retn != -2) {
        std::cerr << "[OTA] Step " << s.step << ": " << s.name
                  << " send failed." << std::endl;
        return -1;
      }
    }
    if (!success) {
      std::cerr << "[OTA] Step " << s.step << ": " << s.name << " timeout."
                << std::endl;
      return -1;
    }
    std::cout << "[OTA] Step " << s.step << ": " << s.name << " succeeded."
              << std::endl;
  }

  // ── Step 5: Send firmware data ────────────────────────────────────────
  int totalPackets =
      (static_cast<int>(fileData.size()) + kOtaDataPacketSize - 1) /
      kOtaDataPacketSize;
  int currentPacket = 0;
  std::cout << "[OTA] Step 5: Send firmware data, " << totalPackets
            << " packets." << std::endl;

  while (currentPacket < totalPackets) {
    int packetsToSend =
        std::min(kOtaMaxPacketsPerBatch, totalPackets - currentPacket);

    for (int i = 0; i < packetsToSend; ++i) {
      int packetIndex = currentPacket + i;
      int startPos = packetIndex * kOtaDataPacketSize;
      int endPos = std::min(startPos + kOtaDataPacketSize,
                            static_cast<int>(fileData.size()));

      sendData.clear();
      sendData.push_back(0xFF);
      sendData.push_back(0x23);
      sendData.push_back(0x00);
      sendData.push_back((packetIndex == totalPackets - 1) ? 0xAB : 0xAA);
      sendData.push_back(static_cast<uint8_t>(i));
      for (int j = startPos; j < endPos; ++j)
        sendData.push_back(fileData[j]);
      while (sendData.size() < 5 + kOtaDataPacketSize)
        sendData.push_back(0x00);

      std::cout << "[OTA] >>> SEND pkt " << packetIndex << "/" << totalPackets - 1
                << " " << sendData.size() << " bytes:";
      for (size_t k = 0; k < std::min(sendData.size(), size_t(8)); ++k)
        std::cout << " " << std::uppercase << std::hex << std::setfill('0')
                  << std::setw(2) << static_cast<int>(sendData[k]);
      std::cout << " ..." << std::dec << std::endl;

      lib.set_ota_send_data(sendData.data(),
                            static_cast<unsigned int>(sendData.size()));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Wait for batch response
    auto startTime = std::chrono::steady_clock::now();
    bool responseReceived = false;
    while (true) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - startTime)
                         .count();
      if (elapsed >= kOtaResponseTimeoutMs)
        break;

      int recvLen = 0;
      lib.get_ota_recv_data(nullptr, &recvLen);
      if (recvLen > 0) {
        responseData.resize(static_cast<size_t>(recvLen));
        lib.get_ota_recv_data(responseData.data(), &recvLen);
        if (!responseData.empty()) {
          responseReceived = true;
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!responseReceived) {
      std::cerr << "\n[OTA] Step 5: Batch response timeout, packets "
                << currentPacket << "-" << currentPacket + packetsToSend - 1
                << std::endl;
      return -1;
    }

    if (responseData.size() < 5 || responseData[0] != 0xFF) {
      std::cerr << "\n[OTA] Step 5: Invalid response format." << std::endl;
      return -1;
    }

    bool isLastBatch = (currentPacket + packetsToSend >= totalPackets);
    if (isLastBatch) {
      if (responseData[4] != 0xAB || responseData[5] != 0x5A) {
        std::cerr << "\n[OTA] Step 5: Last batch response error." << std::endl;
        return -1;
      }
    } else {
      if (responseData[4] != 0xAA) {
        std::cerr << "\n[OTA] Step 5: Expected 0xAA response, got 0x"
                  << std::hex << static_cast<int>(responseData[4]) << std::dec
                  << std::endl;
        return -1;
      }
      if (responseData[5] == 0x55) {
        std::cerr << "\n[OTA] Step 5: Device reported write failure (0x55)."
                  << std::endl;
        return -1;
      } else if (responseData[5] != 0x5A) {
        std::cerr << "\n[OTA] Step 5: Unknown status code 0x" << std::hex
                  << static_cast<int>(responseData[5]) << std::dec << std::endl;
        return -1;
      }
    }

    currentPacket += packetsToSend;
    int progress = static_cast<int>(
        (static_cast<float>(currentPacket) / totalPackets) * 100);
    std::cout << "\r[OTA] Progress: " << progress << "%" << std::flush;
  }

  std::cout << std::endl
            << "[OTA] Firmware upgrade completed successfully!" << std::endl;
  return 0;
}

}  // namespace

int main() {
  auto sdk_handle = std::make_shared<SDK_NS::SDK_CLASS>();
  auto canfd_master = std::make_shared<CANFDMaster>();
  int return_code = 0;

  // ── 1. Scan and select CANFD channel ──────────────────────────────────
  std::cout << "CANFD OTA Firmware Upgrade Test" << std::endl << std::endl;

  const std::vector<std::string> channel_names = canfd_master->scanDevices();
  std::cout << "Detected CANFD channels: " << channel_names.size() << std::endl
            << std::endl;

  if (channel_names.empty()) {
    wait_before_exit();
    return 0;
  }

  for (size_t i = 0; i < channel_names.size(); ++i) {
    std::cout << "Input " << i << " ---- channel " << channel_names[i]
              << std::endl;
  }

  const int channel_index =
      select_number_in_range("\nPlease select the CANFD channel", 0,
                             static_cast<int>(channel_names.size()) - 1);

  if (!canfd_master->connect(channel_index)) {
    std::cout << "CANFD connect failed." << std::endl;
    wait_before_exit();
    return -1;
  }
  std::cout << "CANFD connect succeeded." << std::endl;

  // ── 2. Bind SDK callbacks ─────────────────────────────────────────────
  auto send_func = [canfd_master](unsigned int id, const unsigned char* data,
                                  unsigned int size, int is_extended) {
    return canfd_master->sendData(id, data, size, is_extended);
  };
  std::function<bool(unsigned int, const unsigned char*, unsigned int, int)>
      func = send_func;
  sdk_handle->set_send_canfd_callback_ex(&func);

  canfd_master->setReceiveCallback([sdk_handle](uint32_t id,
                                             const std::vector<uint8_t>& data,
                                             uint64_t timestamp) {
    (void)timestamp;
    sdk_handle->set_canfd_data_decode(id, data.data(), data.size());
  });

  // ── 3. Select node id ─────────────────────────────────────────────────
  const int node_id = select_number_in_range(
      "\nPlease enter CANFD node id (default 1)", 1, 255, 1);

  // ── 4. Initialize SDK ─────────────────────────────────────────────────
  if (sdk_handle->initial(SDK_NS::LCN_CANFD, node_id) != SDK_NS::LER_NONE) {
    std::cerr << "LHandProLib init failed." << std::endl;
    return_code = -1;
    goto Cleanup;
  }
  std::cout << "LHandProLib init succeeded." << std::endl;

  // ── 5. Get firmware path and run OTA ──────────────────────────────────
  {
    std::string firmwarePath;
    std::cout << std::endl << "Enter firmware file path: " << std::flush;
    std::getline(std::cin, firmwarePath);

    if (firmwarePath.empty()) {
      std::cerr << "No firmware path provided." << std::endl;
      return_code = -1;
      goto Cleanup;
    }

    return_code = run_ota_upgrade(*sdk_handle, firmwarePath);
  }

Cleanup:
  sdk_handle->close();
  canfd_master->disconnect();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  wait_before_exit();
  return return_code;
}
