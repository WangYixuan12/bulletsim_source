#!/usr/bin/env python  
import rospy
import tf
from geometry_msgs import PoseStamped

def handle_pose(msg, name):
    br = tf.TransformBroadcaster()
    br.sendTransform((msg.x, msg.y, 0),
                     tf.transformations.quaternion_from_euler(0, 0, msg.theta),
                     rospy.Time.now(),
                     name,
                     "world")

if __name__ == '__main__':
    rospy.init_node('cpd_physics_tf_broadcaster')

    rospy.Subscriber('/move_base_simple/goal/Pose',
                     PoseStamped,
                     handle_pose,
                     'move_base_simple/goal')
    rospy.spin()