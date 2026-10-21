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

- `hand_control_interfaces`: service/message interfaces (49 `.srv` plus the
  `HandJointState`, `TactileState`, and `TactileSite` messages).
- `hand_control_service`: hardware communication and service node.
- `hand_control_description`: model loading, robot state publishing, RViz.
- `sequence_demo_cpp`: C++ motion demo.
- `sequence_demo_py`: Python motion demo.
- `tactile_demo_cpp`: C++ tactile sensor self-check demo (`tactile_checker`).
- `tactile_demo_py`: Python tactile sensor self-check demo (`tactile_checker`).

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

The repository default for CANFD is gsusb-canfd (userspace libusb `gs_usb`),
which works on Windows, Linux, and macOS:

```cmake
HAND_CONTROL_USE_GSUSB_CANFD=ON
```

The vendored library lives at
`src/hand_control_service/thirdparty/gsusb-canfd` and is built as a static
library via `add_subdirectory`; link it through the `canfd` target. It requires
`libusb-1.0` development files (`sudo apt install libusb-1.0-0-dev` on Debian /
Ubuntu, `brew install libusb` on macOS).

Optional Linux backend (mutually exclusive with the gsusb default):

- SocketCAN: `-DHAND_CONTROL_USE_SOCKETCAN=ON`

The checked-in `LHandProLib` binaries are Linux aarch64; `LumiDex11SLib` is still
x86-64. Build on a matching target architecture or replace the SDK binaries first.

After changing a backend option on an existing build tree, clear at least
`build/hand_control_service` and `install/hand_control_service`, or clear
`build/ install/ log/` for a full clean rebuild. Verify the remote build with:

```bash
grep -n "HAND_CONTROL_USE_GSUSB_CANFD\|HAND_CONTROL_USE_SOCKETCAN" \
  build/hand_control_service/CMakeCache.txt
grep -R --line-number -- "-DHAND_CONTROL_CANFD_USE" \
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

- Repository default: gsusb-canfd (userspace libusb `gs_usb`), built from
  `src/hand_control_service/thirdparty/gsusb-canfd`.
- Optional Linux backend: SocketCAN (`-DHAND_CONTROL_USE_SOCKETCAN=ON`),
  mutually exclusive with the gsusb default.
- Runtime macros selected by CMake: `HAND_CONTROL_CANFD_USE_GSUSB` or
  `HAND_CONTROL_CANFD_USE_SOCKETCAN`.

RS485:

- Serial scanning should prioritize `/dev/ttyXR*`.
- Other common serial patterns remain supported after that.
- `SerialPort` is the physical serial wrapper; multi-node buses use the SDK's
  `RS485SharedBus` for framing, CRC, scheduling, and node dispatch.
- Configure the shared-bus transaction interval from the baudrate (30 ms for
  500000 and above, larger for slower rates).

EtherCAT:

- The service registers `set_ecat_sdo_callback_ex` so the SDK can use the
  SOEM CoE mailbox during `initial()` for hand type/side/firmware/SN detection.

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

Current model assets under `src/hand_control_description/models/` (eight
folders, two product lines, left and right hands):

- `DH116-R000-A1`, `DH116-L000-A1`
- `DH116S-R000-A1`, `DH116S-L000-A1`
- `Lumi-Dex-11-R000-A1`, `Lumi-Dex-11-L000-A1`
- `Lumi-Dex-11S-R000-A1`, `Lumi-Dex-11S-L000-A1`

Each folder carries its own `urdf/`, `mjcf/`, `meshes/`, `config/`, and
`package.xml`, so models are self-contained. `config/model.yaml` currently
selects `DH116S-L000-A1`, which is also the model used for real-hardware
testing. Vendors receive a subset of these folders chosen per vendor in
`packaging/vendors.json`.

## Documentation

- `README.md` is the complete user-facing document. `docs/` holds two condensed
  versions (Chinese and English) of the same content. When public behavior
  changes, update all three together; they have drifted apart in the past.
- `docs/项目维护笔记.md` carries maintainer knowledge that does not belong in
  the user docs: SDK interface coverage gaps, local build pitfalls, packaging
  and release conventions.
- `docs/` ships inside vendor packages, so it must not name specific vendors or
  SDK libraries — packaging runs a per-vendor forbidden-string scan over it.
  `AGENTS.md`, `packaging/`, and `scripts/package_vendor.py` are excluded from
  packages, so internal detail is fine here.

## Deployment Checklist

Use the new remote path:

```bash
cd /home/plf/Project/RosProject/hand_control_ws
git fetch origin main
git pull --ff-only origin main
source /opt/ros/humble/setup.bash
colcon build --symlink-install
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
- Commit messages are written in Chinese: a `type: 主题` subject line
  (`feat`, `docs`, `chore`) followed by one `-m` paragraph per category
  (功能 / 文档 / 工程).
- Pushing to GitHub has been unreliable in both directions: the machine
  configures a local HTTP proxy (`127.0.0.1:7890`), and depending on the day
  either the proxied route fails with a TLS error or the direct route resets.
  Try the other route and a larger `http.postBuffer` before blaming
  credentials, e.g.
  `git -c http.proxy= -c https.proxy= -c http.postBuffer=524288000 push origin main`.
- Fetch before pushing: the remote may have advanced from another machine even
  when the local branch looked ahead.
- Avoid modifying `thirdparty` SDK sources unless the task explicitly asks for
  SDK integration work.
- Be careful with generated build artifacts; do not commit `build/`, `install/`,
  or `log/`.
