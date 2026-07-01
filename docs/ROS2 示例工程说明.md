# ROS2 工程说明

本文档面向当前工程本身，说明代码结构、运行链路、配置方式、常用命令和排错方法。工程采用白标命名，ROS 包、节点、命名空间统一使用 `hand_control`；模型目录名和第三方 SDK 名称保持资源原始命名。

## 工程目标

当前工程提供一套 ROS2 手部设备控制工作空间，主要能力包括：

- 通过 CANFD、RS485、EtherCAT 三类通讯链路连接设备。
- 通过 ROS2 service 暴露设备控制、参数配置和状态查询接口。
- 通过 ROS2 topic 以默认 100Hz 发布实时状态，供显示和上层控制使用。
- 支持单手控制，也支持同一总线不同 ID 或不同总线相同/不同 ID 的多手拓扑。
- 提供 RViz 模型显示链路，把实时状态同步为 `/joint_states`。
- 提供 C++ 和 Python 动作序列示例，便于验证服务调用链路。

## 目录结构

```text
./
  README.md
  docs/
    ROS2 示例工程说明.md
    ROS2 Example Project Description.md
  src/
    hand_control_interfaces/
      msg/HandJointState.msg
      srv/*.srv
    hand_control_service/
      config/service.yaml
      config/two_hands_same_bus.yaml
      config/two_hands_two_buses.yaml
      launch/service.launch.py
      src/
      thirdparty/
    hand_control_description/
      config/model.yaml
      config/display_robot_model.rviz
      launch/display.launch.py
      models/
      scripts/import_model.py
    sequence_demo_cpp/
    sequence_demo_py/
    ros2_hand_control.conf
```

## 包职责

| 包 | 职责 |
| --- | --- |
| `hand_control_interfaces` | 定义 `HandJointState` topic 消息和全部 service 接口。 |
| `hand_control_service` | 核心控制节点，加载总线/手配置，初始化 SDK，注册 service，发布实时状态 topic。 |
| `hand_control_description` | 管理模型资源、RViz 配置和 `hand_state_publisher` 显示节点。 |
| `sequence_demo_cpp` | C++ service 调用示例。 |
| `sequence_demo_py` | Python service 调用示例。 |

## 运行链路

```text
sequence demo / external ROS2 client
        |
        | service call
        v
hand_control_service
        |
        | third-party SDK callback
        v
CANFD / RS485 / EtherCAT bus
        |
        v
physical hand device

hand_control_service
        |
        | /hand_control/realtime_state
        v
hand_state_publisher
        |
        | /joint_states
        v
robot_state_publisher + RViz
```

## 构建

包名或接口改动后，建议清理旧构建产物，避免 ROS2 ament index 中残留旧包名：

```bash
# 在当前项目根目录执行
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build --symlink-install
source install/setup.bash
```

只构建核心包：

```bash
colcon build --symlink-install --packages-select \
  hand_control_interfaces \
  hand_control_service \
  hand_control_description
```

构建后确认包可见：

```bash
ros2 pkg list | grep hand_control
ros2 interface show hand_control_interfaces/msg/HandJointState
```

## 动态库路径

服务节点依赖第三方 SDK 动态库。若运行时报找不到动态库，可以按当前安装路径修改 `src/ros2_hand_control.conf`，再写入系统动态库配置：

```bash
sudo cp src/ros2_hand_control.conf /etc/ld.so.conf.d/
sudo ldconfig
```

也可以临时设置：

```bash
export LD_LIBRARY_PATH=$PWD/install/hand_control_service/lib:$LD_LIBRARY_PATH
```

## 权限

默认 libcanbus 后端需要系统可加载 `libcanbus.so` 及其依赖。若关闭 libcanbus 并改用 SocketCAN，CANFD 通常需要网络 raw 权限：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

RS485 串口通常需要当前用户具备串口权限：

```bash
sudo usermod -aG dialout $USER
```

修改用户组后需要重新登录终端会话。

## 服务配置

默认配置文件：

```text
src/hand_control_service/config/service.yaml
```

默认启动命令：

```bash
ros2 launch hand_control_service service.launch.py
```

指定配置文件：

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/service.yaml
```

核心参数说明：

| 参数 | 说明 |
| --- | --- |
| `bus_names` | 总线名称列表，每个名称必须在 `buses` 下有对应配置。 |
| `hand_names` | 手名称列表，每个名称必须在 `hands` 下有对应配置。 |
| `default_hand` | 兼容无手名前缀服务时默认操作的手。 |
| `compat_single_hand_services` | 是否注册 `/hand_control/<service>` 这类兼容服务。 |
| `publish_realtime_state` | 是否发布实时状态 topic。 |
| `realtime_state_publish_rate_hz` | 实时状态发布频率，默认 100Hz。 |
| `buses.<name>.type` | `canfd`、`rs485`、`ethercat`，`ecat` 可作为 EtherCAT 别名。 |
| `buses.<name>.channel` | 扫描到的设备索引：CAN 设备、串口或网卡。 |
| `hands.<name>.bus` | 当前手使用的总线名称。 |
| `hands.<name>.address` | CANFD/RS485 节点 ID，或 EtherCAT 从站号。 |
| `hands.<name>.hand_type` | SDK 手型枚举，例如 `LAC_DOF_6`、`LAC_DOF_6_S`、`LAC_DOF_16`。 |

## 通讯方式

### CANFD

CANFD 配置示例：

```yaml
buses:
  bus0:
    type: canfd
    channel: 0
    canfd_arb_baudrate: 1000000
    canfd_data_baudrate: 5000000
```

`canfd_arb_baudrate` 和 `canfd_data_baudrate` 直接传入 SDK，不再额外乘以 1000。

Linux 默认使用 libcanbus；构建时关闭 `HAND_CONTROL_USE_LIBCANBUS` 可切换到 SocketCAN 后端：

```bash
colcon build --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=OFF
```

### RS485

RS485 配置示例：

```yaml
buses:
  bus0:
    type: rs485
    channel: 0
    rs485_baudrate: 500000
```

Linux 串口扫描优先级按代码实现为：

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
其他 /dev/tty* 兜底
/dev/ttyS*
```

其中虚拟控制台类 `tty`、`ttyprintk`、`tty0` 等会被过滤；标准 `ttyS*` 会放在最后兜底。

### EtherCAT

EtherCAT 配置示例：

```yaml
buses:
  bus0:
    type: ethercat
    channel: 0
hands:
  hand:
    bus: bus0
    address: 1
```

`channel` 对应扫描到的网卡索引，`address` 对应 SOEM 从站号。

## 多手拓扑

同一总线不同 ID：

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/two_hands_same_bus.yaml
```

不同总线相同或不同 ID：

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/two_hands_two_buses.yaml
```

服务端按 `hands.<name>.bus` 找到总线，再把 `hands.<name>.address` 交给 SDK。CANFD/RS485 的地址是节点 ID；EtherCAT 的地址是从站号。无需在 ROS 层按 CAN 帧 ID 做额外路由。

## 服务命名

服务节点默认运行在 `/hand_control` namespace 下。

单手兼容服务：

```text
/hand_control/<service>
```

具名手服务：

```text
/hand_control/<hand_name>/<service>
```

例如：

```bash
ros2 service call /hand_control/set_enable \
  hand_control_interfaces/srv/SetEnable "{id: 1, enable: true}"

ros2 service call /hand_control/left/set_enable \
  hand_control_interfaces/srv/SetEnable "{id: 1, enable: true}"
```

常用服务类别：

| 类别 | 服务 |
| --- | --- |
| 运动控制 | `set_position`、`set_angle`、`move_motors`、`stop_motors`、`home_motors` |
| 速度/电流 | `set_position_velocity`、`set_angular_velocity`、`set_max_current`、`set_home_current` |
| 状态读取 | `get_now_position`、`get_now_angle`、`get_now_current`、`get_now_status`、`get_now_alarm` |
| 参数读取 | `get_position`、`get_angle`、`get_control_mode`、`get_enable`、`get_serial_number` |
| 通讯配置 | `set_can_node_id`、`set_canfd_arb_baudrate`、`set_rs485_node_id`、`set_rs485_baudrate` |

查看完整接口：

```bash
ros2 interface list | grep hand_control_interfaces
ros2 service list | grep hand_control
```

## 实时状态 Topic

服务端发布 `hand_control_interfaces/msg/HandJointState`。

默认单手 topic：

```text
/hand_control/realtime_state
/hand_control/hand/realtime_state
```

多手 topic：

```text
/hand_control/<hand_name>/realtime_state
```

消息字段：

```text
header
hand_name
bus_name
address
active_dof
joint_ids
angles
angular_velocities
positions
position_velocities
currents
statuses
alarms
```

检查发布频率：

```bash
ros2 topic hz /hand_control/realtime_state
ros2 topic echo /hand_control/realtime_state
```

## 模型显示

模型配置文件：

```text
src/hand_control_description/config/model.yaml
```

默认启动：

```bash
ros2 launch hand_control_description display.launch.py
```

不启动 RViz，只启动状态同步和 TF：

```bash
ros2 launch hand_control_description display.launch.py use_rviz:=false
```

指定模型目录：

```bash
ros2 launch hand_control_description display.launch.py model_id:=<model_directory_name>
```

指定完整 URDF 或 xacro：

```bash
ros2 launch hand_control_description display.launch.py model:=/absolute/path/to/model.urdf
```

显示链路：

```text
hand_control_service -> /hand_control/realtime_state
hand_state_publisher -> /joint_states
robot_state_publisher -> /tf
rviz2 -> RobotModel
```

当前显示 launch 面向单只手模型。服务端可以控制多手，但如果要同时显示两只手，需要为两套模型准备不同 frame、joint 名称或 namespace 后再扩展显示节点。

## 动作序列示例

启动 Python 示例：

```bash
ros2 launch sequence_demo_py sequence.launch.py
```

启动 C++ 示例：

```bash
ros2 launch sequence_demo_cpp sequence.launch.py
```

两个示例默认在 `/hand_control` namespace 下调用兼容服务，适合默认单手配置或多手配置中的 `default_hand`。

## 推荐启动顺序

终端 1：启动服务端。

```bash
source install/setup.bash
ros2 launch hand_control_service service.launch.py
```

等待设备初始化完成，再确认 topic：

```bash
ros2 topic hz /hand_control/realtime_state
```

终端 2：启动模型显示。

```bash
source install/setup.bash
ros2 launch hand_control_description display.launch.py
```

终端 3：运行动作序列或手动 service call。

```bash
source install/setup.bash
ros2 launch sequence_demo_py sequence.launch.py
```

## 停止和重复启动检查

如果使用 `Ctrl+C` 后 RViz 仍然乱跳，优先检查是否存在旧进程或重复发布者：

```bash
ros2 node list
ros2 topic info -v /joint_states
ros2 topic info -v /hand_control/realtime_state
```

同一个模型显示链路中，`/joint_states` 应只有一个有效发布者。若存在多个旧的 `hand_state_publisher`、`robot_state_publisher` 或 RViz 相关进程，需要先停止旧进程再重启。

## 常见问题

### 找不到服务

确认服务节点已启动且 namespace 正确：

```bash
ros2 service list | grep hand_control
ros2 node list | grep hand_control
```

### 实时 topic 有数据但 RViz 不动

检查 `/joint_states` 是否有数据：

```bash
ros2 topic echo /joint_states
ros2 topic info -v /joint_states
```

再检查模型中的 joint 名称是否和 `hand_state_publisher` 发布的名称匹配。

### RViz 模型位置在旧位置和新位置之间跳变

常见原因是存在两个 `/joint_states` 发布者，或之前通过 launch 启动的旧进程没有完全退出。先用 `ros2 topic info -v /joint_states` 找发布者，再清理重复进程。

### CANFD 启动失败

检查：

- CAN 设备是否能被扫描到。
- `channel` 是否是正确的设备索引。
- 默认 libcanbus 后端下，系统是否可加载 `libcanbus.so` 及其依赖；SocketCAN 后端下，是否具备网络权限。
- 波特率配置是否和设备一致。

### RS485 找不到设备

检查：

- 串口是否出现在 `/dev/ttyXR*`、`/dev/ttyUSB*`、`/dev/ttyACM*` 或其他扫描列表中。
- 当前用户是否有串口读写权限。
- `channel` 是否对应扫描列表中的正确序号。
- `rs485_baudrate` 是否被系统 termios 支持。

### 构建找不到 SDK

检查：

```bash
ls src/hand_control_service/thirdparty/include/
ls src/hand_control_service/thirdparty/lib/
```

如果使用系统库，确认 `ldconfig -p` 能查到相关动态库。

## 维护建议

- 修改 `.srv` 或 `.msg` 后，需要重新构建 `hand_control_interfaces` 和依赖它的包。
- 修改模型资源后，优先使用 `--symlink-install`，便于 RViz 调试。
- 修改 package 名称后，需要清理 `build/`、`install/`、`log/`。
- 不要同时启动多个相同 namespace 的服务端或显示节点。
- 多手控制可以共用总线，也可以分总线；路由信息来自 YAML，不在 ROS 层硬编码 CAN 帧 ID。
