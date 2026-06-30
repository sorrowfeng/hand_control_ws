# LHandPro ROS2 Workspace

这是一个面向 LHandPro 灵巧手的 ROS2 工作空间，包含设备通讯服务、ROS2 接口定义、模型显示、动作序列示例以及 LHandProLib 第三方 SDK 文件。

当前工程支持：

- CANFD、RS485、EtherCAT 三种通讯方式。
- 单手控制，以及两只手的多拓扑控制。
- 同一总线不同 ID 控制多只手。
- 不同总线相同或不同 ID 分别控制多只手。
- EtherCAT 同一网卡不同从站，或不同网卡相同/不同从站。
- 实时状态 topic 发布，用于位置、速度、电流、状态、报警等高频数据。
- RViz 模型显示，默认模型为 `DH116S-L000-A1`。

## 目录结构

```text
lhandpro_ws/
  src/
    lhandpro_interfaces/   ROS2 msg/srv 接口定义
    lhandpro_service/      LHandPro 设备服务节点和通讯后端
    lhandpro_description/  URDF/MJCF/mesh 模型和 RViz 显示节点
    sequence_demo_cpp/     C++ 动作序列示例
    sequence_demo_py/      Python 动作序列示例
    ros2_lhandpro.conf     可选的动态库路径配置示例
```

## 包说明

| 包 | 作用 |
| --- | --- |
| `lhandpro_interfaces` | 定义服务接口和 `HandJointState` 实时状态消息。 |
| `lhandpro_service` | 核心控制服务。负责加载 YAML 拓扑、初始化总线和手、注册服务、发布实时状态 topic。 |
| `lhandpro_description` | 模型资源、RViz 配置和 `lhandpro_state_publisher`。该节点订阅实时状态并发布 `/joint_states`。 |
| `sequence_demo_cpp` | C++ 版本动作序列调用示例。 |
| `sequence_demo_py` | Python 版本动作序列调用示例。 |

## 运行链路

```text
LHandPro device
   ^
   | CANFD / RS485 / EtherCAT
   v
lhandpro_service
   | services: /lhandpro_service/<service>
   | topics:   /lhandpro_service/realtime_state
   v
lhandpro_state_publisher
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

CANFD SocketCAN 常用工具：

```bash
sudo apt install -y can-utils
```

## 构建

```bash
cd ~/Project/RosProject/lhandpro_ws
source /opt/ros/humble/setup.bash
colcon build --symlink-install
source install/setup.bash
```

只构建核心包：

```bash
colcon build --packages-select \
  lhandpro_interfaces \
  lhandpro_service \
  lhandpro_description \
  --symlink-install
```

## 动态库配置

`lhandpro_service` 会链接 `src/lhandpro_service/thirdparty/lib/libLHandProLib.so`，构建后该库也会安装到工作空间的 `install/lib`。

通常执行下面命令即可：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
```

如果运行时报找不到 `libLHandProLib.so`，可以把工作空间库路径加入系统动态库配置：

```bash
sudo cp src/ros2_lhandpro.conf /etc/ld.so.conf.d/
sudo ldconfig
```

`src/ros2_lhandpro.conf` 中的路径需要按实际工作空间位置调整，例如：

```text
/home/plf/Project/RosProject/lhandpro_ws/install/lhandpro_service/lib
/home/plf/Project/RosProject/lhandpro_ws/install/lhandpro_interfaces/lib
/home/plf/Project/RosProject/lhandpro_ws/install/lhandpro_description/lib
/home/plf/Project/RosProject/lhandpro_ws/install/sequence_demo_cpp/lib
/home/plf/Project/RosProject/lhandpro_ws/install/sequence_demo_py/lib
```

## 权限配置

SocketCAN 方式需要操作 CAN 网络接口。如果不希望每次用 `sudo` 运行服务，可给服务可执行文件添加能力：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/lhandpro_service/lib/lhandpro_service/lhandpro_service
```

RS485 串口通常需要当前用户具备串口权限：

```bash
sudo usermod -aG dialout $USER
```

执行后重新登录终端。

EtherCAT 通常需要原始网卡访问权限。调试阶段可先使用 `sudo` 验证网卡和从站，之后再按现场部署方式配置权限。

## 启动服务

默认参数文件：

```bash
ros2 launch lhandpro_service lhandpro.launch.py
```

等价于加载：

```text
src/lhandpro_service/config/lhandpro_service.yaml
```

指定参数文件：

```bash
ros2 launch lhandpro_service lhandpro.launch.py \
  params_file:=/absolute/path/to/lhandpro_service.yaml
```

服务节点名称为：

```text
/lhandpro_service/lhandpro_service
```

## 默认配置

默认配置位于 `src/lhandpro_service/config/lhandpro_service.yaml`：

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
| `default_hand` | 兼容旧服务名时默认绑定的手。 |
| `compat_single_hand_services` | 是否注册无手名前缀的兼容服务和 topic。 |
| `publish_realtime_state` | 是否发布实时状态 topic。 |
| `realtime_state_publish_rate_hz` | 实时状态发布频率，默认 100Hz。 |
| `buses.<name>.type` | `canfd`、`rs485`、`ethercat`。也支持 `ecat` 作为 EtherCAT 别名。 |
| `buses.<name>.channel` | 总线通道索引。CANFD 对应扫描到的 CAN 设备；RS485 对应扫描到的串口；EtherCAT 对应扫描到的网卡。 |
| `hands.<name>.bus` | 该手挂在哪条总线上。 |
| `hands.<name>.address` | CANFD/RS485 节点 ID，或 EtherCAT 从站号。 |
| `hands.<name>.hand_type` | `LAC_DOF_6`、`LAC_DOF_6_S`、`LAC_DOF_16`，也支持 `DH116`、`DH116S` 等别名。 |

## 通讯方式

### CANFD

Linux 默认使用 SocketCAN 后端，扫描 `/sys/class/net` 下以 `can` 开头的接口，例如 `can0`、`can1`。

`channel` 是扫描结果的索引。通常单 CAN 设备时：

```yaml
buses:
  bus0:
    type: canfd
    channel: 0
    canfd_arb_baudrate: 1000000
    canfd_data_baudrate: 5000000
```

服务启动时会按配置设置 CANFD 波特率。这里的 `canfd_arb_baudrate` 和 `canfd_data_baudrate` 直接使用 SDK 元数据值，不再额外乘以 1000。

如需使用 `libcanbus` 后端，构建时打开：

```bash
colcon build --packages-select lhandpro_service \
  --cmake-args -DLHANDPRO_USE_LIBCANBUS=ON
```

`libcanbus.so` 和 `libusb-1.0.so` 需要能被动态链接器找到，例如放在 `/usr/local/lib` 后执行：

```bash
sudo ldconfig
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

在 `/dev/ttyS*` 之前，还会用 `/dev/tty*` 做一次通用兜底扫描，用来覆盖其他可能的串口命名。兜底扫描会跳过 `/dev/tty`、`/dev/tty0` 这类虚拟控制台和 `ttyprintk`，也会把标准 `ttyS数字` 留到最后再处理。这样可以避免系统默认存在的一批 `ttyS*` 把其他真实串口挤到扫描列表后面。

`channel` 是最终扫描结果的索引。例如如果系统同时存在 `/dev/ttyXR0` 和 `/dev/ttyUSB0`，默认 `channel: 0` 会优先选择 `/dev/ttyXR0`。

配置示例：

```yaml
buses:
  bus0:
    type: rs485
    channel: 0
    rs485_baudrate: 500000

hands:
  hand:
    bus: bus0
    address: 1
    hand_type: LAC_DOF_6_S
```

### EtherCAT

EtherCAT 使用 SOEM。`channel` 对应扫描到的网卡索引，`address` 对应从站号。

配置示例：

```yaml
buses:
  bus0:
    type: ethercat
    channel: 0

hands:
  hand:
    bus: bus0
    address: 1
    hand_type: LAC_DOF_6_S
```

## 多手配置

当前服务支持两种多手拓扑。

### 同一总线，不同 ID

参考：

```text
src/lhandpro_service/config/lhandpro_two_hands_same_bus.yaml
```

示例：

```yaml
bus_names: [bus0]
hand_names: [left, right]
default_hand: left
compat_single_hand_services: true

buses:
  bus0:
    type: canfd
    channel: 0

hands:
  left:
    bus: bus0
    address: 1
    hand_type: LAC_DOF_6
  right:
    bus: bus0
    address: 2
    hand_type: LAC_DOF_6
```

适用于 CANFD/RS485 同一总线不同节点 ID，或 EtherCAT 同一网卡不同从站。

### 不同总线，相同或不同 ID

参考：

```text
src/lhandpro_service/config/lhandpro_two_hands_two_buses.yaml
```

示例：

```yaml
bus_names: [left_bus, right_bus]
hand_names: [left, right]
default_hand: left
compat_single_hand_services: true

buses:
  left_bus:
    type: canfd
    channel: 0
  right_bus:
    type: canfd
    channel: 1

hands:
  left:
    bus: left_bus
    address: 1
    hand_type: LAC_DOF_6
  right:
    bus: right_bus
    address: 1
    hand_type: LAC_DOF_6
```

适用于两条 CANFD/RS485 总线分别接一只手，或者 EtherCAT 两张网卡分别接一只手。由于总线隔离，两个手可以使用相同 ID 或相同从站号。

启动双手示例：

```bash
ros2 launch lhandpro_service lhandpro.launch.py \
  params_file:=$(pwd)/src/lhandpro_service/config/lhandpro_two_hands_same_bus.yaml
```

## 服务命名规则

服务节点运行在 `/lhandpro_service` 命名空间下。

单手默认配置中，`compat_single_hand_services: true`，因此会注册两套服务名：

```text
/lhandpro_service/<service>
/lhandpro_service/hand/<service>
```

例如：

```text
/lhandpro_service/set_enable
/lhandpro_service/hand/set_enable
```

多手配置中，每只手都会注册带手名前缀的服务：

```text
/lhandpro_service/left/set_enable
/lhandpro_service/right/set_enable
```

如果 `compat_single_hand_services: true`，还会给 `default_hand` 注册无手名前缀的兼容服务：

```text
/lhandpro_service/set_enable
```

## 服务接口

可用服务按功能分组如下。

### 基础控制

| 服务 | 说明 |
| --- | --- |
| `set_enable` / `get_enable` | 设置或读取电机使能。 |
| `home_motors` | 电机回零。 |
| `move_motors` | 下发运动。 |
| `stop_motors` | 停止运动。 |
| `control_motors` | 一次性设置 6 个关节的位置、速度、电流并触发运动。 |
| `play_gesture` | 执行 SDK 内置手势。 |

### 目标参数

| 服务 | 说明 |
| --- | --- |
| `set_angle` / `get_angle` | 设置或读取目标角度。 |
| `set_position` / `get_position` | 设置或读取目标位置。 |
| `set_angular_velocity` / `get_angular_velocity` | 设置或读取目标角速度。 |
| `set_position_velocity` / `get_position_velocity` | 设置或读取目标位置速度。 |
| `set_max_current` / `get_max_current` | 设置或读取最大电流。 |
| `set_control_mode` / `get_control_mode` | 设置或读取控制模式。 |
| `set_move_no_home` | 设置未回零是否允许运动。 |

### 实时状态读取

这些服务保留用于点查状态；高频显示和监控推荐使用 realtime topic。

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

### 设备配置和信息

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
ros2 service list | grep lhandpro_service
```

查看某个服务类型：

```bash
ros2 service type /lhandpro_service/get_now_angle
ros2 interface show lhandpro_interfaces/srv/GetNowAngle
```

## 常用服务调用

使能所有关节：

```bash
ros2 service call /lhandpro_service/set_enable \
  lhandpro_interfaces/srv/SetEnable \
  "{joint_id: 0, enable: 1}"
```

设置 1 号关节目标位置：

```bash
ros2 service call /lhandpro_service/set_position \
  lhandpro_interfaces/srv/SetPosition \
  "{joint_id: 1, position: 5000}"
```

设置 1 号关节速度：

```bash
ros2 service call /lhandpro_service/set_position_velocity \
  lhandpro_interfaces/srv/SetPositionVelocity \
  "{joint_id: 1, velocity: 20000}"
```

触发所有关节运动：

```bash
ros2 service call /lhandpro_service/move_motors \
  lhandpro_interfaces/srv/MoveMotors \
  "{joint_id: 0}"
```

读取当前角度：

```bash
ros2 service call /lhandpro_service/get_now_angle \
  lhandpro_interfaces/srv/GetNowAngle \
  "{joint_id: 1}"
```

多手时调用右手：

```bash
ros2 service call /lhandpro_service/right/set_enable \
  lhandpro_interfaces/srv/SetEnable \
  "{joint_id: 0, enable: 1}"
```

## 实时状态 Topic

服务端发布 `lhandpro_interfaces/msg/HandJointState`。

单手默认配置中会发布：

```text
/lhandpro_service/realtime_state
/lhandpro_service/hand/realtime_state
```

多手配置中会发布：

```text
/lhandpro_service/<hand_name>/realtime_state
```

如果开启兼容单手模式，还会为 `default_hand` 发布：

```text
/lhandpro_service/realtime_state
```

消息字段：

```text
std_msgs/Header header
string hand_name
string bus_name
int32 address
int32 active_dof
int32[] joint_ids
float64[] angles
float64[] angular_velocities
int32[] positions
int32[] position_velocities
float64[] currents
int32[] statuses
int32[] alarms
```

查看实时状态：

```bash
ros2 topic echo /lhandpro_service/realtime_state
```

查看频率：

```bash
ros2 topic hz /lhandpro_service/realtime_state
```

## 模型显示

默认模型配置：

```text
src/lhandpro_description/config/model.yaml
```

当前默认：

```yaml
active_model: DH116S-L000-A1
urdf_file:
```

启动模型和 RViz：

```bash
ros2 launch lhandpro_description display_lhandpro.launch.py
```

不启动 RViz，只启动模型状态链路：

```bash
ros2 launch lhandpro_description display_lhandpro.launch.py use_rviz:=false
```

指定模型：

```bash
ros2 launch lhandpro_description display_lhandpro.launch.py \
  model_id:=DH116S-R000-A1
```

指定完整 URDF 或 xacro 路径：

```bash
ros2 launch lhandpro_description display_lhandpro.launch.py \
  model:=/absolute/path/to/robot.urdf
```

当前随包安装的模型：

```text
DH116S-L000-A1
DH116S-R000-A1
DH116-L000-A1
DH116-R000-A1
```

模型资源结构：

```text
src/lhandpro_description/models/<model_id>/
  urdf/<model_id>.urdf
  meshes/*.STL
  mjcf/<model_id>.xml
```

`lhandpro_state_publisher` 默认订阅：

```text
/lhandpro_service/realtime_state
```

并发布：

```text
/joint_states
```

`robot_state_publisher` 再根据 URDF 发布：

```text
/tf
/tf_static
/robot_description
```

当前 `display_lhandpro.launch.py` 已设置：

```text
robot_state_publisher publish_frequency = 100Hz
robot_state_publisher ignore_timestamp = true
```

`lhandpro_state_publisher` 在收到第一帧实时状态之前不会发布默认 0 姿态，避免 RViz 启动时先显示错误初始姿态。

当前 `display_lhandpro.launch.py` 面向单只手模型显示。虽然 `lhandpro_service` 支持多手控制，但模型显示链路默认只订阅 `/lhandpro_service/realtime_state` 并发布全局 `/joint_states`。如果需要同时显示两只手，不能简单启动两套同名 display，否则 `/joint_states` 和 `/tf` 会出现多个发布者；需要为两只手设计不同的模型 frame、joint 名称或 namespace 后再扩展显示链路。

## 动作序列示例

启动服务后，可以运行 Python 示例：

```bash
ros2 launch sequence_demo_py sequence.launch.py
```

或运行 C++ 示例：

```bash
ros2 launch sequence_demo_cpp sequence.launch.py
```

两个示例节点都运行在 `/lhandpro_service` 命名空间下，默认调用无手名前缀的兼容服务。它们适合默认单手配置，或多手配置中的 `default_hand`。

Python 示例流程：

1. 使能所有关节。
2. 回零。
3. 按预设数组依次设置 6 个关节位置。
4. 每步设置速度并调用 `move_motors`。

C++ 示例增加了回零状态等待逻辑，会通过 `get_now_status` 判断回零是否完成。

## 推荐启动顺序

实机调试推荐顺序：

```bash
# 终端 1
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch lhandpro_service lhandpro.launch.py
```

等待日志出现类似：

```text
[hand] 初始化完成 bus=bus0 address=1 dof=6/11
实时状态话题发布频率: 100.0 Hz
```

再启动模型：

```bash
# 终端 2
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch lhandpro_description display_lhandpro.launch.py
```

确认 topic 正常：

```bash
ros2 topic hz /lhandpro_service/realtime_state
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
/lhandpro_service/lhandpro_service
/lhandpro_state_publisher
/robot_state_publisher
/rviz
```

检查关键 topic 发布者数量：

```bash
ros2 topic info -v /lhandpro_service/realtime_state
ros2 topic info -v /joint_states
ros2 topic info -v /tf
```

正常情况下这些 topic 的 `Publisher count` 都应为 `1`。

如果出现 `Publisher count: 2`，说明重复启动或有残留进程。可先正常 `Ctrl+C` 停止对应终端；如果仍有残留，再用进程列表确认：

```bash
ps -ef | grep -E 'lhandpro_service|lhandpro_state_publisher|robot_state_publisher|rviz2' | grep -v grep
```

谨慎杀掉确认无用的残留进程：

```bash
kill <pid>
```

## 常见问题

### RViz 模型在两个位置之间反复跳

优先检查是否重复启动：

```bash
ros2 topic info -v /joint_states
ros2 topic info -v /tf
ros2 topic info -v /lhandpro_service/realtime_state
```

如果 `Publisher count` 大于 1，就是多个节点同时发布同名 topic。清理重复进程后再启动一套即可。

### `/lhandpro_service/realtime_state` 有数据，但 RViz 不动

检查 `/joint_states` 是否发布：

```bash
ros2 topic echo --once /joint_states
```

检查 `robot_state_publisher` 是否发布 `/tf`：

```bash
ros2 topic echo --once /tf
```

检查 RViz 中 RobotModel 的 Description Topic 是否为：

```text
/robot_description
```

### CANFD 启动失败

检查 CAN 接口：

```bash
ip link show
```

确认存在 `can0` 或其他 `can*` 接口。服务默认扫描 `can*`，`channel: 0` 表示扫描结果的第一个接口。

检查权限：

```bash
getcap install/lhandpro_service/lib/lhandpro_service/lhandpro_service
```

必要时重新设置：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/lhandpro_service/lib/lhandpro_service/lhandpro_service
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

确认命名空间。默认服务在 `/lhandpro_service` 下，例如：

```bash
ros2 service list | grep set_enable
```

默认单手兼容服务：

```text
/lhandpro_service/set_enable
```

带手名前缀服务：

```text
/lhandpro_service/hand/set_enable
```

多手配置示例：

```text
/lhandpro_service/left/set_enable
/lhandpro_service/right/set_enable
```

### 构建找不到 LHandProLib

确认文件存在：

```bash
ls src/lhandpro_service/thirdparty/lib/libLHandProLib.so
ls src/lhandpro_service/thirdparty/include/LHandProLib/LHandProLib.hpp
```

重新构建：

```bash
colcon build --packages-select lhandpro_service --symlink-install
source install/setup.bash
```

## 第三方 SDK

SDK 相关文件在：

```text
src/lhandpro_service/thirdparty/
  include/LHandProLib/
  lib/
  bin/
  docs/
  share/LHandProLib/examples/
```

其中 `docs` 和 `share/LHandProLib/examples` 保留了 SDK 手册以及 CANFD、RS485、EtherCAT 的 C++/Python 参考示例。

## 开发提示

- 实时状态使用 topic，不建议模型显示端用服务轮询。
- 服务端允许 CANFD/RS485/EtherCAT 通过同一总线不同 ID 或不同总线相同 ID 控制多只手。
- 不要同时启动多套相同 namespace 的 service/display，否则 `/joint_states` 和 `/tf` 会出现多个发布者。
- 修改 `.srv` 或 `.msg` 后需要重新构建 `lhandpro_interfaces` 以及依赖它的包。
- 修改模型后需要重新构建 `lhandpro_description`，或使用 `--symlink-install` 方便开发调试。

## 许可证

本项目使用 Apache-2.0 许可证。详情见 [LICENSE](LICENSE)。
