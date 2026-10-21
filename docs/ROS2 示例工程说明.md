# ROS2 工程说明

本文档面向当前工程本身，说明代码结构、运行链路、配置方式、常用命令和排错方法。工程采用白标命名，ROS 包、节点、命名空间统一使用 `hand_control`；模型目录名和第三方 SDK 名称保持资源原始命名。

## 工程目标

当前工程提供一套 ROS2 手部设备控制工作空间，主要能力包括：

- 通过 CANFD、RS485、EtherCAT 三类通讯链路连接设备。
- 通过 ROS2 service 暴露设备控制、参数配置和状态查询接口。
- 通过 ROS2 topic 以默认 100Hz 发布实时状态，供显示和上层控制使用。
- 通过 ROS2 topic 发布触觉传感器数据，覆盖 11 个位点的压力、法向力、切向力、受力方向和接近觉（可选，默认关闭）。
- 支持单手控制，也支持同一总线不同 ID 或不同总线相同/不同 ID 的多手拓扑。
- 提供 RViz 模型显示链路，把实时状态同步为 `/joint_states`。
- 提供 C++ 和 Python 动作序列示例，以及 C++ 和 Python 触觉自检示例。

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
    tactile_demo_cpp/
    tactile_demo_py/
    ros2_hand_control.conf
```

## 包职责

| 包 | 职责 |
| --- | --- |
| `hand_control_interfaces` | 定义 `HandJointState`、`TactileState` topic 消息和全部 service 接口。 |
| `hand_control_service` | 核心控制节点，加载总线/手配置，初始化 SDK，注册 service，发布实时状态和触觉 topic。 |
| `hand_control_description` | 管理模型资源、RViz 配置和 `hand_state_publisher` 显示节点。 |
| `sequence_demo_cpp` | C++ service 调用示例。 |
| `sequence_demo_py` | Python service 调用示例。 |
| `tactile_demo_cpp` | C++ 触觉传感器自检示例。 |
| `tactile_demo_py` | Python 触觉传感器自检示例。 |

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

> **改动 `hand_control_interfaces` 的接口定义后要清理再重建，删除接口时尤其如此。**
> 增量构建不会清理 `install/hand_control_interfaces` 里旧的 typesupport 动态库，
> 它们仍会引用已删除的符号，运行时报 `undefined symbol: ...__convert_from_py`。
> 保险起见直接全清 `rm -rf build install log`。

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

CANFD SocketCAN 方式通常需要网络 raw 权限：

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
| `hands.<name>.tactile_enabled` | 是否采集并发布触觉，默认 `false`。详见「触觉 Topic 与服务」。 |
| `tactile_publish_rate_hz` | 触觉发布频率，默认 50Hz，所有开启的手共用一个定时器。 |

> 频率类参数在代码里声明为 `double`，**必须写成小数形式**（`50.0`）。
> 写成 `50` 会让节点在启动阶段抛 `InvalidParameterTypeException` 直接退出。
> `realtime_state_publish_rate_hz` 同理。

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

服务启动时会按配置设置 CANFD 波特率。

CANFD 有两种后端，**默认使用 gsusb-canfd**（用户态 libusb 的 `gs_usb` 驱动，Windows / Linux / macOS 通用，支持 CAN FD）。后端在编译期二选一，由构建开关决定，运行时不能切换。

| 后端 | 构建开关 | 适用场景 |
| --- | --- | --- |
| gsusb-canfd | 默认（ON） | candleLight / `gs_usb` 兼容 USB-CANFD 适配器，依赖 `libusb-1.0` |
| SocketCAN | `-DHAND_CONTROL_USE_SOCKETCAN=ON` | 内核 SocketCAN 接口，即 `can0` / `can1`，仅 Linux |

#### gsusb-canfd 后端（默认）

源码内置于 `thirdparty/gsusb-canfd`，构建时作为静态库链接。需要 `libusb-1.0` 开发包（`sudo apt install libusb-1.0-0-dev`），并建议安装 udev 规则：

```bash
sudo cp src/hand_control_service/thirdparty/share/99-hand-control-usb.rules \
  /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

直接构建即为该后端：

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service
```

#### 切换到 SocketCAN

```bash
colcon build --symlink-install --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_SOCKETCAN=ON
```

SocketCAN 扫描 `/sys/class/net` 下以 `can` 开头的接口，需要给可执行文件网络权限：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

> **切换后必须清理再重建。** 开关会写入 `CMakeCache.txt` 并生成对应的 `HAND_CONTROL_CANFD_USE_*` 编译宏。
> 在已有构建树上直接改选项，CMake 不一定重新生成宏定义，会出现「开关是 ON、代码里宏没生效」。
> 至少清理 `build/hand_control_service` 和 `install/hand_control_service`。

SocketCAN 直接使用内核 `can*` 接口，不依赖额外的厂商动态库。

#### 切回 gsusb-canfd（默认）

清理后重建，不传任何后端开关即可：

```bash
rm -rf build/hand_control_service install/hand_control_service
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service
```

#### 确认当前用的是哪个后端

```bash
grep -n "HAND_CONTROL_USE_GSUSB_CANFD\|HAND_CONTROL_USE_SOCKETCAN" \
  build/hand_control_service/CMakeCache.txt
grep -R --line-number -- "-DHAND_CONTROL_CANFD_USE" \
  build/hand_control_service/CMakeFiles/hand_control_service.dir
```

第二条输出里出现 `HAND_CONTROL_CANFD_USE_GSUSB` 即为 gsusb-canfd 后端，出现 `HAND_CONTROL_CANFD_USE_SOCKETCAN` 即为 SocketCAN 后端；也能抓出「开关改了但没清理」的情况。

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

同一串口上的多只手由 SDK 的 `RS485SharedBus` 统一调度，分包、CRC、发送时序和按节点分发都在 SDK 内完成；事务窗口按波特率自动设置。

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

服务端会注册 EtherCAT 的 CoE SDO 回调，`initial()` 可通过邮箱自动识别手型、左右手、固件和 SN。

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

## 触觉 Topic 与服务

触觉采集默认关闭。在手配置里打开 `tactile_enabled` 后，服务端额外发布
`hand_control_interfaces/msg/TactileState`：

```text
/hand_control/<hand_name>/tactile_state
```

触觉是新增能力，**不会**生成无前缀的兼容副本，只有带手名的那一份。

### 开启方式

```yaml
# 顶层参数：触觉发布频率，只有至少一只手开启时才生效
tactile_publish_rate_hz: 50.0

hands:
  hand:
    tactile_enabled: true
```

| 参数 | 位置 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `tactile_enabled` | `hands.<name>` | `false` | 是否采集并发布触觉。没有传感器的手应保持关闭，避免空转。 |
| `tactile_publish_rate_hz` | 顶层 | `50.0` | 发布频率，所有开启的手共用同一个定时器。必须写成小数。 |

每帧固定遍历全部 11 个位点和全部 5 个通道，没有额外的可选配置项。

### 采集开销

一次发布 = 11 个位点（5 指尖 + 5 指腹 + 手掌）× 5 个通道。位点的传感单元数
`cell_count` 在首次采集时探测一次并缓存，重连或重新初始化后失效重查，所以稳态下
是 55 次读取，不会因为两阶段调用而翻倍。

总线跟不上时先调低 `tactile_publish_rate_hz`。

### 消息结构

`TactileState.sites[]` 每个元素对应一个位点，`sensor_id` 即 SDK 的 `LSS_*` 枚举值：

| 字段 | 量纲 |
| --- | --- |
| `pressure` | 归一化 [0,1]，相对零点基准的差值 |
| `normal_force` | N |
| `tangential_force` | N |
| `force_direction` | deg，[0,360] |
| `proximity` | 归一化 [0,1] |

每个数组的长度等于该位点的 `cell_count`。某通道读取失败时数组为空，因此可以用
`empty()` 区分「读取失败」和「读到全零」；`cell_count` 为 0 表示该位点不可用。

### 配套服务

与其他服务一样带手名前缀：

| 服务 | 请求 | 说明 |
| --- | --- | --- |
| `set_sensor_enable` | `int32 enable` | 开启或关闭传感器监控，0 关 1 开。 |
| `set_finger_pressure_reset` | 无 | 将当前读数设为零点基准。 |
| `get_finger_sensor_pos` | `int32 sensor_id` | 查询该位点传感单元的分布坐标，归一化到 [0,1]。 |

`tactile_enabled` 开启时，服务节点会在初始化时自动下发一次传感器使能，通常不需要
手动调用 `set_sensor_enable`。

### 典型用法

`pressure` 是相对基准的差值，要读绝对压力必须先设定零点：

```bash
ros2 service call /hand_control/hand/set_finger_pressure_reset \
  hand_control_interfaces/srv/SetFingerPressureReset
ros2 topic echo /hand_control/hand/tactile_state
ros2 topic hz /hand_control/hand/tactile_state
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

## 触觉传感器自检示例

`tactile_demo_py` 和 `tactile_demo_cpp` 用来验证触觉链路是否正常工作。流程是：
使能传感器 → 设定压力零点 → 采集若干帧 → 输出统计报告。

先确保服务端对应手开启了 `tactile_enabled`，然后：

```bash
ros2 launch tactile_demo_py tactile.launch.py
# 或
ros2 launch tactile_demo_cpp tactile.launch.py
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `hand_name` | `hand` | 目标手名，决定订阅的话题和调用的服务前缀。 |
| `sample_count` | `20` | 采集帧数，收够即出报告。 |
| `timeout_sec` | `10.0` | 采集超时；超时后按已收到的样本出报告。 |

输出内容包括实测发布频率、每个位点的 `cell_count` 与各通道的数组长度和最大值，
以及一次 `get_finger_sensor_pos` 的返回规模。全程无数据的通道会单独告警。

与动作序列示例不同，这两个节点使用**带手名前缀**的服务名（例如
`hand/set_sensor_enable`），因此无论是否开启兼容模式都能找到服务。

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

默认后端是 gsusb-canfd。启动日志出现 `libusb_open failed: LIBUSB_ERROR_ACCESS` 说明
USB 适配器存在但没有访问权限，安装 udev 规则后重新插拔适配器：

```bash
sudo cp src/hand_control_service/thirdparty/share/99-hand-control-usb.rules \
  /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

用 `lsusb` 确认能看到适配器（例如 `a8fa:8598 ... CANFD Analyser`）。若日志是
「未找到 CANFD 通道」，说明没有扫描到 `gs_usb` 适配器。

如果用的是 SocketCAN 后端，检查：

- CAN 设备是否能被扫描到（`ip link show` 里是否存在 `can*` 接口）。
- `channel` 是否是正确的设备索引。
- 是否具备 SocketCAN 权限：`getcap install/hand_control_service/lib/hand_control_service/hand_control_service`。
- 波特率配置是否和设备一致。

### 触觉话题没有数据

确认手配置里打开了开关，并且日志里没有出现「没有手开启 tactile_enabled」：

```bash
ros2 param get /hand_control_service hands.hand.tactile_enabled
ros2 topic list | grep tactile
```

话题存在但 `sites` 里 `cell_count` 全是 0，说明 SDK 没探测到传感单元，常见原因：

- 该手型本身不带触觉传感器
- 传感器未使能，手动调用一次 `set_sensor_enable` 再试
- 手未初始化（总线没连上），此时同一只手的其他服务调用也会返回 `-1`

话题存在但频率明显低于配置的 `tactile_publish_rate_hz`，说明总线带宽不够，需要降频。

### 节点启动即崩溃，提示参数类型不匹配

启动日志出现：

```text
terminate called after throwing an instance of 'rclcpp::exceptions::InvalidParameterTypeException'
  what():  parameter 'tactile_publish_rate_hz' has invalid type ...
  ... is of type {double}, setting it to {integer} is not allowed
```

频率类参数在代码里声明为 `double`，**必须写成小数形式**：

```yaml
tactile_publish_rate_hz: 50.0    # 正确
tactile_publish_rate_hz: 50      # 错误，节点无法启动
```

`tactile_publish_rate_hz` 和 `realtime_state_publish_rate_hz` 都是这个规则，命令行
覆盖时同样要带小数点（`-p tactile_publish_rate_hz:=50.0`）。

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
- 实时状态和触觉都用 topic，不要在模型显示端或上层用服务轮询高频数据。
- 删除 `.srv` / `.msg` 后必须清理构建产物再重建，否则旧 typesupport 库会引用已删符号。
