from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import PushROSNamespace
from launch_ros.actions import SetParameter
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():

    return LaunchDescription([
        DeclareLaunchArgument('namespace', default_value=''),
        DeclareLaunchArgument('map_frame', default_value='map'),
        PushROSNamespace(
            namespace=LaunchConfiguration('namespace')
        ),
        SetParameter(
            name='map_frame',
            value=LaunchConfiguration('map_frame')
        ),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource([
                PathJoinSubstitution([
                    FindPackageShare('cube_bathymetry'),
                    'launch',
                    'cube_bathymetry_launch.py'
                ])
            ]),
        )
    ])
