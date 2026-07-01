# AGENTS.md

This file captures project-specific context for future agent sessions. Treat it
as operational guidance, not product documentation.

## Project Identity

- Repository: `https://github.com/sorrowfeng/hand_control_ws.git`
- Default branch: `main`
- Local workspace path: `D:\Project\RosProject\hand_control_ws`
- Previous local workspace path `D:\Project\RosProject\lhandpro_ws` may still
  exist if a terminal/editor keeps it locked. Use `hand_control_ws`.
- Remote deployment host: `plf@192.168.213.128`
- Remote workspace path: `/home/plf/Project/RosProject/hand_control_ws`
- Do not commit SSH passwords or other secrets into this file.

The project has been white-labeled around the `hand_control` name. Avoid
reintroducing old project/package names. Exceptions that intentionally remain:

- SDK/vendor directory and library names under `thirdparty`, such as
  `LHandProLib`.
- Model folder names under the model assets, such as specific DH model names.
- SDK configuration defaults in CMake, which are intentionally configurable.

## Package Layout

ROS 2 Humble workspace packages:

- `hand_control_interfaces`: service/message interfaces.
- `hand_control_service`: hardware communication and service node.
- `hand_control_description`: model loading, robot state publishing, RViz.
- `sequence_demo_cpp`: C++ motion demo.
- `sequence_demo_py`: Python motion demo.

Important launch/config files:

- Service launch: `src/hand_control_service/launch/service.launch.py`
- Description launch: `src/hand_control_description/launch/display.launch.py`
- Main service config: `src/hand_control_service/config/service.yaml`
- Multi-hand examples:
  - `src/hand_control_service/config/two_hands_same_bus.yaml`
  - `src/hand_control_service/config/two_hands_two_buses.yaml`

## Build Rules

Normal local build:

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install
```

The repository default for CANFD on Linux is SocketCAN:

```cmake
HAND_CONTROL_USE_LIBCANBUS=OFF
```

The current SSH deployment environment should use libcanbus explicitly:

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=ON
```

After changing this option on an existing build tree, clear at least
`build/hand_control_service` and `install/hand_control_service`, or clear
`build/ install/ log/` for a full clean rebuild. Verify the remote build with:

```bash
grep -n "HAND_CONTROL_USE_LIBCANBUS" build/hand_control_service/CMakeCache.txt
grep -R --line-number -- "-DUSE_LIBCANBUS" \
  build/hand_control_service/CMakeFiles/hand_control_service.dir
```

## SDK Adapter Conventions

`hand_control_service` uses a small SDK adapter layer:

- Generated include template:
  `src/hand_control_service/cmake/sdk_adapter.hpp.in`
- CMake cache variables:
  - `HC_SDK_LIB_NAME`
  - `HC_SDK_HEADER`
  - `HC_SDK_NS`
  - `HC_SDK_CLASS`

Only two short C++ compile macros should be used in service wrapper code:

- `HC_SDK_NS`
- `HC_SDK_CLASS`

Do not add broad macro wrappers for every SDK enum or constant. Use direct
qualified references such as `HC_SDK_NS::LER_NONE` and
`HC_SDK_NS::LAC_DOF_6`.

## Communication Support

The service supports CANFD, RS485, and EtherCAT.

Topology expectations:

- One bus can control multiple hands/devices with different IDs.
- Multiple buses can control hands with same or different IDs.
- EtherCAT follows the same idea using slave indices instead of CAN/RS485 IDs.
- Do not add custom route math like `0x480/0x580/0x7000 + node_id`; metadata
  should be passed to the SDK and the SDK handles routing.

CANFD:

- Linux default in the repository: SocketCAN.
- Remote hardware environment: build with `HAND_CONTROL_USE_LIBCANBUS=ON`.
- Runtime macro selected by CMake: `USE_LIBCANBUS`.

RS485:

- Serial scanning should prioritize `/dev/ttyXR*`.
- Other common serial patterns remain supported after that.

## Realtime State

Realtime states such as angle, position, velocity, current, status, and alarm
are published through topics for smoother model display. Service APIs still
exist for request/response operations, but the model side should not poll
services for realtime joint rendering.

Default realtime publish rate is 100 Hz.

## Model Handling

`hand_control_description` is designed so the active model can be changed from
configuration instead of editing every URDF path. The copied model folder names
may remain vendor/model-specific; that is an explicit exception to the
white-label naming rule.

Current known model assets include:

- `DH116S-R000-A1`
- `DH116S-L000-A1`
- `DH116-L000-A1`
- `DH116-R000-A1`

The real hardware currently used in testing was `DH116S-L000-A1`.

## Deployment Checklist

Use the new remote path:

```bash
cd /home/plf/Project/RosProject/hand_control_ws
git fetch origin main
git pull --ff-only origin main
source /opt/ros/humble/setup.bash
colcon build --symlink-install --cmake-args -DHAND_CONTROL_USE_LIBCANBUS=ON
```

Useful runtime commands:

```bash
source install/setup.bash
ros2 launch hand_control_service service.launch.py
ros2 launch hand_control_description display.launch.py
```

For topic checks:

```bash
ros2 topic list
ros2 topic echo <topic_name>
ros2 topic info -v /joint_states
```

If RViz/model joints jump between two positions, first check for duplicate
`/joint_states` publishers or stale launch processes that did not exit cleanly.

## Git Notes

- `main` is the only intended active branch.
- Push to `origin/main`.
- Avoid modifying `thirdparty` SDK sources unless the task explicitly asks for
  SDK integration work.
- Be careful with generated build artifacts; do not commit `build/`, `install/`,
  or `log/`.
