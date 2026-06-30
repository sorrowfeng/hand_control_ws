from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():
    default_params_file = os.path.join(
        get_package_share_directory('lhandpro_service'),
        'config',
        'lhandpro_service.yaml',
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params_file,
            description='Path to the LHandPro service parameter file',
        ),
        Node(
            package='lhandpro_service',         # 包名
            executable='lhandpro_service',  # 可执行文件名
            name='lhandpro_service', # 节点名称
            namespace='/lhandpro_service',      # 设置命名空间
            output='screen',                 # 输出日志到终端
            emulate_tty=True,                 # 更好地显示日志            
            parameters=[LaunchConfiguration('params_file')],
        )
    ])
