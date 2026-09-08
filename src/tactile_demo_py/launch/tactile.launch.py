from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='tactile_demo_py',
            executable='tactile_checker',
            name='tactile_checker',
            namespace='/hand_control',
            output='screen',
            emulate_tty=True
        )
    ])
