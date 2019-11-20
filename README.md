# ABBP Installation
Optional (to make life easy):  
`export ROS_WS=$HOME/catkin_ws`  
`export UBUNTU_DISTRO=bionic`  
# ROS - Catkin Workspace
Todo  
`...apt-get...-ros-melodic-desktop-full ...`  
...
`cd $ROS_WS`  
`catkin_init_workspace`  
in $HOME/.bashrc:  
`source $ROS_WS/devel/setup.bash`  
# Camera
- Install Realsense Driver:  
`sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-key F6E65AC044F831AC80A06380C8B3A55A6F3EFCDE`  
`sudo add-apt-repository "deb http://realsense-hw-public.s3.amazonaws.com/Debian/apt-repo $ main" $UBUNTU_DISTRO -u`  
`sudo apt-get install librealsense2-dkms`  
`sudo apt-get install librealsense2-utils`  
- Install Realsense ROS-package:  
`cd $ROS_WS/src`  
`git clone https://github.com/pal-robotics/ddynamic_reconfigure.git -b kinetic-devel`  
`git clone https://github.com/IntelRealSense/realsense-ros.git`  
`cd $ROS_WS`  
`catkin_make install`  
# Vision (aanvullen)
## Windows:
- Install all dependencies
`pip install -r requirements.txt`

## UNIX / Linux
- Install all dependencies
`pip install -r requirements.txt`

- The pyrealsense2 library needs to built with the source files 
`1. Install pyrealsense2 library`

`1. git clone https://github.com/IntelRealSense/librealsense`

`2. cd librealsense`

`3. mkdir build`

`4. cd build`

`5. cmake ../ -DBUILD_PYTHON_BINDINGS=TRUE`

`6. make -j4`

`7. sudo make install #Optional if you want the library to be installed in your system`

`9 - If it all goes without errors, you should be able to find the pyrealsense2.<arch info>.so under build/wrappers/python (actually 3 files with the same name and extensions .so, .so.2, .so.2.8.1). Now the easiest way to use it is run python from that folder and import pyrealsense2 or extract the .so files to the root of your files.`


### Robot
Todo
