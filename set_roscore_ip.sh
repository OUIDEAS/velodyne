# BOTTOM IP SHOULD MATCH YOUR COMPUTER, TOP SHOULD MATCH HOST
# USED TO SEND TOPICS TO ROSCORE MASTER ON SEPARATE PC
# DOES NOT WORK IN SHELL SCRIPT, BUT THESE COMMANDS NEED PERFORMED MANUALLY?
# TODO: Identify why... and how to fix...
export ROS_MASTER_URI=http://192.168.1.61:11311/
echo $ROS_MASTER_URI
# YOUR COMPUTERS IP BELOW: v
export ROS_IP=192.168.1.61
echo $ROS_IP

source /opt/ros/melodic/setup.bash