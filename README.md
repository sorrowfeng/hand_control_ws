# Hand Control ROS2 Workspace

这是一个面向手部设备控制的 ROS2 工作空间，包含设备通讯服务、ROS2 接口定义、模型显示、动作序列示例和第三方 SDK 适配文件。

当前工程支持：

- CANFD、RS485、EtherCAT 三种通讯方式。
- 单手控制，以及两只手的多拓扑控制。
- 同一总线不同 ID 控制多只手。
- 不同总线相同或不同 ID 分别控制多只手。
- EtherCAT 同一网卡不同从站，或不同网卡相同/不同从站。
- 实时状态 topic 发布，用于位置、速度、电流、状态、报警等高频数据。
- 触觉传感器 topic 发布，覆盖 11 个位点的压力、法向力、切向力、受力方向和接近觉（可选，默认关闭）。
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
    tactile_demo_cpp/          C++ 触觉传感器自检示例
    tactile_demo_py/           Python 触觉传感器自检示例
    ros2_hand_control.conf     可选的动态库路径配置示例
```

## 包说明

| 包 | 作用 |
| --- | --- |
| `hand_control_interfaces` | 定义服务接口，以及 `HandJointState` 实时状态消息和 `TactileState` 触觉消息。 |
| `hand_control_service` | 核心控制服务。负责加载 YAML 拓扑、初始化总线和手、注册服务、发布实时状态和触觉 topic。 |
| `hand_control_description` | 模型资源、RViz 配置和 `hand_state_publisher`。该节点订阅实时状态并发布 `/joint_states`。 |
| `sequence_demo_cpp` | C++ 版本动作序列调用示例。 |
| `sequence_demo_py` | Python 版本动作序列调用示例。 |
| `tactile_demo_cpp` | C++ 版本触觉传感器自检示例。 |
| `tactile_demo_py` | Python 版本触觉传感器自检示例。 |

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

默认 CANFD 后端 gsusb-canfd 需要 libusb 开发包；只有切到 SocketCAN 后端时才需要 `can-utils`：

```bash
sudo apt install -y libusb-1.0-0-dev
# 仅 SocketCAN 后端需要
# sudo apt install -y can-utils
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

> **改动 `hand_control_interfaces` 的接口定义后要清理再重建，删除接口时尤其如此。**
> 增量构建不会清理 `install/hand_control_interfaces` 里旧的 typesupport 动态库，它们
> 仍会引用已删除的符号，运行时报：
>
> ```text
> undefined symbol: hand_control_interfaces__srv__xxx__request__convert_from_py
> ```
>
> 清理 `build/hand_control_interfaces` 和 `install/hand_control_interfaces` 后重建即可，
> 保险起见直接全清 `rm -rf build install log`。

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

SocketCAN 方式需要操作 CAN 网络接口。如果不希望每次用 `sudo` 运行服务，可给服务可执行文件添加能力：

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
    # 触觉话题发布频率，只有至少一只手开启 tactile_enabled 时才生效
    tactile_publish_rate_hz: 50.0

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
        # 触觉采集默认关闭，没有传感器的手开启它只会空转
        tactile_enabled: false
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
| `tactile_publish_rate_hz` | 触觉话题发布频率，默认 50Hz。详见「触觉 Topic 与服务」。 |
| `buses.<name>.type` | `canfd`、`rs485`、`ethercat`。也支持 `ecat` 作为 EtherCAT 别名。 |
| `buses.<name>.channel` | 总线通道索引。CANFD 对应扫描到的 CAN 设备；RS485 对应扫描到的串口；EtherCAT 对应扫描到的网卡。 |
| `hands.<name>.bus` | 该手挂在哪条总线上。 |
| `hands.<name>.address` | CANFD/RS485 节点 ID，或 EtherCAT 从站号。 |
| `hands.<name>.hand_type` | SDK 手型枚举名，例如 `LAC_DOF_6`、`LAC_DOF_6_S`、`LAC_DOF_16`。 |
| `hands.<name>.tactile_enabled` | 是否采集并发布触觉，默认 `false`。详见「触觉 Topic 与服务」。 |

## 通讯方式

### CANFD

CANFD 有三种后端，**默认使用 gsusb-canfd**（用户态 libusb 的 `gs_usb` 驱动，Windows / Linux / macOS 通用，完整支持 CAN FD）。后端在编译期四选一，由构建开关决定，运行时不能切换。

| 后端 | 构建开关 | 适用场景 |
| --- | --- | --- |
| gsusb-canfd | 默认（ON） | candleLight / `gs_usb` 兼容 USB-CANFD 适配器，三平台同一套代码，依赖 `libusb-1.0` |
| SocketCAN | `-DHAND_CONTROL_USE_SOCKETCAN=ON` | 内核 SocketCAN 接口，即 `can0` / `can1`，仅 Linux |
| libcanbus | `-DHAND_CONTROL_USE_LIBCANBUS=ON` | 厂商 USB-CANFD 适配器，仅 Linux |
| HCanbus | `-DHAND_CONTROL_USE_HCANBUS=ON` | 厂商 HCanbus SDK，仅 Windows |

各后端共用同一份总线配置，`channel` 和波特率的写法没有区别：

```yaml
buses:
  bus0:
    type: canfd
    channel: 0
    canfd_arb_baudrate: 1000000
    canfd_data_baudrate: 5000000
```

`channel` 是各后端扫描结果里的索引：gsusb-canfd 对应扫描到的适配器，SocketCAN 对应扫描到的 `can*` 接口。服务启动时会按配置设置 CANFD 波特率（gsusb 默认采样点 0.80 / 0.75）。

#### gsusb-canfd 后端（默认）

源码内置于 `src/hand_control_service/thirdparty/gsusb-canfd`，构建时作为静态库 `add_subdirectory`，链接 `canfd` 目标。三平台共用同一段代码。

运行/构建环境需要 `libusb-1.0` 开发包：

```bash
# Debian / Ubuntu
sudo apt install libusb-1.0-0-dev
# macOS
brew install libusb
```

适配器需要绑定 WinUSB（Windows）或具备 USB 访问权限（Linux）。Linux 上建议安装 udev 规则（`thirdparty/share/libcanbus/hcanbus.rules`），否则会打印 `libusb_open failed: LIBUSB_ERROR_ACCESS`：

```bash
sudo cp src/hand_control_service/thirdparty/share/libcanbus/hcanbus.rules \
  /etc/udev/rules.d/99-hcanbus.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

直接构建即为该后端：

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service
```

#### 切换到 SocketCAN

SocketCAN 扫描 `/sys/class/net` 下以 `can` 开头的接口（`can0` / `can1`），需要给可执行文件设置网络权限，否则启动会失败：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

构建时选择该后端（会关闭默认的 gsusb）：

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_SOCKETCAN=ON
```

#### 切换到 libcanbus 后端

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=ON
```

> **切换后必须清理再重建。** 开关会写入 `CMakeCache.txt` 并生成对应的 `HAND_CONTROL_CANFD_USE_*` 编译宏。在已有构建树上直接改选项，CMake 不一定重新生成宏定义，容易出现「开关是 ON、但代码里宏没生效」的假象。至少清理：
>
> ```bash
> rm -rf build/hand_control_service install/hand_control_service
> ```
>
> 保险起见直接全清：`rm -rf build install log`。

libcanbus 在运行时通过 `dlopen` 加载，不参与链接，所以构建机上可以不装这个库，但**运行机上必须有**。库按以下顺序查找：

1. 环境变量 `HAND_CONTROL_LIBCANBUS_PATH` 指向的完整路径
2. `libcanbus.so`（走系统动态库搜索路径）
3. `/usr/local/lib/libcanbus.so`

装在非标准位置时直接指定：

```bash
export HAND_CONTROL_LIBCANBUS_PATH=/path/to/libcanbus.so
ros2 launch hand_control_service service.launch.py
```

该后端同时依赖 `libusb-1.0.so`，一般系统自带；缺失时启动会打印 `Failed to load libusb-1.0.so`。

#### 切回 gsusb-canfd（默认）

清理后重建，不传任何后端开关即可（也可以用 `-DHAND_CONTROL_USE_GSUSB_CANFD=ON` 显式指定）：

```bash
rm -rf build/hand_control_service install/hand_control_service
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service
```

#### 确认当前用的是哪个后端

看构建缓存里的开关值：

```bash
grep -n "HAND_CONTROL_USE_GSUSB_CANFD\|HAND_CONTROL_USE_SOCKETCAN\|HAND_CONTROL_USE_LIBCANBUS" \
  build/hand_control_service/CMakeCache.txt
```

再看宏是否真的传给了编译器，这一步能抓出「开关改了但没清理」的情况：

```bash
grep -R --line-number -- "-DHAND_CONTROL_CANFD_USE" \
  build/hand_control_service/CMakeFiles/hand_control_service.dir
```

输出里出现 `HAND_CONTROL_CANFD_USE_GSUSB` 即为 gsusb-canfd 后端，`HAND_CONTROL_CANFD_USE_SOCKETCAN` / `HAND_CONTROL_CANFD_USE_LIBCANBUS` 同理。

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

触觉传感器（需先开启 `tactile_enabled`，详见「触觉 Topic 与服务」）：

| 服务 | 说明 |
| --- | --- |
| `set_sensor_enable` | 开启或关闭传感器监控。 |
| `set_finger_pressure_reset` | 将当前读数设为压力零点基准。 |
| `get_finger_sensor_pos` | 查询位点传感单元的分布坐标。 |

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

## 触觉 Topic 与服务

触觉采集默认关闭。在手配置里打开 `tactile_enabled` 后，服务端会额外发布
`hand_control_interfaces/msg/TactileState`：

```text
/hand_control/<hand_name>/tactile_state
```

触觉是新增能力，没有历史包袱，因此**不会**生成无前缀的兼容副本，只有带手名的那一份。

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
| `tactile_publish_rate_hz` | 顶层 | `50.0` | 发布频率，所有开启的手共用同一个定时器。**必须写成小数**，写成 `50` 会导致节点启动失败。 |

每帧固定遍历全部 11 个位点和全部 5 个通道，没有额外的可选配置项。

### 采集开销

一次发布 = 11 个位点（5 指尖 + 5 指腹 + 手掌）× 5 个通道。位点的传感单元数
`cell_count` 在首次采集时探测一次并缓存，重连或重新初始化后失效重查，所以稳态下
是 55 次读取，不会因为两阶段调用而翻倍。

总线跟不上时先调低 `tactile_publish_rate_hz`。实测一轮采集耗时超过半个发布周期
就该降频。

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

`get_finger_sensor_pos` 返回的是静态坐标，不随采样变化，因此没有放进 topic。

### 典型用法

`pressure` 是相对基准的差值，要读绝对压力必须先设定零点：

```bash
ros2 service call /hand_control/hand/set_finger_pressure_reset \
  hand_control_interfaces/srv/SetFingerPressureReset
ros2 topic echo /hand_control/hand/tactile_state
```

查看实际发布频率，确认总线是否跑得满：

```bash
ros2 topic hz /hand_control/hand/tactile_state
```

运行中手动开关传感器采集：

```bash
ros2 service call /hand_control/hand/set_sensor_enable \
  hand_control_interfaces/srv/SetSensorEnable "{enable: 1}"
```

查询某个位点的传感单元坐标：

```bash
ros2 service call /hand_control/hand/get_finger_sensor_pos \
  hand_control_interfaces/srv/GetFingerSensorPos "{sensor_id: 1}"
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

## 触觉传感器自检示例

`tactile_demo_py` 和 `tactile_demo_cpp` 用来验证触觉链路是否正常工作。流程是：
使能传感器 → 设定压力零点 → 采集若干帧 → 输出统计报告。

先确保服务端对应手开启了 `tactile_enabled`，然后：

```bash
ros2 launch tactile_demo_py tactile.launch.py
# 或
ros2 launch tactile_demo_cpp tactile.launch.py
```

参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `hand_name` | `hand` | 目标手名，决定订阅的话题和调用的服务前缀。 |
| `sample_count` | `20` | 采集帧数，收够即出报告。 |
| `timeout_sec` | `10.0` | 采集超时；超时后按已收到的样本出报告。 |

输出内容包括实测发布频率、每个位点的 `cell_count` 与各通道的数组长度和最大值，
以及一次 `get_finger_sensor_pos` 的返回规模。全程无数据的通道会单独告警。

没有收到数据时，节点会提示检查 `tactile_enabled` 是否开启、以及该手型是否带传感器。
注意服务调用返回 `-1` 通常表示手未初始化（总线没连上），此时后面的采集必然为空。

与动作序列示例不同，这两个节点使用**带手名前缀**的服务名
（例如 `hand/set_sensor_enable`），因此无论是否开启兼容模式都能找到服务。

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

默认后端是 gsusb-canfd。启动日志出现 `libusb_open failed: LIBUSB_ERROR_ACCESS`
说明 USB 适配器存在但没有访问权限，安装 udev 规则后重新插拔适配器：

```bash
sudo cp src/hand_control_service/thirdparty/share/libcanbus/hcanbus.rules \
  /etc/udev/rules.d/99-hcanbus.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

用 `lsusb` 确认能看到适配器（例如 `a8fa:8598 ... CANFD Analyser`）。若日志是
「未找到 CANFD 通道」，说明没有扫描到 `gs_usb` 适配器，检查 USB 连接与驱动绑定。

如果用的是 SocketCAN 后端，则检查 CAN 接口与权限：

```bash
ip link show
```

确认存在 `can0` 或其他 `can*` 接口。服务默认扫描 `can*`，`channel: 0` 表示扫描结果的第一个接口。

```bash
getcap install/hand_control_service/lib/hand_control_service/hand_control_service
```

必要时重新设置：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

### libcanbus 后端加载失败

启动日志打印 `Failed to load libcanbus.so` 或 `Failed to load libusb-1.0.so`，
说明构建用的是 libcanbus 后端，但运行时没找到库。

先确认构建确实切到了该后端。若开关是 ON 但编译参数里查不到
`HAND_CONTROL_CANFD_USE_LIBCANBUS` 宏，说明切换时没清理构建产物，需要清理后重建。
验证方法见「CANFD」章节。

确认运行机上存在库文件：

```bash
ls -l /usr/local/lib/libcanbus.so
ldconfig -p | grep libusb-1.0
```

库不在标准位置时用环境变量指定：

```bash
export HAND_CONTROL_LIBCANBUS_PATH=/path/to/libcanbus.so
ros2 launch hand_control_service service.launch.py
```

该后端不经过内核 SocketCAN，因此不需要 `ip link set up can0`，也不需要给可执行
文件设置 `cap_net_raw` 权限。

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

话题存在但频率明显低于配置的 `tactile_publish_rate_hz`，说明总线带宽不够，需要降频：

```bash
ros2 topic hz /hand_control/hand/tactile_state
```

### 节点启动即崩溃，提示参数类型不匹配

启动日志出现：

```text
terminate called after throwing an instance of 'rclcpp::exceptions::InvalidParameterTypeException'
  what():  parameter 'tactile_publish_rate_hz' has invalid type ...
  ... is of type {double}, setting it to {integer} is not allowed
```

频率类参数在代码里声明为 `double`，**必须写成小数形式**。写成整数时节点会在启动阶段
直接抛异常退出：

```yaml
tactile_publish_rate_hz: 50.0    # 正确
tactile_publish_rate_hz: 50      # 错误，节点无法启动
```

`tactile_publish_rate_hz` 和 `realtime_state_publish_rate_hz` 都是这个规则，
命令行覆盖时同样要带小数点（`-p tactile_publish_rate_hz:=50.0`）。

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
