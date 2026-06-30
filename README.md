# LHandPro ROS2 示例工程

这是一个基于 ROS2 的 LHandPro 示例工程，支持 EtherCAT、CANFD 和 RS485 三种通讯方式。

## 系统要求

- Ubuntu 22.04 或更高版本
- ROS2 Humble 或更高版本
- C++17 兼容编译器
- CMake 3.16 或更高版本

## 安装

1. 克隆仓库：

   ```bash
   git clone <repository_url>
   ```

2. 构建项目：

   ```bash
   cd lhandpro_ws
   colcon build
   ```

3. 激活工作空间：

   ```bash
   source install/setup.bash
   ```

4. 配置环境

   ```bash
   # 复制so到系统库目录,方便后续使用
   sudo cp src/lhandpro_service/thirdparty/lib/libLHandProLib.so /usr/local/lib/ 
   # 刷新动态链接器缓存
   sudo ldconfig       
   # 赋予原始套接字 + 网络管理权限
   sudo setcap cap_net_raw,cap_net_admin+ep ./install/lhandpro_service/lib/lhandpro_service/lhandpro_service
   ```

   打开lhandpro_ws/src/ros2_lhandpro.conf, 编辑conf文件
   ```bash
   # ros2_lhandpro.conf 文件内容, 修改为本机的实际路径
   /home/plf/Project/RosProject/lhandpro_ws/install/lhandpro_service/lib
   /home/plf/Project/RosProject/lhandpro_ws/install/lhandpro_interfaces/lib
   /home/plf/Project/RosProject/lhandpro_ws/install/lhandpro_description/lib
   /home/plf/Project/RosProject/lhandpro_ws/install/sequence_demo_cpp/lib
   /home/plf/Project/RosProject/lhandpro_ws/install/sequence_demo_py/lib
   ```
   
   配置执行环境
   ```bash
   # 复制conf文件到系统目录
   sudo cp src/ros2_lhandpro.conf /etc/ld.so.conf.d/
   # 刷新动态链接器缓存
   sudo ldconfig       
   ```

## 使用方法

### 启动服务

   ```bash
   ros2 launch lhandpro_service lhandpro.launch.py
   ```

### 启动示例控制

   ```bash
   # 启动后则会自动执行准备好的动作序列
   ros2 launch sequence_demo_py sequence.launch.py
   # 或者执行C++版本的示例程序
   ros2 launch sequence_demo_cpp sequence.launch.py
   ```

### 启动rviz2的仿真显示

   ```bash
   ros2 launch lhandpro_description display_lhandpro.launch.py
   # 执行该脚本, 会启动robot_state_publisher节点
   # 启动lhandpro_state_publisher节点, 用来通过服务监控角度变化, 并通过/joint_states发布给rviz2更新显示
   # 启动rviz2节点, 并加载准备好的rviz配置config
   # 加载完成顺利的话则能在rviz2中看到灵巧手的模型
   # 此时执行4步骤中的控制示例, 则能看到实际灵巧手在运动, 并且rviz2中的模型也在同时运动
   ```


### 通讯方式切换

在 `src/lhandpro_service/src/hand_control_service.hpp` 文件中修改 `COMMUNICATION_MODE` 宏：

- `#define COMMUNICATION_MODE 0` - 使用 EtherCAT 通讯
- `#define COMMUNICATION_MODE 1` - 使用 CANFD 通讯
- `#define COMMUNICATION_MODE 2` - 使用 RS485 通讯

CANFD 默认节点和波特率也在同一文件中配置：

- `#define CANFD_NODE_ID 1` - 默认 CANFD 节点 ID
- `#define CANFD_ARB_BAUDRATE 1000000` - 默认仲裁段波特率，直接使用 SDK 元数据值
- `#define CANFD_DATA_BAUDRATE 5000000` - 默认数据段波特率，直接使用 SDK 元数据值

RS485 默认节点和波特率也在同一文件中配置：

- `#define RS485_NODE_ID 1` - 默认 RS485 节点 ID
- `#define RS485_BAUDRATE 500000` - 默认 RS485 串口波特率

Linux 下 CANFD 默认使用 SocketCAN；如需使用 `libcanbus` 后端，构建时添加：

```bash
colcon build --cmake-args -DLHANDPRO_USE_LIBCANBUS=ON
```

### 灵巧手型号切换

在 `src/lhandpro_service/src/hand_control_service.hpp` 文件中修改 `HAND_TYPE` 宏：

- `#define HAND_TYPE lhplib::LAC_DOF_6` - DH116灵巧手
- `#define HAND_TYPE lhplib::LAC_DOF_6_S` - DH116S灵巧手
- `#define HAND_TYPE lhplib::LAC_DOF_16` - 16自由度灵巧手



### 服务列表

| 服务名称 | 功能描述 |
|---------|----------|
| `set_enable` | 设置电机使能状态 |
| `get_enable` | 获取电机使能状态 |
| `get_now_alarm` | 获取当前报警状态 |
| `set_clear_alarm` | 清除电机报警 |
| `set_angle` | 设置目标角度 |
| `get_angle` | 获取目标角度 |
| `set_position` | 设置目标位置 |
| `get_position` | 获取目标位置 |
| `set_angular_velocity` | 设置目标角速度 |
| `get_angular_velocity` | 获取目标角速度 |
| `get_now_angle` | 获取当前角度 |
| `get_now_angular_velocity` | 获取当前角速度 |
| `get_now_position` | 获取当前位置 |
| `get_now_position_velocity` | 获取当前速度 |
| `get_now_current` | 获取当前电流 |
| `get_now_status` | 获取当前运行状态 |
| `get_position_velocity` | 获取目标速度 |
| `set_position_velocity` | 设置目标速度 |
| `get_max_current` | 获取最大电流 |
| `set_max_current` | 设置最大电流 |
| `get_control_mode` | 获取控制模式 |
| `set_control_mode` | 设置控制模式 |
| `get_position_reached` | 获取位置到位状态 |
| `get_torque_reached` | 获取力矩到位状态 |
| `set_move_no_home` | 设置未回零是否允许运动 |
| `get_firmware_version` | 获取固件版本 |
| `get_serial_number` | 获取序列号 |
| `set_safe_current_enable` | 设置安全电流开关 |
| `get_safe_current_enable` | 获取安全电流开关 |
| `set_home_current` | 设置回零电流 |
| `get_home_current` | 获取回零电流 |
| `set_can_node_id` | 设置 CAN/CANFD 节点 ID |
| `get_can_node_id` | 获取 CAN/CANFD 节点 ID |
| `set_canfd_arb_baudrate` | 设置 CANFD 仲裁段波特率 |
| `get_canfd_arb_baudrate` | 获取 CANFD 仲裁段波特率 |
| `set_canfd_data_baudrate` | 设置 CANFD 数据段波特率 |
| `get_canfd_data_baudrate` | 获取 CANFD 数据段波特率 |
| `set_rs485_node_id` | 设置 RS485 节点 ID |
| `get_rs485_node_id` | 获取 RS485 节点 ID |
| `set_rs485_baudrate` | 设置 RS485 波特率 |
| `get_rs485_baudrate` | 获取 RS485 波特率 |
| `home_motors` | 电机回零 |
| `move_motors` | 驱动电机运动 |
| `stop_motors` | 停止电机运动 |
| `play_gesture` | 执行内置手势 |

## 示例调用

### 设置电机使能

```bash
ros2 service call /set_enable lhandpro_interfaces/srv/SetEnable "{joint_id: 0, enable: 1}"
```

### 设置目标位置

```bash
ros2 service call /set_position lhandpro_interfaces/srv/SetPosition "{joint_id: 1, position: 5000}"
```

### 驱动电机运动

```bash
ros2 service call /move_motors lhandpro_interfaces/srv/MoveMotors "{joint_id: 0}"
```

## 维护者

- Sorrowfeng

## 许可证

本项目使用 Apache-2.0 许可证，详情见 LICENSE 文件。
