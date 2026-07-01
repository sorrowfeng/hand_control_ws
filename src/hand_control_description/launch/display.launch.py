import os

import launch
import launch_ros
from ament_index_python.packages import get_package_share_directory
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
import launch_ros.parameter_descriptions


PACKAGE_NAME = 'hand_control_description'


def _load_simple_yaml(path):
    values = {}
    if not os.path.exists(path):
        return values

    with open(path, 'r', encoding='utf-8') as config_file:
        for raw_line in config_file:
            line = raw_line.split('#', 1)[0].strip()
            if not line or ':' not in line:
                continue

            key, value = line.split(':', 1)
            values[key.strip()] = value.strip().strip('"\'')

    return values


def _find_urdf(package_path, model_id, urdf_file):
    if urdf_file:
        candidate = os.path.join(package_path, 'models', model_id, urdf_file)
        if os.path.exists(candidate):
            return candidate
        raise RuntimeError(f'URDF file does not exist: {candidate}')

    model_path = os.path.join(package_path, 'models', model_id)
    default_urdf = os.path.join(model_path, 'urdf', f'{model_id}.urdf')
    if os.path.exists(default_urdf):
        return default_urdf

    urdf_dir = os.path.join(model_path, 'urdf')
    for suffix in ('.urdf', '.xacro'):
        if os.path.isdir(urdf_dir):
            for filename in sorted(os.listdir(urdf_dir)):
                if filename.endswith(suffix):
                    return os.path.join(urdf_dir, filename)

    raise RuntimeError(f'No URDF or xacro file found for model: {model_id}')


def _launch_setup(context, *args, **kwargs):
    urdf_package_path = get_package_share_directory(PACKAGE_NAME)
    default_rviz_config_path = os.path.join(urdf_package_path, 'config', 'display_robot_model.rviz')

    model = LaunchConfiguration('model').perform(context).strip()
    model_id = LaunchConfiguration('model_id').perform(context).strip()
    urdf_file = LaunchConfiguration('urdf_file').perform(context).strip()
    model_path = model if model else _find_urdf(urdf_package_path, model_id, urdf_file)

    substitutions_command_result = launch.substitutions.Command(['xacro ', model_path])
    robot_description_value = launch_ros.parameter_descriptions.ParameterValue(substitutions_command_result, value_type=str)

    action_robot_state_publisher = launch_ros.actions.Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{
            'robot_description': robot_description_value,
            'publish_frequency': 100.0,
            'ignore_timestamp': True,
        }],
        output='screen',
        emulate_tty=True
    )

    action_hand_state_publisher = launch_ros.actions.Node(
        package=PACKAGE_NAME,                   # 包名
        executable='hand_state_publisher',  # 可执行文件名
        name='hand_state_publisher', # 节点名称
        output='screen',                 # 输出日志到终端
        emulate_tty=True                 # 更好地显示日志
    )

    action_joint_state_publisher_gui = launch_ros.actions.Node(
        package='joint_state_publisher_gui',      # 包名
        executable='joint_state_publisher_gui',   # 可执行文件名
        name='joint_state_publisher_gui',         # 节点名称 (可选，可以自定义)
        output='screen',                          # 输出日志到终端
        emulate_tty=True,                         # 更好地显示日志
    )

    action_rviz_node = launch_ros.actions.Node(
        package='rviz2',
        executable='rviz2',
        arguments=['-d', default_rviz_config_path],
        condition=IfCondition(LaunchConfiguration('use_rviz')),
    )

    return [
        action_robot_state_publisher,
        action_hand_state_publisher,
        # action_joint_state_publisher_gui,
        action_rviz_node
    ]


def generate_launch_description():
    urdf_package_path = get_package_share_directory(PACKAGE_NAME)
    model_config_path = os.path.join(urdf_package_path, 'config', 'model.yaml')
    model_config = _load_simple_yaml(model_config_path)

    return launch.LaunchDescription([
        DeclareLaunchArgument(
            name='model_id',
            default_value=model_config.get('active_model', ''),
            description='Model directory under hand_control_description/models',
        ),
        DeclareLaunchArgument(
            name='urdf_file',
            default_value=model_config.get('urdf_file', ''),
            description='URDF path relative to models/<model_id>, empty means auto-detect',
        ),
        DeclareLaunchArgument(
            name='model',
            default_value='',
            description='Full URDF/xacro path override',
        ),
        DeclareLaunchArgument(
            name='use_rviz',
            default_value='true',
            description='Start RViz2 with the robot model config',
        ),
        OpaqueFunction(function=_launch_setup),
    ])
