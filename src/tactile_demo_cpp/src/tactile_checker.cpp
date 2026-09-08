#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "hand_control_interfaces/msg/tactile_state.hpp"
#include "hand_control_interfaces/srv/get_finger_sensor_pos.hpp"
#include "hand_control_interfaces/srv/set_finger_pressure_reset.hpp"
#include "hand_control_interfaces/srv/set_sensor_enable.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

namespace {

using TactileState = hand_control_interfaces::msg::TactileState;
using TactileSite = hand_control_interfaces::msg::TactileSite;
using SetSensorEnable = hand_control_interfaces::srv::SetSensorEnable;
using SetFingerPressureReset =
    hand_control_interfaces::srv::SetFingerPressureReset;
using GetFingerSensorPos = hand_control_interfaces::srv::GetFingerSensorPos;

const std::vector<std::string> kChannels = {
    "pressure", "normal_force", "tangential_force", "force_direction",
    "proximity"};

const std::vector<double>& ChannelValues(const TactileSite& site,
                                         const std::string& name) {
  static const std::vector<double> kEmpty;
  if (name == "pressure") return site.pressure;
  if (name == "normal_force") return site.normal_force;
  if (name == "tangential_force") return site.tangential_force;
  if (name == "force_direction") return site.force_direction;
  if (name == "proximity") return site.proximity;
  return kEmpty;
}

}  // namespace

class TactileChecker : public rclcpp::Node {
 public:
  TactileChecker() : Node("tactile_checker") {
    hand_name_ = this->declare_parameter<std::string>("hand_name", "hand");
    sample_count_ = this->declare_parameter<int>("sample_count", 20);
    timeout_sec_ = this->declare_parameter<double>("timeout_sec", 10.0);

    // 服务名带手名前缀，无论是否开启兼容模式都能找到
    enable_client_ = this->create_client<SetSensorEnable>(
        hand_name_ + "/set_sensor_enable");
    reset_client_ = this->create_client<SetFingerPressureReset>(
        hand_name_ + "/set_finger_pressure_reset");
    pos_client_ = this->create_client<GetFingerSensorPos>(
        hand_name_ + "/get_finger_sensor_pos");

    subscription_ = this->create_subscription<TactileState>(
        hand_name_ + "/tactile_state", 50,
        [this](const TactileState::SharedPtr msg) { this->OnState(msg); });
  }

  void WaitForServices() {
    WaitFor(enable_client_, "set_sensor_enable");
    WaitFor(reset_client_, "set_finger_pressure_reset");
    WaitFor(pos_client_, "get_finger_sensor_pos");
  }

  void Prepare(rclcpp::Executor& executor) {
    WaitForServices();

    auto req = std::make_shared<SetSensorEnable::Request>();
    req->enable = 1;
    auto future = enable_client_->async_send_request(req);
    if (executor.spin_until_future_complete(future, 5s) ==
        rclcpp::FutureReturnCode::SUCCESS) {
      // future.get() 只能调用一次，先取出响应再判断
      const auto response = future.get();
      if (response->result == 0) {
        RCLCPP_INFO(this->get_logger(), "传感器已使能");
      } else {
        RCLCPP_ERROR(this->get_logger(), "使能传感器失败 (result=%d)",
                     response->result);
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "使能传感器调用超时");
    }

    auto reset_req = std::make_shared<SetFingerPressureReset::Request>();
    auto reset_future = reset_client_->async_send_request(reset_req);
    if (executor.spin_until_future_complete(reset_future, 5s) ==
        rclcpp::FutureReturnCode::SUCCESS) {
      const auto response = reset_future.get();
      if (response->result == 0) {
        RCLCPP_INFO(this->get_logger(), "压力零点已设定");
      } else {
        RCLCPP_ERROR(this->get_logger(),
                     "设定压力零点失败 (result=%d)，pressure 可能不是绝对值",
                     response->result);
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "设定压力零点调用超时");
    }
  }

  void Collect(rclcpp::Executor& executor) {
    RCLCPP_INFO(this->get_logger(), "开始采集，目标 %d 帧，超时 %.1f 秒...",
                sample_count_, timeout_sec_);

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration<double>(timeout_sec_);

    while (rclcpp::ok() &&
           static_cast<int>(samples_.size()) < sample_count_ &&
           std::chrono::steady_clock::now() < deadline) {
      executor.spin_some(100ms);
    }
  }

  void Report(rclcpp::Executor& executor) {
    const auto count = samples_.size();
    RCLCPP_INFO(this->get_logger(), "======================================");
    RCLCPP_INFO(this->get_logger(), "手: %s   样本数: %zu", hand_name_.c_str(),
                count);

    if (samples_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "%.1f 秒内没有收到 %s/tactile_state",
                   timeout_sec_, hand_name_.c_str());
      RCLCPP_ERROR(this->get_logger(),
                   "检查服务端是否开启 tactile_enabled，以及该手型是否带触觉传感器");
      RCLCPP_INFO(this->get_logger(), "======================================");
      return;
    }

    const double duration = last_stamp_ - first_stamp_;
    const double freq =
        (count > 1 && duration > 0.0)
            ? static_cast<double>(count - 1) / duration
            : 0.0;
    RCLCPP_INFO(this->get_logger(), "实测频率: %.1f Hz", freq);

    const auto& latest = samples_.back();
    int active_count = 0;
    for (const auto& site : latest.sites) {
      if (site.cell_count > 0) ++active_count;
    }
    RCLCPP_INFO(this->get_logger(), "位点: 共 %zu 个，其中 %d 个有传感单元",
                latest.sites.size(), active_count);

    for (const auto& site : latest.sites) {
      if (site.cell_count <= 0) continue;
      std::string line = "  sensor_id=" + std::to_string(site.sensor_id) +
                         "  cell_count=" + std::to_string(site.cell_count);
      for (const auto& channel : kChannels) {
        const auto& values = ChannelValues(site, channel);
        line += "  " + channel + "=";
        if (values.empty()) {
          line += "空";
        } else {
          const double max_value =
              *std::max_element(values.begin(), values.end());
          char buffer[64];
          std::snprintf(buffer, sizeof(buffer), "%zu[max %.3f]", values.size(),
                        max_value);
          line += buffer;
        }
      }
      RCLCPP_INFO(this->get_logger(), "%s", line.c_str());
    }

    // 顺带验证静态坐标服务
    for (const auto& site : latest.sites) {
      if (site.cell_count <= 0) continue;
      auto req = std::make_shared<GetFingerSensorPos::Request>();
      req->sensor_id = site.sensor_id;
      auto future = pos_client_->async_send_request(req);
      if (executor.spin_until_future_complete(future, 2s) ==
          rclcpp::FutureReturnCode::SUCCESS) {
        RCLCPP_INFO(this->get_logger(), "get_finger_sensor_pos(%d) -> %zu 个坐标点",
                    site.sensor_id, future.get()->x.size());
      } else {
        RCLCPP_WARN(this->get_logger(), "get_finger_sensor_pos(%d) 调用失败",
                    site.sensor_id);
      }
      break;
    }

    for (const auto& channel : kChannels) {
      bool any_data = false;
      for (const auto& site : latest.sites) {
        if (site.cell_count > 0 && !ChannelValues(site, channel).empty()) {
          any_data = true;
          break;
        }
      }
      if (!any_data) {
        RCLCPP_WARN(this->get_logger(), "通道全程无数据: %s", channel.c_str());
      }
    }

    RCLCPP_INFO(this->get_logger(), "======================================");
  }

 private:
  void OnState(const TactileState::SharedPtr msg) {
    samples_.push_back(*msg);
    const double stamp = static_cast<double>(msg->header.stamp.sec) +
                         static_cast<double>(msg->header.stamp.nanosec) * 1e-9;
    if (samples_.size() == 1) first_stamp_ = stamp;
    last_stamp_ = stamp;
  }

  template <typename ClientT>
  void WaitFor(const ClientT& client, const std::string& name) {
    while (rclcpp::ok() && !client->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 %s 服务上线...", name.c_str());
    }
  }

  std::string hand_name_;
  int sample_count_{20};
  double timeout_sec_{10.0};

  typename rclcpp::Client<SetSensorEnable>::SharedPtr enable_client_;
  typename rclcpp::Client<SetFingerPressureReset>::SharedPtr reset_client_;
  typename rclcpp::Client<GetFingerSensorPos>::SharedPtr pos_client_;
  rclcpp::Subscription<TactileState>::SharedPtr subscription_;

  std::vector<TactileState> samples_;
  double first_stamp_{0.0};
  double last_stamp_{0.0};
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<TactileChecker>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);

  node->Prepare(executor);
  node->Collect(executor);
  node->Report(executor);

  executor.remove_node(node);
  rclcpp::shutdown();
  return 0;
}
