# ROS2 Project Guide

This document describes the current project: package layout, runtime flow, configuration, launch commands, validation steps, and common troubleshooting. The ROS-facing names are white-labeled as `hand_control`; model directory names and third-party SDK names are kept as their original resource names.

## Purpose

This workspace provides ROS2 control for hand devices:

- CANFD, RS485, and EtherCAT communication backends.
- ROS2 services for motion control, parameter configuration, and state queries.
- A default 100Hz realtime state topic for visualization and upper-level control.
- Single-hand and multi-hand topologies, including same-bus different IDs and separate-bus same/different IDs.
- RViz visualization by converting realtime state to `/joint_states`.
- C++ and Python sequence demos for service-call validation.

## Layout

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

## Package Roles

| Package | Role |
| --- | --- |
| `hand_control_interfaces` | Defines `HandJointState` and all ROS2 service interfaces. |
| `hand_control_service` | Main control node. Loads bus/hand configuration, initializes the SDK, registers services, and publishes realtime state. |
| `hand_control_description` | Holds model assets, RViz config, and the `hand_state_publisher` node. |
| `sequence_demo_cpp` | C++ service-call sequence demo. |
| `sequence_demo_py` | Python service-call sequence demo. |

## Runtime Flow

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

## Build

After package or interface renaming, clean stale ROS2 build products before rebuilding:

```bash
# Run from the current project root
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build --symlink-install
source install/setup.bash
```

Build only the core packages:

```bash
colcon build --symlink-install --packages-select \
  hand_control_interfaces \
  hand_control_service \
  hand_control_description
```

Verify package discovery:

```bash
ros2 pkg list | grep hand_control
ros2 interface show hand_control_interfaces/msg/HandJointState
```

## Shared Library Path

The service node depends on third-party SDK shared libraries. If runtime loading fails, update `src/ros2_hand_control.conf` to match the installed workspace paths, then run:

```bash
sudo cp src/ros2_hand_control.conf /etc/ld.so.conf.d/
sudo ldconfig
```

For a temporary shell-only setup:

```bash
export LD_LIBRARY_PATH=$PWD/install/hand_control_service/lib:$LD_LIBRARY_PATH
```

## Permissions

The default libcanbus backend requires `libcanbus.so` and its dependencies to be loadable by the system. If libcanbus is disabled and SocketCAN is used, CANFD normally needs raw network capability:

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

RS485 serial access normally requires the current user to be in the `dialout` group:

```bash
sudo usermod -aG dialout $USER
```

Log in again after changing user groups.

## Service Configuration

Default configuration:

```text
src/hand_control_service/config/service.yaml
```

Default launch:

```bash
ros2 launch hand_control_service service.launch.py
```

Launch with an explicit config file:

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/service.yaml
```

Main parameters:

| Parameter | Meaning |
| --- | --- |
| `bus_names` | Bus name list. Each name must exist under `buses`. |
| `hand_names` | Hand name list. Each name must exist under `hands`. |
| `default_hand` | Target hand for compatibility services without a hand-name prefix. |
| `compat_single_hand_services` | Registers `/hand_control/<service>` compatibility services when enabled. |
| `publish_realtime_state` | Enables realtime state topic publishing. |
| `realtime_state_publish_rate_hz` | Realtime state publish rate, default 100Hz. |
| `buses.<name>.type` | `canfd`, `rs485`, or `ethercat`; `ecat` is accepted as an EtherCAT alias. |
| `buses.<name>.channel` | Scanned device index: CAN device, serial port, or network interface. |
| `hands.<name>.bus` | Bus used by this hand. |
| `hands.<name>.address` | CANFD/RS485 node ID, or EtherCAT slave ID. |
| `hands.<name>.hand_type` | SDK hand type enum, such as `LAC_DOF_6`, `LAC_DOF_6_S`, or `LAC_DOF_16`. |

## Communication Backends

### CANFD

Example:

```yaml
buses:
  bus0:
    type: canfd
    channel: 0
    canfd_arb_baudrate: 1000000
    canfd_data_baudrate: 5000000
```

`canfd_arb_baudrate` and `canfd_data_baudrate` are passed directly to the SDK. They are not multiplied by 1000 in the ROS layer.

Linux uses libcanbus by default. Build with `HAND_CONTROL_USE_LIBCANBUS` disabled to use the SocketCAN backend:

```bash
colcon build --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=OFF
```

### RS485

Example:

```yaml
buses:
  bus0:
    type: rs485
    channel: 0
    rs485_baudrate: 500000
```

Linux serial scanning order:

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
fallback /dev/tty*
/dev/ttyS*
```

Virtual console devices such as `tty`, `ttyprintk`, and `tty0` are filtered. Standard `ttyS*` ports are added last as a fallback.

### EtherCAT

Example:

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

`channel` is the scanned network interface index. `address` is the SOEM slave ID.

## Multi-Hand Topologies

Same bus, different IDs:

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/two_hands_same_bus.yaml
```

Separate buses, same or different IDs:

```bash
ros2 launch hand_control_service service.launch.py \
  params_file:=$(pwd)/src/hand_control_service/config/two_hands_two_buses.yaml
```

The service maps `hands.<name>.bus` to a bus and passes `hands.<name>.address` to the SDK. CANFD/RS485 use node IDs; EtherCAT uses slave IDs. The ROS layer does not need additional CAN frame ID routing.

## Service Names

The service node runs under `/hand_control` by default.

Compatibility service:

```text
/hand_control/<service>
```

Named-hand service:

```text
/hand_control/<hand_name>/<service>
```

Examples:

```bash
ros2 service call /hand_control/set_enable \
  hand_control_interfaces/srv/SetEnable "{id: 1, enable: true}"

ros2 service call /hand_control/left/set_enable \
  hand_control_interfaces/srv/SetEnable "{id: 1, enable: true}"
```

Common service groups:

| Group | Services |
| --- | --- |
| Motion control | `set_position`, `set_angle`, `move_motors`, `stop_motors`, `home_motors` |
| Speed/current | `set_position_velocity`, `set_angular_velocity`, `set_max_current`, `set_home_current` |
| Live state | `get_now_position`, `get_now_angle`, `get_now_current`, `get_now_status`, `get_now_alarm` |
| Parameters | `get_position`, `get_angle`, `get_control_mode`, `get_enable`, `get_serial_number` |
| Communication | `set_can_node_id`, `set_canfd_arb_baudrate`, `set_rs485_node_id`, `set_rs485_baudrate` |

List all interfaces and services:

```bash
ros2 interface list | grep hand_control_interfaces
ros2 service list | grep hand_control
```

## Realtime State Topic

The service publishes `hand_control_interfaces/msg/HandJointState`.

Default single-hand topics:

```text
/hand_control/realtime_state
/hand_control/hand/realtime_state
```

Multi-hand topics:

```text
/hand_control/<hand_name>/realtime_state
```

Message fields:

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

Inspect topic output:

```bash
ros2 topic hz /hand_control/realtime_state
ros2 topic echo /hand_control/realtime_state
```

## Model Display

Model selection config:

```text
src/hand_control_description/config/model.yaml
```

Default launch:

```bash
ros2 launch hand_control_description display.launch.py
```

Run without RViz:

```bash
ros2 launch hand_control_description display.launch.py use_rviz:=false
```

Select a model directory:

```bash
ros2 launch hand_control_description display.launch.py model_id:=<model_directory_name>
```

Use a full URDF or xacro path:

```bash
ros2 launch hand_control_description display.launch.py model:=/absolute/path/to/model.urdf
```

Display flow:

```text
hand_control_service -> /hand_control/realtime_state
hand_state_publisher -> /joint_states
robot_state_publisher -> /tf
rviz2 -> RobotModel
```

The current display launch targets one hand model. The service can control multiple hands, but displaying multiple hands at once requires distinct frames, joint names, or namespaces before extending the display node.

## Sequence Demos

Python demo:

```bash
ros2 launch sequence_demo_py sequence.launch.py
```

C++ demo:

```bash
ros2 launch sequence_demo_cpp sequence.launch.py
```

Both demos call compatibility services under `/hand_control`, so they target the default single-hand setup or the configured `default_hand` in a multi-hand setup.

## Recommended Startup Order

Terminal 1: start the service.

```bash
source install/setup.bash
ros2 launch hand_control_service service.launch.py
```

Wait for initialization, then verify realtime state:

```bash
ros2 topic hz /hand_control/realtime_state
```

Terminal 2: start visualization.

```bash
source install/setup.bash
ros2 launch hand_control_description display.launch.py
```

Terminal 3: run a sequence demo or manual service calls.

```bash
source install/setup.bash
ros2 launch sequence_demo_py sequence.launch.py
```

## Shutdown And Duplicate Process Checks

If RViz jumps between old and current positions after `Ctrl+C`, first check for stale nodes or duplicate publishers:

```bash
ros2 node list
ros2 topic info -v /joint_states
ros2 topic info -v /hand_control/realtime_state
```

In one visualization pipeline, `/joint_states` should have only one active publisher. Stop stale `hand_state_publisher`, `robot_state_publisher`, or RViz-related processes before restarting.

## Troubleshooting

### Services are missing

Check namespace and node startup:

```bash
ros2 service list | grep hand_control
ros2 node list | grep hand_control
```

### Realtime topic has data but RViz does not move

Check `/joint_states`:

```bash
ros2 topic echo /joint_states
ros2 topic info -v /joint_states
```

Then verify that model joint names match the names published by `hand_state_publisher`.

### RViz jumps between old and new positions

The usual cause is multiple `/joint_states` publishers, or a stale launch process that did not exit fully. Use `ros2 topic info -v /joint_states` to identify publishers and stop duplicates.

### CANFD startup fails

Check:

- The CAN device is detected.
- `channel` points to the correct scanned device index.
- For the default libcanbus backend, `libcanbus.so` and its dependencies are loadable; for SocketCAN, network capabilities are available.
- Baudrate configuration matches the device.

### RS485 device is not found

Check:

- The serial port appears under `/dev/ttyXR*`, `/dev/ttyUSB*`, `/dev/ttyACM*`, or another scanned pattern.
- The current user has read/write permission.
- `channel` points to the correct scanned port index.
- `rs485_baudrate` is supported by system termios.

### SDK library is not found

Check:

```bash
ls src/hand_control_service/thirdparty/include/
ls src/hand_control_service/thirdparty/lib/
```

If using system libraries, verify that `ldconfig -p` can find them.

## Maintenance Notes

- Rebuild `hand_control_interfaces` and dependent packages after changing `.srv` or `.msg` files.
- Use `--symlink-install` while editing model assets and RViz-related files.
- Clean `build/`, `install/`, and `log/` after package renaming.
- Do not run multiple service or display nodes with the same namespace.
- Multi-hand routing comes from YAML topology, not hard-coded CAN frame IDs in ROS code.
