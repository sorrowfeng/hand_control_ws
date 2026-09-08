import time

import rclpy
from rclpy.node import Node

from hand_control_interfaces.msg import TactileState
from hand_control_interfaces.srv import (
    GetFingerSensorPos,
    SetFingerPressureReset,
    SetSensorEnable,
)

# 与 TactileSite.msg 中的通道字段一一对应
CHANNELS = (
    'pressure',
    'normal_force',
    'tangential_force',
    'force_direction',
    'proximity',
)


class TactileChecker(Node):
    """触觉传感器自检节点。

    流程：使能传感器 -> 设定压力零点 -> 采集若干帧 -> 输出统计报告。
    """

    def __init__(self):
        super().__init__('tactile_checker')

        self.declare_parameter('hand_name', 'hand')
        self.declare_parameter('sample_count', 20)
        self.declare_parameter('timeout_sec', 10.0)

        self.hand_name = self.get_parameter('hand_name').value
        self.sample_count = int(self.get_parameter('sample_count').value)
        self.timeout_sec = float(self.get_parameter('timeout_sec').value)

        # 服务名带手名前缀，这样无论是否开启兼容模式都能找到
        self.enable_cli = self.create_client(
            SetSensorEnable, f'{self.hand_name}/set_sensor_enable')
        self.reset_cli = self.create_client(
            SetFingerPressureReset,
            f'{self.hand_name}/set_finger_pressure_reset')
        self.pos_cli = self.create_client(
            GetFingerSensorPos, f'{self.hand_name}/get_finger_sensor_pos')

        self.subscription = self.create_subscription(
            TactileState,
            f'{self.hand_name}/tactile_state',
            self._on_state,
            50,
        )

        self.samples = []
        self.first_stamp = None
        self.last_stamp = None

    # ---------- 服务准备 ----------

    def _wait(self, cli, name):
        while not cli.wait_for_service(timeout_sec=1.0):
            self.get_logger().info(f'等待 {name} 服务上线...')
        return True

    def prepare(self):
        self._wait(self.enable_cli, 'set_sensor_enable')
        self._wait(self.reset_cli, 'set_finger_pressure_reset')
        self._wait(self.pos_cli, 'get_finger_sensor_pos')

        self._call_enable(1)
        self._call_reset()

    def _call_enable(self, enable):
        req = SetSensorEnable.Request()
        req.enable = enable
        future = self.enable_cli.call_async(req)
        rclpy.spin_until_future_complete(self, future)
        result = future.result()
        if result is not None and result.result == 0:
            self.get_logger().info('传感器已使能')
        else:
            code = None if result is None else result.result
            self.get_logger().error(f'使能传感器失败 (result={code})')

    def _call_reset(self):
        req = SetFingerPressureReset.Request()
        future = self.reset_cli.call_async(req)
        rclpy.spin_until_future_complete(self, future)
        result = future.result()
        if result is not None and result.result == 0:
            self.get_logger().info('压力零点已设定')
        else:
            code = None if result is None else result.result
            self.get_logger().error(f'设定压力零点失败 (result={code})，'
                                    'pressure 读数可能不是绝对值')

    # ---------- 采集 ----------

    def _on_state(self, msg):
        self.samples.append(msg)
        stamp = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9
        if self.first_stamp is None:
            self.first_stamp = stamp
        self.last_stamp = stamp

    def collect(self):
        deadline = time.monotonic() + self.timeout_sec
        while (
            len(self.samples) < self.sample_count
            and time.monotonic() < deadline
            and rclpy.ok()
        ):
            rclpy.spin_once(self, timeout_sec=0.1)

    # ---------- 报告 ----------

    def _query_sensor_pos(self, sensor_id):
        req = GetFingerSensorPos.Request()
        req.sensor_id = sensor_id
        future = self.pos_cli.call_async(req)
        rclpy.spin_until_future_complete(self, future, timeout_sec=2.0)
        result = future.result()
        if result is None:
            return None
        return len(result.x), len(result.y)

    def report(self):
        count = len(self.samples)
        self.get_logger().info('=' * 58)
        self.get_logger().info(f'手: {self.hand_name}   样本数: {count}')

        if count == 0:
            self.get_logger().error(
                f'{self.timeout_sec} 秒内没有收到 {self.hand_name}/tactile_state')
            self.get_logger().error('检查服务端是否开启 tactile_enabled，'
                                    '以及该手型是否带触觉传感器')
            self.get_logger().info('=' * 58)
            return

        duration = self.last_stamp - self.first_stamp
        freq = (count - 1) / duration if duration > 0 and count > 1 else 0.0
        self.get_logger().info(f'实测频率: {freq:.1f} Hz')

        # 以最后一帧为准统计维度
        latest = self.samples[-1]
        active = [site for site in latest.sites if site.cell_count > 0]
        self.get_logger().info(
            f'位点: 共 {len(latest.sites)} 个，其中 {len(active)} 个有传感单元')

        for site in latest.sites:
            if site.cell_count <= 0:
                continue
            parts = [f'cell_count={site.cell_count}']
            for channel in CHANNELS:
                values = list(getattr(site, channel))
                if values:
                    parts.append(f'{channel}={len(values)}[max {max(values):.3f}]')
                else:
                    parts.append(f'{channel}=空')
            self.get_logger().info(
                f'  sensor_id={site.sensor_id:<3} ' + '  '.join(parts))

        # 顺带验证静态坐标服务
        if active:
            probe = active[0].sensor_id
            pos = self._query_sensor_pos(probe)
            if pos is None:
                self.get_logger().warn(
                    f'get_finger_sensor_pos({probe}) 调用失败')
            else:
                self.get_logger().info(
                    f'get_finger_sensor_pos({probe}) -> {pos[0]} 个坐标点')

        empty_channels = []
        for channel in CHANNELS:
            if not any(list(getattr(site, channel)) for site in active):
                empty_channels.append(channel)
        if empty_channels:
            self.get_logger().warn(
                '以下通道全程无数据: ' + ', '.join(empty_channels))

        self.get_logger().info('=' * 58)


def main(args=None):
    rclpy.init(args=args)
    node = TactileChecker()
    try:
        node.prepare()
        node.get_logger().info(
            f'开始采集，目标 {node.sample_count} 帧，'
            f'超时 {node.timeout_sec} 秒...')
        node.collect()
        node.report()
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
