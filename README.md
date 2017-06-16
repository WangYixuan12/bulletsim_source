# bulletsim_source
Code for tracking deformable objects by stereo cameras

## I. Dependency Installation

### 1. Install Ubuntu 14.04 LTS 
Reference: http://howtoubuntu.org/how-to-install-ubuntu-14-04-trusty-tahr
### 2. Install ROS-Indigo
#### 2.1 Installation
Reference: http://wiki.ros.org/indigo/Installation/Ubuntu
```Bash
sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list'
sudo apt-key adv --keyserver hkp://ha.pool.sks-keyservers.net:80 --recv-key 421C365BD9FF1F717815A3895523BAEEB01FA116
sudo apt-get update
sudo apt-get install ros-indigo-desktop-full
sudo rosdep init
rosdep update
echo "source /opt/ros/indigo/setup.bash" >> ~/.bashrc
source ~/.bashrc
sudo apt-get install python-rosinstall
```
#### 2.2 Create Catkin workspace
Reference: http://wiki.ros.org/ROS/Tutorials/InstallingandConfiguringROSEnvironment
```Bash
mkdir -p ~/catkin_ws/src
cd ~/catkin_ws/
catkin_make
echo "source ~/catkin_ws/devel/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

### 3. Driver for Kinect V1
```Bash
sudo apt-get install libfreenect-dev
sudo apt-get install ros-indigo-freenect-launch
```
Test Kinect V1 connection by 
```Bash
roslaunch freenect_launch freenect.launch
```

### 4. Driver for Kinect V2
Your computer needs to have USB3.0

Install the latest versoin of Nivida GPU driver. Restart computer after GPU driver installation.
```Bash
sudo apt-get install nvidia-3 (press tab and install the one with the largest version number, such as 340, 375)
```
Use ocl-icd-libopencl1 to replace nvidia-libopencl1
```Bash
sudo apt-get install ocl-icd-opencl-dev
```
Install libfreenect2
```Bash
cd ~
git clone https://github.com/OpenKinect/libfreenect2.git
cd libfreenect2
cd depends; ./download_debs_trusty.sh
sudo apt-get install build-essential cmake pkg-config
sudo dpkg -i debs/libusb*deb
sudo apt-get install libturbojpeg libjpeg-turbo8-dev
sudo dpkg -i debs/libglfw3*deb; sudo apt-get install -f
cd ..
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/freenect2 -DENABLE_CXX11=ON
make
make install
sudo cp ../platform/linux/udev/90-kinect2.rules /etc/udev/rules.d/
```
Plug in Kinect V2 to USB3.0, run the test program for libfreenect2
```Bash
cd bin
./Protonect gl
./Protonect cl
./Protonect cpu
```
Install IAI Kinect2 to bridge the libfreenect2 and ROS
```Bash
cd ~/catkin_ws/src/ 
git clone https://github.com/code-iai/iai_kinect2.git 
cd iai_kinect2 
rosdep install -r --from-paths . 
cd ~/catkin_ws 
catkin_make -DCMAKE_BUILD_TYPE="Release"
```
Test whether the kinect data is published as ROS Topic
```Bash
roslaunch kinect2_bridge kinect2_bridge.launch
rostopic list
```

### 5. Other dependency for bulletsim
```Bash
sudo apt-get install libopenscenegraph-dev python-networkx
```

## II. Install bulletsim
### 1. Download code and set up the environment variables
```Bash
cd ~
mkdir DeformableTracking && cd DeformableTracking
git clone git@github.com:thomastangucb/bulletsim_source.git
mkdir bulletsim_build
echo 'export BULLETSIM_SOURCE_DIR=~/DeformableTracking/bulletsim_source' >> ~/.bashrc
echo 'export BULLETSIM_BUILD_DIR=~/DeformableTracking/bulletsim_build' >> ~/.bashrc
source ~/.bashrc
```
### 2. Compile ROS packages "bulletsim_msgs" and "bulletsim_python" for customized ROS messages
#### 2.1 Copy the "bulletsim_msgs" and "bulletsim_python" folders to Catkin workspace
```Bash
cp -R $BULLETSIM_SOURCE_DIR/src/bulletsim_msgs ~/catkin_ws/src
cp -R $BULLETSIM_SOURCE_DIR/src/bulletsim_python ~/catkin_ws/src
```
#### 2.2. Catkin_make the ROS package and create a project for Eclipse
```Bash
cd ~/catkin_ws
catkin_make --force-cmake -G"Eclipse CDT4 - Unix Makefiles"
```

### 3. Compile "fgt" and "cpd" packages for rope tracking
#### 3.1 Copy the "fgt" and "cpd" folders to outside
```Bash
cp -R $BULLETSIM_SOURCE_DIR/lib/fgt ~/DeformableTracking
cp -R $BULLETSIM_SOURCE_DIR/lib/cpd ~/DeformableTracking
```
#### 3.2 Compile package "fgt"
```Bash
mkdir ~/DeformableTracking/fgt/build && cd ~/DeformableTracking/fgt/build
cmake ..  -DCMAKE_BUILD_TYPE=Release
make && sudo make install
make test
```
#### 3.3 Compile package "cpd"
```Bash
mkdir ~/DeformableTracking/cpd/build && cd ~/DeformableTracking/cpd/build
cmake ..  -DCMAKE_BUILD_TYPE=Release -DWITH_FGT=ON
make && sudo make install
make test
```

### 4. Create two Eclipse projects (Release and Debug) to folder "bulletsim_build"
```Bash
cd $BULLETSIM_SOURCE_DIR
./make_eclipse_project.sh
```
### 5. Import the Release and Debug projects to Eclipse, then compile
reference: http://www.cnblogs.com/cv-pr/p/4871546.html
