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

"""
Launch-file test: the node is activated once, and a later reset stays put.

reset_cube_nodes.sh --consumer drives deactivate, cleanup, configure, activate.
The launch file's own activation handler must fire for the first configure only:
it used to re-activate on every transition into 'inactive', then on every
configure, so the script's own activate found the node already active.
"""

import os
import time
import unittest

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource

import launch_testing
import launch_testing.actions

from lifecycle_msgs.msg import State, Transition
from lifecycle_msgs.srv import ChangeState, GetState

import rclpy

NAMESPACE = 'launch_lifecycle_test'
NODE = '/' + NAMESPACE + '/coverage_assessment'


def generate_test_description():
    launch_file = os.path.join(
        get_package_share_directory('coverage_assessment'),
        'launch', 'coverage_assessment_launch.py')
    return LaunchDescription([
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(launch_file),
            launch_arguments={'namespace': NAMESPACE}.items()),
        launch_testing.actions.ReadyToTest(),
    ])


class TestLaunchLifecycle(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        rclpy.init()
        cls.node = rclpy.create_node('launch_lifecycle_test_client')
        cls.get_state = cls.node.create_client(GetState, NODE + '/get_state')
        cls.change_state = cls.node.create_client(ChangeState, NODE + '/change_state')

    @classmethod
    def tearDownClass(cls):
        cls.node.destroy_node()
        rclpy.shutdown()

    def _state(self):
        future = self.get_state.call_async(GetState.Request())
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=5.0)
        if future.result() is None:
            return None
        return future.result().current_state.id

    def _wait_for(self, state_id, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if self._state() == state_id:
                return True
            time.sleep(0.1)
        return False

    def test_activates_once_and_a_reset_sequence_stays_put(self):
        self.assertTrue(self.get_state.wait_for_service(timeout_sec=30.0))
        self.assertTrue(self.change_state.wait_for_service(timeout_sec=30.0))
        self.assertTrue(
            self._wait_for(State.PRIMARY_STATE_ACTIVE, 30.0),
            'the launch file did not configure and activate the node')

        self._transition(Transition.TRANSITION_DEACTIVATE)
        self._assert_stays(State.PRIMARY_STATE_INACTIVE, 5.0)

        # The rest of the script's sequence: after the second configure the
        # launch handler must not activate the node a second time.
        self._transition(Transition.TRANSITION_CLEANUP)
        self._assert_stays(State.PRIMARY_STATE_UNCONFIGURED, 1.0)
        self._transition(Transition.TRANSITION_CONFIGURE)
        self._assert_stays(State.PRIMARY_STATE_INACTIVE, 5.0)

        # And the script's own activate works.
        self._transition(Transition.TRANSITION_ACTIVATE)
        self.assertEqual(self._state(), State.PRIMARY_STATE_ACTIVE)

    def _transition(self, transition_id):
        request = ChangeState.Request()
        request.transition.id = transition_id
        future = self.change_state.call_async(request)
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=10.0)
        self.assertTrue(
            future.result() is not None and future.result().success,
            'transition %d failed' % transition_id)

    def _assert_stays(self, state_id, seconds):
        """Positive check, repeated: in `state_id` now and for `seconds` more."""
        end = time.monotonic() + seconds
        seen = 0
        while time.monotonic() < end:
            self.assertEqual(self._state(), state_id)
            seen += 1
            time.sleep(0.25)
        self.assertGreaterEqual(seen, int(seconds * 2))
