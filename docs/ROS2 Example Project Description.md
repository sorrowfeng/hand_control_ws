# ROS2 Project Guide

This document describes the current project: package layout, runtime flow, configuration, launch commands, validation steps, and common troubleshooting. The ROS-facing names are white-labeled as `hand_control`; model directory names and third-party SDK names are kept as their original resource names.

## Purpose

This workspace provides ROS2 control for hand devices:

- CANFD, RS485, and EtherCAT communication backends.
- ROS2 services for motion control, parameter configuration, and state queries.
- A default 100Hz realtime state topic for visualization and upper-level control.
- An optional tactile topic covering 11 sites: pressure, normal force, tangential force, force direction, and proximity (disabled by default).
- Single-hand and multi-hand topologies, including same-bus different IDs and separate-bus same/different IDs.
- RViz visualization by converting realtime state to `/joint_states`.
- C++ and Python sequence demos for service-call validation, plus C++ and Python tactile self-check demos.

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
    tactile_demo_cpp/
    tactile_demo_py/
    ros2_hand_control.conf
```

## Package Roles

| Package | Role |
| --- | --- |
| `hand_control_interfaces` | Defines `HandJointState`, `TactileState`, and all ROS2 service interfaces. |
| `hand_control_service` | Main control node. Loads bus/hand configuration, initializes the SDK, registers services, and publishes realtime state and tactile topics. |
| `hand_control_description` | Holds model assets, RViz config, and the `hand_state_publisher` node. |
| `sequence_demo_cpp` | C++ service-call sequence demo. |
| `sequence_demo_py` | Python service-call sequence demo. |
| `tactile_demo_cpp` | C++ tactile self-check demo. |
| `tactile_demo_py` | Python tactile self-check demo. |

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

> **Clean and rebuild after changing interface definitions in `hand_control_interfaces`,
> especially when deleting one.** Incremental builds do not remove stale typesupport
> libraries under `install/hand_control_interfaces`, and they still reference deleted
> symbols, producing `undefined symbol: ...__convert_from_py` at runtime.
> A full `rm -rf build install log` is the safe option.

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

SocketCAN CANFD normally needs raw network capability:

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
| `hands.<name>.tactile_enabled` | Enables tactile sampling and publishing, default `false`. See "Tactile Topic And Services". |
| `tactile_publish_rate_hz` | Tactile publish rate, default 50Hz, shared by all enabled hands. |

> Rate parameters are declared as `double`, so they **must be written in decimal form**
> (`50.0`). Writing `50` makes the node throw `InvalidParameterTypeException` during
> startup and exit immediately. The same applies to `realtime_state_publish_rate_hz`.

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

The service applies the configured CANFD baudrates at startup.

Linux offers two CANFD backends, **SocketCAN by default**. They are mutually exclusive
and selected at build time; you cannot switch at runtime.

| Backend | Build flag | Use case |
| --- | --- | --- |
| SocketCAN | default (OFF) | Kernel SocketCAN interfaces such as `can0` / `can1` |
| libcanbus | `-DHAND_CONTROL_USE_LIBCANBUS=ON` | Vendor USB-CANFD adapters that bypass kernel SocketCAN |

#### SocketCAN backend (default)

It scans `/sys/class/net` for interfaces starting with `can`; `channel: 0` is the first
match. The executable needs network capability:

```bash
sudo setcap cap_net_raw,cap_net_admin+ep \
  install/hand_control_service/lib/hand_control_service/hand_control_service
```

#### Switching to libcanbus

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select hand_control_service \
  --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=ON
```

> **Clean and rebuild after switching.** The flag is written into `CMakeCache.txt` and
> generates the `USE_LIBCANBUS` compile definition. Changing it on an existing build tree
> may not regenerate the definition, leaving you with "flag is ON but the macro is not
> active". At least remove `build/hand_control_service` and `install/hand_control_service`.

libcanbus is loaded at runtime via `dlopen` and is not linked, so the build machine does
not need it but **the runtime machine does**. Lookup order:

1. Full path from environment variable `HAND_CONTROL_LIBCANBUS_PATH`
2. `libcanbus.so` through the system loader search path
3. `/usr/local/lib/libcanbus.so`

It also depends on `libusb-1.0.so`. Since it bypasses kernel SocketCAN, it needs neither
`ip link set up can0` nor the `cap_net_raw` capability.

#### Checking which backend is active

```bash
grep -n "HAND_CONTROL_USE_LIBCANBUS" build/hand_control_service/CMakeCache.txt
grep -R --line-number -- "-DUSE_LIBCANBUS" \
  build/hand_control_service/CMakeFiles/hand_control_service.dir
```

Output from the second command means the libcanbus backend is compiled in; it also
catches the "flag changed but build tree not cleaned" case.

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

## Tactile Topic And Services

Tactile sampling is disabled by default. After enabling `tactile_enabled` for a hand,
the service also publishes `hand_control_interfaces/msg/TactileState`:

```text
/hand_control/<hand_name>/tactile_state
```

Tactile is a new capability, so it **does not** get an unprefixed compatibility copy;
only the hand-name-prefixed topic exists.

### Enabling

```yaml
# Top-level: publish rate, only effective when at least one hand enables tactile
tactile_publish_rate_hz: 50.0

hands:
  hand:
    tactile_enabled: true
```

| Parameter | Scope | Default | Meaning |
| --- | --- | --- | --- |
| `tactile_enabled` | `hands.<name>` | `false` | Enables tactile sampling and publishing. Keep it off for hands without sensors. |
| `tactile_publish_rate_hz` | top level | `50.0` | Publish rate shared by all enabled hands. Must be written in decimal form. |

Every cycle walks all 11 sites and all 5 channels. There are no extra optional
configuration items.

### Sampling Cost

One publish = 11 sites (5 fingertips + 5 finger pads + palm) x 5 channels. Each site's
cell count `cell_count` is probed once on the first sample and cached, and invalidated on
reconnect or re-init, so steady state is 55 reads rather than double that from the
two-phase calls.

Lower `tactile_publish_rate_hz` first if the bus cannot keep up.

### Message Layout

Each element of `TactileState.sites[]` is one site; `sensor_id` is the SDK `LSS_*` value:

| Field | Unit |
| --- | --- |
| `pressure` | Normalized [0,1], delta against the zero baseline |
| `normal_force` | N |
| `tangential_force` | N |
| `force_direction` | deg, [0,360] |
| `proximity` | Normalized [0,1] |

Every array length equals that site's `cell_count`. A failed channel read yields an empty
array, so `empty()` distinguishes "read failed" from "all zeros"; `cell_count` of 0 means
the site is unavailable.

### Related Services

All of them carry the hand-name prefix like other services:

| Service | Request | Meaning |
| --- | --- | --- |
| `set_sensor_enable` | `int32 enable` | Enable (1) or disable (0) sensor monitoring. |
| `set_finger_pressure_reset` | none | Set the current reading as the zero baseline. |
| `get_finger_sensor_pos` | `int32 sensor_id` | Query cell distribution coordinates, normalized to [0,1]. |

When `tactile_enabled` is on, the service node enables the sensor once during
initialization, so calling `set_sensor_enable` manually is usually unnecessary.

### Typical Usage

`pressure` is a delta against the baseline, so set the zero point before reading
absolute pressure:

```bash
ros2 service call /hand_control/hand/set_finger_pressure_reset \
  hand_control_interfaces/srv/SetFingerPressureReset
ros2 topic echo /hand_control/hand/tactile_state
ros2 topic hz /hand_control/hand/tactile_state
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

## Tactile Self-Check Demos

`tactile_demo_py` and `tactile_demo_cpp` verify that the tactile path works. The flow is:
enable sensor -> set pressure zero baseline -> collect frames -> print a report.

Make sure `tactile_enabled` is on for the target hand, then:

```bash
ros2 launch tactile_demo_py tactile.launch.py
# or
ros2 launch tactile_demo_cpp tactile.launch.py
```

| Parameter | Default | Meaning |
| --- | --- | --- |
| `hand_name` | `hand` | Target hand; decides the topic and service prefix. |
| `sample_count` | `20` | Frames to collect before printing the report. |
| `timeout_sec` | `10.0` | Collection timeout; the report uses whatever arrived. |

The report covers the measured publish rate, each site's `cell_count` with per-channel
array lengths and maxima, and the size of one `get_finger_sensor_pos` response. Channels
with no data at all raise a dedicated warning.

Unlike the sequence demos, these nodes use **hand-name-prefixed** service names (for
example `hand/set_sensor_enable`), so they resolve services whether or not compatibility
mode is enabled.

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

The following applies to the default SocketCAN backend. See the next section for libcanbus.

Check:

- The CAN device is detected (`ip link show` lists a `can*` interface).
- `channel` points to the correct scanned device index.
- SocketCAN capability is set: `getcap install/hand_control_service/lib/hand_control_service/hand_control_service`.
- Baudrate configuration matches the device.

### libcanbus backend fails to load

Startup logs `Failed to load libcanbus.so` or `Failed to load libusb-1.0.so`, meaning the
build uses the libcanbus backend but the library is missing at runtime.

First confirm the build actually switched. If the flag is ON but `USE_LIBCANBUS` is absent
from the compile flags, the build tree was not cleaned after switching:

```bash
grep -n "HAND_CONTROL_USE_LIBCANBUS" build/hand_control_service/CMakeCache.txt
grep -R --line-number -- "-DUSE_LIBCANBUS" \
  build/hand_control_service/CMakeFiles/hand_control_service.dir
```

Confirm the library exists on the runtime machine:

```bash
ls -l /usr/local/lib/libcanbus.so
ldconfig -p | grep libusb-1.0
```

For a non-standard location:

```bash
export HAND_CONTROL_LIBCANBUS_PATH=/path/to/libcanbus.so
ros2 launch hand_control_service service.launch.py
```

### Tactile topic has no data

Confirm the hand enables tactile and the log does not say no hand enabled it:

```bash
ros2 param get /hand_control_service hands.hand.tactile_enabled
ros2 topic list | grep tactile
```

If the topic exists but every `cell_count` is 0, the SDK found no sensing cells. Common
causes:

- The hand model has no tactile sensor.
- The sensor is not enabled; call `set_sensor_enable` once and retry.
- The hand is not initialized (bus not connected); other services on the same hand also
  return `-1` in that case.

If the topic exists but the rate is far below `tactile_publish_rate_hz`, the bus cannot
keep up; lower the rate.

### Node crashes on startup with a parameter type error

Startup log shows:

```text
terminate called after throwing an instance of 'rclcpp::exceptions::InvalidParameterTypeException'
  what():  parameter 'tactile_publish_rate_hz' has invalid type ...
  ... is of type {double}, setting it to {integer} is not allowed
```

Rate parameters are declared as `double`, so they **must use decimal form**:

```yaml
tactile_publish_rate_hz: 50.0    # correct
tactile_publish_rate_hz: 50      # wrong, node cannot start
```

The same rule applies to `realtime_state_publish_rate_hz`, including command-line
overrides (`-p tactile_publish_rate_hz:=50.0`).

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
- Realtime state and tactile data both use topics; do not poll high-frequency data with services.
- Clean the build tree after deleting `.srv` / `.msg` files, otherwise stale typesupport libraries reference removed symbols.
