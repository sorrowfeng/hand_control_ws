#include <chrono>
#include <thread>
#include <vector>

#include "hand_control_interfaces/srv/home_motors.hpp"
#include "hand_control_interfaces/srv/move_motors.hpp"
#include "hand_control_interfaces/srv/get_now_status.hpp"
#include "hand_control_interfaces/srv/set_enable.hpp"
#include "hand_control_interfaces/srv/set_position.hpp"
#include "hand_control_interfaces/srv/set_position_velocity.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

class SequenceCaller : public rclcpp::Node {
 public:
  using SetEnable = hand_control_interfaces::srv::SetEnable;
  using SetPosition = hand_control_interfaces::srv::SetPosition;
  using SetPositionVelocity = hand_control_interfaces::srv::SetPositionVelocity;
  using MoveMotors = hand_control_interfaces::srv::MoveMotors;
  using HomeMotors = hand_control_interfaces::srv::HomeMotors;
  using GetNowStatus = hand_control_interfaces::srv::GetNowStatus;

  SequenceCaller() : Node("sequence_caller") {
    RCLCPP_INFO(this->get_logger(), "Sequence Caller 启动...");

    // 创建客户端
    set_enable_client_ = this->create_client<SetEnable>("set_enable");
    set_position_client_ = this->create_client<SetPosition>("set_position");
    set_position_velocity_client_ =
        this->create_client<SetPositionVelocity>("set_position_velocity");
    move_motors_client_ = this->create_client<MoveMotors>("move_motors");
    home_motors_client_ = this->create_client<HomeMotors>("home_motors");
    get_now_status_client_ = this->create_client<GetNowStatus>("get_now_status");

    // 等待服务上线
    while (!set_enable_client_->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 set_enable 服务...");
    }

    while (!set_position_client_->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 set_position 服务...");
    }

    while (!set_position_velocity_client_->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 set_position_velocity 服务...");
    }

    while (!move_motors_client_->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 move_motors 服务...");
    }

    while (!home_motors_client_->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 home_motors 服务...");
    }

    while (!get_now_status_client_->wait_for_service(1s)) {
      RCLCPP_INFO(this->get_logger(), "等待 get_now_status 服务...");
    }

    // 使用线程启动轨迹执行
    std::thread([this]() {
      this->execute_trajectory();
      rclcpp::shutdown();
    }).detach();  // 自动释放线程资源（注意内存安全）
  }

 public:
  void execute_trajectory() {
    // 设置默认参数
    std::vector<int> joint_ids = {1, 2, 3, 4, 5, 6};
    int default_velocity_ = 20000;

    // 内嵌动作数组
    const std::vector<std::vector<int>> positions = {
        {0, 0, 0, 0, 0, 0},     {10000, 0, 0, 0, 0, 0}, {0, 10000, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0},     {0, 0, 10000, 0, 0, 0}, {0, 0, 0, 10000, 0, 0},
        {0, 0, 0, 0, 10000, 0}, {0, 0, 0, 0, 0, 10000}, {0, 0, 0, 0, 0, 0},
    };

    send_set_enable(0, 1);
    RCLCPP_INFO(this->get_logger(), "正在使能");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    send_home_motors(0);
    RCLCPP_INFO(this->get_logger(), "正在回零");
    if (!wait_for_home_complete(joint_ids, 30s)) {
      RCLCPP_ERROR(this->get_logger(), "回零未完成，取消轨迹执行");
      send_set_enable(0, 0);
      return;
    }
    RCLCPP_INFO(this->get_logger(), "回零完成");

    for (size_t i = 0; i < positions.size(); ++i) {
      RCLCPP_INFO(this->get_logger(), "正在执行第 %lu 步动作...", i + 1);

      for (size_t j = 0; j < joint_ids.size(); ++j) {
        int joint_id = joint_ids[j];
        int position = positions[i][j];

        send_set_position(joint_id, position);
        send_set_position_velocity(joint_id, default_velocity_);
      }

      // 触发所有关节运动
      send_move_motors(0);
      RCLCPP_INFO(this->get_logger(), "第 %lu 步动作", i + 1);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    RCLCPP_INFO(this->get_logger(), "轨迹执行完成");
    send_set_enable(0, 0);
  }

  void send_set_position(int joint_id, int position) {
    auto req = std::make_shared<SetPosition::Request>();
    req->joint_id = joint_id;
    req->position = position;

    auto future = set_position_client_->async_send_request(req);
    future.wait();
    if (future.get()->result == 0) {
      RCLCPP_DEBUG(this->get_logger(), "设置位置成功: %d %d", joint_id,
                   position);
    } else {
      RCLCPP_ERROR(this->get_logger(), "设置位置失败: %d", joint_id);
    }
  }

  void send_set_position_velocity(int joint_id, int velocity) {
    auto req = std::make_shared<SetPositionVelocity::Request>();
    req->joint_id = joint_id;
    req->velocity = velocity;

    auto future = set_position_velocity_client_->async_send_request(req);
    future.wait();
    if (future.get()->result == 0) {
      RCLCPP_DEBUG(this->get_logger(), "设置速度成功: %d %d", joint_id,
                   velocity);
    } else {
      RCLCPP_ERROR(this->get_logger(), "设置速度失败: %d", joint_id);
    }
  }

  void send_set_enable(int joint_id, int enable) {
    auto req = std::make_shared<SetEnable::Request>();
    req->joint_id = joint_id;
    req->enable = enable;

    auto future = set_enable_client_->async_send_request(req);
    future.wait();
    if (future.get()->result == 0) {
      RCLCPP_DEBUG(this->get_logger(), "设置使能成功: %d %d", joint_id, enable);
    } else {
      RCLCPP_ERROR(this->get_logger(), "设置使能失败: %d", joint_id);
    }
  }

  void send_move_motors(int joint_id) {
    auto req = std::make_shared<MoveMotors::Request>();
    req->joint_id = joint_id;

    auto future = move_motors_client_->async_send_request(req);
    future.wait();
    const int result = future.get()->result;
    if (result != 0) {
      RCLCPP_ERROR(this->get_logger(), "驱动关节失败: %d result=%d", joint_id,
                   result);
    }
  }

  void send_home_motors(int joint_id) {
    auto req = std::make_shared<HomeMotors::Request>();
    req->joint_id = joint_id;

    auto future = home_motors_client_->async_send_request(req);
    future.wait();
    if (future.get()->result != 0) {
      RCLCPP_ERROR(this->get_logger(), "回零关节失败: %d", joint_id);
    }
  }

  int get_now_status(int joint_id) {
    auto req = std::make_shared<GetNowStatus::Request>();
    req->joint_id = joint_id;

    auto future = get_now_status_client_->async_send_request(req);
    future.wait();
    return future.get()->status;
  }

  bool wait_for_home_complete(const std::vector<int>& joint_ids,
                              std::chrono::seconds timeout) {
    constexpr int kStopped = 0;
    constexpr int kRunning = 1;
    constexpr int kHoming = 7;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      bool all_stopped = true;
      for (const int joint_id : joint_ids) {
        const int status = get_now_status(joint_id);
        if (status == kStopped) {
          continue;
        }
        if (status == kRunning || status == kHoming) {
          all_stopped = false;
          continue;
        }

        RCLCPP_ERROR(this->get_logger(), "关节 %d 回零状态异常: %d", joint_id,
                     status);
        return false;
      }

      if (all_stopped) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return false;
  }

  rclcpp::Client<SetEnable>::SharedPtr set_enable_client_;
  rclcpp::Client<SetPosition>::SharedPtr set_position_client_;
  rclcpp::Client<SetPositionVelocity>::SharedPtr set_position_velocity_client_;
  rclcpp::Client<MoveMotors>::SharedPtr move_motors_client_;
  rclcpp::Client<HomeMotors>::SharedPtr home_motors_client_;
  rclcpp::Client<GetNowStatus>::SharedPtr get_now_status_client_;

  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SequenceCaller>());
  rclcpp::shutdown();
  return 0;
}
