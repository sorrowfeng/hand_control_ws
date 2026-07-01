#!/usr/bin/env python3

import math

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import JointState

from hand_control_interfaces.msg import HandJointState


class HandStatePublisher(Node):
    def __init__(self):
        super().__init__('hand_state_publisher')

        self.declare_parameter('state_topic', '/hand_control/realtime_state')
        self.declare_parameter('joint_state_publish_rate_hz', 100.0)

        self.state_topic = str(self.get_parameter('state_topic').value)
        publish_rate_hz = max(
            1.0, float(self.get_parameter('joint_state_publish_rate_hz').value)
        )

        self.joint_pub = self.create_publisher(JointState, '/joint_states', 10)
        self.state_sub = self.create_subscription(
            HandJointState, self.state_topic, self.state_callback, 10
        )

        self.joint_names = [
            'finger11', 'finger12', 'finger13', 'finger14',
            'finger21', 'finger22', 'finger23',
            'finger31', 'finger32', 'finger33',
            'finger41', 'finger42', 'finger43',
            'finger51', 'finger52', 'finger53',
        ]
        self.joint_positions = [0.0] * len(self.joint_names)
        self.has_state = False
        self.map_index = {
            1: 'finger11',
            2: 'finger12',
            3: 'finger21',
            4: 'finger31',
            5: 'finger41',
            6: 'finger51',
        }
        self.joint_relations = {
            'finger13': ('finger12', 1),
            'finger22': ('finger21', 1),
            'finger32': ('finger31', 1),
            'finger42': ('finger41', 1),
            'finger52': ('finger51', 1),
        }

        self.publish_timer = self.create_timer(
            1.0 / publish_rate_hz, self.publish_joint_state
        )

        self.get_logger().info(
            f"订阅实时状态话题: {self.state_topic}, /joint_states 发布频率: "
            f"{publish_rate_hz:.1f} Hz"
        )

    def state_callback(self, msg):
        for joint_id, angle_deg in zip(msg.joint_ids, msg.angles):
            joint_name = self.map_index.get(int(joint_id))
            if joint_name is None:
                continue
            self.update_joint_angle(joint_name, math.radians(float(angle_deg)))
        self.has_state = True

    def update_joint_angle(self, joint_name, angle_rad):
        idx = self.joint_names.index(joint_name)
        self.joint_positions[idx] = angle_rad

        for dep_joint, (src_name, ratio) in self.joint_relations.items():
            if src_name == joint_name:
                dep_idx = self.joint_names.index(dep_joint)
                self.joint_positions[dep_idx] = angle_rad * ratio

    def publish_joint_state(self):
        if not self.has_state:
            return

        msg = JointState()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.name = self.joint_names
        msg.position = list(self.joint_positions)
        msg.velocity = []
        msg.effort = []

        self.joint_pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = HandStatePublisher()

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
