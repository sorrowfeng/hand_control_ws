# Hand Control ROS2 Workspace

这是一个面向手部设备控制的 ROS2 工作空间，包含设备通讯服务、ROS2 接口定义、模型显示、动作序列示例和第三方 SDK 适配文件。

当前工程支持：

- CANFD、RS485、EtherCAT 三种通讯方式。
- 单手控制，以及两只手的多拓扑控制。
- 同一总线不同 ID 控制多只手。
- 不同总线相同或不同 ID 分别控制多只手。
- EtherCAT 同一网卡不同从站，或不同网卡相同/不同从站。
- 实时状态 topic 发布，用于位置、速度、电流、状态、报警等高频数据。
- RViz 模型显示，模型资源由 `hand_control_description/models` 管理。

## 目录结构

```text
hand_control_ws/
  src/
    hand_control_interfaces/   ROS2 msg/srv 接口定义
    hand_control_service/      设备控制服务节点和通讯后端
    hand_control_description/  URDF/MJCF/mesh 模型和 RViz 显示节点
    sequence_demo_cpp/         C++ 动作序列示例
    sequence_demo_py/          Python 动作序列示例
    ros2_hand_control.conf     可选的动态库路径配置示例
```

## 包说明

| 包 | 作用 |
| --- | --- |
| `hand_control_interfaces` | 定义服务接口和 `HandJointState` 实时状态消息。 |
| `hand_control_service` | 核心控制服务。负责加载 YAML 拓扑、初始化总线和手、注册服务、发布实时状态 topic。 |
| `hand_control_description` | 模型资源、RViz 配置和 `hand_state_publisher`。该节点订阅实时状态并发布 `/joint_states`。 |
| `sequence_demo_cpp` | C++ 版本动作序列调用示例。 |
| `sequence_demo_py` | Python 版本动作序列调用示例。 |

## 运行链路

```text
Hand device
   ^
   | CANFD / RS485 / EtherCAT
   v
hand_control_service
   | services: /hand_control/<service>
   | topics:   /hand_control/realtime_state
   v
hand_state_publisher
   | topic: /joint_states
   v
robot_state_publisher
   | topics: /tf, /tf_static, /robot_description
   v
RViz RobotModel
```

## 环境要求

推荐环境：

- Ubuntu 22.04
- ROS2 Humble
- `colcon`
- C++17 编译器
- CMake 3.8+
- Python 3

ROS2 依赖：

```bash
sudo apt update
sudo apt install -y \
  ros-humble-desktop \
  python3-colcon-common-extensions \
  python3-rosdep \
  ros-humble-xacro \
  ros-humble-robot-state-publisher \
  ros-humble-joint-state-publisher-gui \
  ros-humble-rviz2
```

如果需要关闭默认的 libcanbus 后端并改用 SocketCAN，可安装常用工具：

```bash
sudo apt install -y can-utils
```

## 构建

包名已经白标化。包名改动后建议清理旧构建产物，避免 ROS2 包索引里同时残留旧名和新名：

```bash
cd ~/Project/RosProject/hand_control_ws
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build --symlink-install
source install/setup.bash
```

只构建核心包：

```bash
colcon build --packages-select \
  hand_control_interfaces \
  hand_control_service \
  hand_control_description \
  --symlink-install
```

## 动态库配置

`hand_control_service` 会链接第三方 SDK 动态库，构建后相关库会安装到工作空间的 `install/lib`。

通常执行下面命令即可：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
```

如果运行时报找不到 SDK 动态库，可以把工作空间库路径加入系统动态库配置：

```bash
sudo cp src/ros2_hand_control.conf /etc/ld.so.conf.d/
sudo ldconfig
```

`src/ros2_hand_control.conf` 中的路径需要按实际工作空间位置调整，例如：

```text
/home/plf/Project/RosProject/hand_control_ws/install/hand_control_service/lib
/home/plf/Project/RosProject/hand_control_ws/install/hand_control_interfaces/lib
/home/plf/Project/RosProject/hand_control_ws/install/hand_control_description/lib
/home/plf/Project/RosProject/hand_control_ws/install/sequence_demo_cpp/lib
/home/plf/Project/RosProject/hand_control_ws/install/sequence_demo_py/lib
```

## 权限配置

默认 libcanbus 后端需要系统可加载 `libcanbus.so` 及其依赖。如果关闭 libcanbus 并改用 SocketCAN，服务需要操作 CAN 网络接口；不希望每次用 `sudo` 运行服务时，可给服务可执行文件添加能力：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

RS485 串口通常需要当前用户具备串口权限：

```bash
sudo usermod -aG dialout $USER
```

执行后重新登录终端。

## 启动服务

默认参数文件：

```bash
ros2 launch hand_control_service service.launch.py
```

等价于加载：

```text
src/hand_control_service/config/service.yaml
```

指定参数文件：

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=/absolute/path/to/service.yaml
```

服务节点名称为：

```text
/hand_control/hand_control_service
```

## 默认配置

默认配置位于 `src/hand_control_service/config/service.yaml`：

```yaml
/**:
  ros__parameters:
    bus_names: [bus0]
    hand_names: [hand]
    default_hand: hand
    compat_single_hand_services: true
    publish_realtime_state: true
    realtime_state_publish_rate_hz: 100.0

    buses:
      bus0:
        type: canfd
        channel: 0
        canfd_arb_baudrate: 1000000
        canfd_data_baudrate: 5000000
        rs485_baudrate: 500000

    hands:
      hand:
        bus: bus0
        address: 1
        hand_type: LAC_DOF_6_S
```

字段含义：

| 字段 | 说明 |
| --- | --- |
| `bus_names` | 总线名称列表。每个名称必须在 `buses` 下有对应配置。 |
| `hand_names` | 手名称列表。每个名称必须在 `hands` 下有对应配置。 |
| `default_hand` | 兼容单手服务名时默认绑定的手。 |
| `compat_single_hand_services` | 是否注册无手名前缀的兼容服务和 topic。 |
| `publish_realtime_state` | 是否发布实时状态 topic。 |
| `realtime_state_publish_rate_hz` | 实时状态发布频率，默认 100Hz。 |
| `buses.<name>.type` | `canfd`、`rs485`、`ethercat`。也支持 `ecat` 作为 EtherCAT 别名。 |
| `buses.<name>.channel` | 总线通道索引。CANFD 对应扫描到的 CAN 设备；RS485 对应扫描到的串口；EtherCAT 对应扫描到的网卡。 |
| `hands.<name>.bus` | 该手挂在哪条总线上。 |
| `hands.<name>.address` | CANFD/RS485 节点 ID，或 EtherCAT 从站号。 |
| `hands.<name>.hand_type` | SDK 手型枚举名，例如 `LAC_DOF_6`、`LAC_DOF_6_S`、`LAC_DOF_16`。 |

## 通讯方式

### CANFD

Linux 默认使用 libcanbus 后端，扫描 libcanbus 可见的 CANFD 设备，`channel` 对应扫描到的设备索引。

```yaml
buses:
  bus0:
    type: canfd
    channel: 0
    canfd_arb_baudrate: 1000000
    canfd_data_baudrate: 5000000
```

服务启动时会按配置设置 CANFD 波特率。这里的 `canfd_arb_baudrate` 和 `canfd_data_baudrate` 直接使用 SDK 元数据值，不再额外乘以 1000。

如需切回 SocketCAN 后端，构建时关闭 `HAND_CONTROL_USE_LIBCANBUS`。SocketCAN 会扫描 `/sys/class/net` 下以 `can` 开头的接口，例如 `can0`、`can1`：

```bash
colcon build --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=OFF
```

### RS485

Linux 下 RS485 会按优先级扫描串口设备。`/dev/ttyXR*` 会优先扫描，后续再扫描 USB 转串口、ACM、板载 UART 以及常见嵌入式平台串口命名：

```text
/dev/ttyXR*
/dev/ttyUSB*
/dev/ttyACM*
/dev/ttyAMA*
/dev/ttyTHS*
/dev/ttyO*
/dev/ttySAC*
/dev/ttySC*
/dev/ttymxc*
/dev/ttyUL*
/dev/ttyPS*
/dev/ttyMSM*
/dev/ttyHS*
/dev/ttyS*
```

在 `/dev/ttyS*` 之前，还会用 `/dev/tty*` 做一次通用兜底扫描，用来覆盖其他可能的串口命名。兜底扫描会跳过 `/dev/tty`、`/dev/tty0` 这类虚拟控制台和 `ttyprintk`，也会把标准 `ttyS数字` 留到最后再处理。

```yaml
buses:
  bus0:
    type: rs485
    channel: 0
    rs485_baudrate: 500000
```

### EtherCAT

EtherCAT 使用 SOEM。`channel` 对应扫描到的网卡索引，`address` 对应从站号。

```yaml
buses:
  bus0:
    type: ethercat
    channel: 0
```

## 多手配置

同一总线不同 ID：

```text
src/hand_control_service/config/two_hands_same_bus.yaml
```

不同总线相同或不同 ID：

```text
src/hand_control_service/config/two_hands_two_buses.yaml
```

启动双手示例：

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/two_hands_same_bus.yaml
```

## 服务命名规则

服务节点运行在 `/hand_control` 命名空间下。

单手默认配置中，`compat_single_hand_services: true`，因此会注册两套服务名：

```text
/hand_control/<service>
/hand_control/hand/<service>
```

多手配置中，每只手都会注册带手名前缀的服务：

```text
/hand_control/left/set_enable
/hand_control/right/set_enable
```

如果 `compat_single_hand_services: true`，还会给 `default_hand` 注册无手名前缀的兼容服务：

```text
/hand_control/set_enable
```

## 服务接口

基础控制：

| 服务 | 说明 |
| --- | --- |
| `set_enable` / `get_enable` | 设置或读取电机使能。 |
| `home_motors` | 电机回零。 |
| `move_motors` | 下发运动。 |
| `stop_motors` | 停止运动。 |
| `control_motors` | 一次性设置关节的位置、速度、电流并触发运动。 |
| `play_gesture` | 执行 SDK 内置手势。 |

目标参数：

| 服务 | 说明 |
| --- | --- |
| `set_angle` / `get_angle` | 设置或读取目标角度。 |
| `set_position` / `get_position` | 设置或读取目标位置。 |
| `set_angular_velocity` / `get_angular_velocity` | 设置或读取目标角速度。 |
| `set_position_velocity` / `get_position_velocity` | 设置或读取目标位置速度。 |
| `set_max_current` / `get_max_current` | 设置或读取最大电流。 |
| `set_control_mode` / `get_control_mode` | 设置或读取控制模式。 |
| `set_move_no_home` | 设置未回零是否允许运动。 |

实时状态读取：

| 服务 | 说明 |
| --- | --- |
| `get_now_angle` | 当前角度。 |
| `get_now_angular_velocity` | 当前角速度。 |
| `get_now_position` | 当前原始位置。 |
| `get_now_position_velocity` | 当前原始速度。 |
| `get_now_current` | 当前电流。 |
| `get_now_status` | 当前运行状态。 |
| `get_now_alarm` | 当前报警。 |
| `get_position_reached` | 位置到位状态。 |
| `get_torque_reached` | 力矩到位状态。 |

设备配置和信息：

| 服务 | 说明 |
| --- | --- |
| `set_clear_alarm` | 清除报警。 |
| `get_firmware_version` | 固件版本。 |
| `get_serial_number` | 序列号。 |
| `set_safe_current_enable` / `get_safe_current_enable` | 安全电流开关。 |
| `set_home_current` / `get_home_current` | 回零电流。 |
| `set_can_node_id` / `get_can_node_id` | CAN/CANFD 节点 ID。 |
| `set_canfd_arb_baudrate` / `get_canfd_arb_baudrate` | CANFD 仲裁段波特率。 |
| `set_canfd_data_baudrate` / `get_canfd_data_baudrate` | CANFD 数据段波特率。 |
| `set_rs485_node_id` / `get_rs485_node_id` | RS485 节点 ID。 |
| `set_rs485_baudrate` / `get_rs485_baudrate` | RS485 波特率。 |

查看当前所有服务：

```bash
ros2 service list | grep hand_control
```

查看某个服务类型：

```bash
ros2 service type /hand_control/get_now_angle
ros2 interface show hand_control_interfaces/srv/GetNowAngle
```

## 常用服务调用

使能所有关节：

```bash
ros2 service call /hand_control/set_enable \
  hand_control_interfaces/srv/SetEnable \
  "{joint_id: 0, enable: 1}"
```

设置 1 号关节目标位置：

```bash
ros2 service call /hand_control/set_position \
  hand_control_interfaces/srv/SetPosition \
  "{joint_id: 1, position: 5000}"
```

触发所有关节运动：

```bash
ros2 service call /hand_control/move_motors \
  hand_control_interfaces/srv/MoveMotors \
  "{joint_id: 0}"
```

多手时调用右手：

```bash
ros2 service call /hand_control/right/set_enable \
  hand_control_interfaces/srv/SetEnable \
  "{joint_id: 0, enable: 1}"
```

## 实时状态 Topic

服务端发布 `hand_control_interfaces/msg/HandJointState`。

单手默认配置中会发布：

```text
/hand_control/realtime_state
/hand_control/hand/realtime_state
```

多手配置中会发布：

```text
/hand_control/<hand_name>/realtime_state
```

查看实时状态：

```bash
ros2 topic echo /hand_control/realtime_state
ros2 topic hz /hand_control/realtime_state
```

## 模型显示

默认模型配置：

```text
src/hand_control_description/config/model.yaml
```

启动模型和 RViz：

```bash
ros2 launch hand_control_description display.launch.py
```

不启动 RViz，只启动模型状态链路：

```bash
ros2 launch hand_control_description display.launch.py use_rviz:=false
```

指定模型：

```bash
ros2 launch hand_control_description display.launch.py \
  model_id:=<model_directory_name>
```

指定完整 URDF 或 xacro 路径：

```bash
ros2 launch hand_control_description display.launch.py \
  model:=/absolute/path/to/robot.urdf
```

模型资源结构：

```text
src/hand_control_description/models/<model_id>/
  urdf/
  meshes/
  mjcf/
```

`hand_state_publisher` 默认订阅：

```text
/hand_control/realtime_state
```

并发布：

```text
/joint_states
```

`robot_state_publisher` 再根据 URDF 发布 `/tf`、`/tf_static` 和 `/robot_description`。

当前 `display.launch.py` 面向单只手模型显示。虽然 `hand_control_service` 支持多手控制，但模型显示链路默认只订阅 `/hand_control/realtime_state` 并发布全局 `/joint_states`。如果需要同时显示两只手，需要为两只手设计不同的模型 frame、joint 名称或 namespace 后再扩展显示链路。

## 动作序列示例

启动服务后，可以运行 Python 示例：

```bash
ros2 launch sequence_demo_py sequence.launch.py
```

或运行 C++ 示例：

```bash
ros2 launch sequence_demo_cpp sequence.launch.py
```

两个示例节点都运行在 `/hand_control` 命名空间下，默认调用无手名前缀的兼容服务。它们适合默认单手配置，或多手配置中的 `default_hand`。

## 推荐启动顺序

```bash
# 终端 1
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch hand_control_service service.launch.py
```

等待服务初始化完成后，再启动模型：

```bash
# 终端 2
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch hand_control_description display.launch.py
```

确认 topic 正常：

```bash
ros2 topic hz /hand_control/realtime_state
ros2 topic hz /joint_states
```

再运行动作序列：

```bash
# 终端 3
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch sequence_demo_py sequence.launch.py
```

## 停止和重复启动检查

如果使用 `Ctrl+C` 停止 launch，建议确认没有残留进程。重复启动服务或模型会导致同一个 topic 有多个发布者，RViz 会在不同发布者的状态之间跳动。

检查节点：

```bash
ros2 node list
```

正常单手单模型场景应只有一套：

```text
/hand_control/hand_control_service
/hand_state_publisher
/robot_state_publisher
/rviz
```

检查关键 topic 发布者数量：

```bash
ros2 topic info -v /hand_control/realtime_state
ros2 topic info -v /joint_states
ros2 topic info -v /tf
```

正常情况下这些 topic 的 `Publisher count` 都应为 `1`。

## 常见问题

### RViz 模型在两个位置之间反复跳

优先检查是否重复启动：

```bash
ros2 topic info -v /joint_states
ros2 topic info -v /tf
ros2 topic info -v /hand_control/realtime_state
```

如果 `Publisher count` 大于 1，就是多个节点同时发布同名 topic。清理重复进程后再启动一套即可。

### `/hand_control/realtime_state` 有数据，但 RViz 不动

检查 `/joint_states` 是否发布：

```bash
ros2 topic echo --once /joint_states
```

检查 `robot_state_publisher` 是否发布 `/tf`：

```bash
ros2 topic echo --once /tf
```

### CANFD 启动失败

检查 CAN 接口：

```bash
ip link show
```

确认存在 `can0` 或其他 `can*` 接口。服务默认扫描 `can*`，`channel: 0` 表示扫描结果的第一个接口。

检查权限：

```bash
getcap install/hand_control_service/lib/hand_control_service/hand_control_service
```

必要时重新设置：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

### RS485 找不到设备

检查串口：

```bash
ls /dev/ttyXR* /dev/ttyUSB* /dev/ttyACM* \
   /dev/ttyAMA* /dev/ttyTHS* /dev/ttyO* /dev/ttySAC* \
   /dev/ttySC* /dev/ttymxc* /dev/ttyUL* /dev/ttyPS* \
   /dev/ttyMSM* /dev/ttyHS* /dev/ttyS* /dev/tty* 2>/dev/null
```

检查用户权限：

```bash
groups
```

如果不在 `dialout` 组：

```bash
sudo usermod -aG dialout $USER
```

重新登录后再试。

### 服务调用找不到服务

确认命名空间。默认服务在 `/hand_control` 下，例如：

```bash
ros2 service list | grep hand_control
```

默认单手兼容服务：

```text
/hand_control/set_enable
```

带手名前缀服务：

```text
/hand_control/hand/set_enable
```

多手配置示例：

```text
/hand_control/left/set_enable
/hand_control/right/set_enable
```

### 构建找不到 SDK

确认第三方 SDK 文件存在：

```bash
ls src/hand_control_service/thirdparty/lib/
ls src/hand_control_service/thirdparty/include/
```

重新构建：

```bash
colcon build --packages-select hand_control_service --symlink-install
source install/setup.bash
```

## 第三方 SDK

SDK 相关文件在：

```text
src/hand_control_service/thirdparty/
  include/
  lib/
  bin/
  docs/
  share/
```

其中 `docs` 和 `share` 保留了 SDK 手册以及 CANFD、RS485、EtherCAT 的 C++/Python 参考示例。

## 开发提示

- 实时状态使用 topic，不建议模型显示端用服务轮询。
- 服务端允许 CANFD/RS485/EtherCAT 通过同一总线不同 ID 或不同总线相同 ID 控制多只手。
- 不要同时启动多套相同 namespace 的 service/display，否则 `/joint_states` 和 `/tf` 会出现多个发布者。
- 修改 `.srv` 或 `.msg` 后需要重新构建 `hand_control_interfaces` 以及依赖它的包。
- 修改模型后需要重新构建 `hand_control_description`，或使用 `--symlink-install` 方便开发调试。

## 许可证

本项目使用 Apache-2.0 许可证。详情见 [LICENSE](LICENSE)。
