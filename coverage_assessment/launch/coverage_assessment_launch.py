# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""Launch one coverage_assessment node, configured and activated at start."""

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import EmitEvent
from launch.actions import LogInfo
from launch.actions import RegisterEventHandler
from launch.event_handlers import OnProcessStart
from launch.events import matches_action
from launch.substitutions import LaunchConfiguration

from launch_ros.actions import LifecycleNode
from launch_ros.event_handlers import OnStateTransition
from launch_ros.events.lifecycle import ChangeState

from lifecycle_msgs.msg import Transition


def generate_launch_description():
    default_params = os.path.join(
        get_package_share_directory('coverage_assessment'),
        'config', 'coverage_assessment.yaml')

    node = LifecycleNode(
        package='coverage_assessment',
        executable='coverage_assessment_node',
        name='coverage_assessment',
        namespace=LaunchConfiguration('namespace'),
        parameters=[LaunchConfiguration('params_file')],
        emulate_tty=True,
        output='screen',
    )

    configure = EmitEvent(event=ChangeState(
        lifecycle_node_matcher=matches_action(node),
        transition_id=Transition.TRANSITION_CONFIGURE))
    activate = EmitEvent(event=ChangeState(
        lifecycle_node_matcher=matches_action(node),
        transition_id=Transition.TRANSITION_ACTIVATE))

    return LaunchDescription([
        DeclareLaunchArgument(
            'namespace', default_value='',
            description='Namespace of the node; relative topics (survey_area, '
                        'coverage_assessment, coverage_grid) live under it.'),
        DeclareLaunchArgument(
            'params_file', default_value=default_params,
            description='YAML parameter file; grid_topics is required.'),
        node,
        # Configure as soon as the process is up, activate once that first
        # configure has finished. start_state is what keeps this handler from
        # firing again on a later transition into 'inactive': a deactivate
        # (reset_cube_nodes.sh --consumer) must leave the node inactive, or the
        # script's cleanup that follows it fails.
        RegisterEventHandler(OnProcessStart(
            target_action=node, on_start=[configure])),
        RegisterEventHandler(OnStateTransition(
            target_lifecycle_node=node, start_state='configuring',
            goal_state='inactive',
            entities=[LogInfo(msg='coverage_assessment configured, activating'),
                      activate])),
    ])
