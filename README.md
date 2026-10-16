# cubesat_ws_ROS_Gazebo_Project.tar
ROS2_CONTROL

# CubeSat ROS 2 + Gazebo Kullanım Notu

Bu dosya `cubesat_ws_ROS_Gazebo_Project.tar.gz` arşivini aldıktan sonra projeyi çalıştırmak için gereken temel adımları içerir.

## 1. Dosyayı aç

Dosyayı Home klasörüne koyduktan sonra terminalde:

```bash
cd ~
tar -xzvf cubesat_ws_ROS_Gazebo_Project.tar.gz
cd ~/cubesat_ws
```

## 2. Gerekli paketleri kur

Sistemde ROS 2 Jazzy kurulu olmalıdır. Eksik paketler için:

```bash
sudo apt update
sudo apt install -y \
python3-colcon-common-extensions \
python3-serial \
ros-jazzy-xacro \
ros-jazzy-ros-gz-sim \
ros-jazzy-ros-gz-bridge
```

## 3. Workspace’i build et

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
colcon build
source install/setup.bash
```

Her yeni terminalde şu komutlar çalıştırılmalıdır:

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash
```

## 4. Gazebo uzay ortamını aç

Birinci terminalde:

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash

export GZ_SIM_RESOURCE_PATH=$GZ_SIM_RESOURCE_PATH:$HOME/cubesat_ws/src:$HOME/cubesat_ws/install/cubesat_description/share

gz sim -r -v 4 ~/cubesat_ws/src/cubesat_description/worlds/space_torque_test.sdf
```

Bu komut yıldızlı uzay ortamını açar.

## 5. CubeSat modelini Gazebo’ya ekle

İkinci terminalde:

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash

xacro ~/cubesat_ws/src/cubesat_description/urdf/cubesat.urdf.xacro > /tmp/cubesat_raw.urdf

sed "s|model://cubesat_description|file://$HOME/cubesat_ws/src/cubesat_description|g" \
/tmp/cubesat_raw.urdf > /tmp/cubesat_gazebo.urdf

ros2 run ros_gz_sim create \
-world space_torque_test \
-file /tmp/cubesat_gazebo.urdf \
-name cubesat \
-x 0 -y 0 -z 0
```

CubeSat görünmezse Gazebo’da modele tıklayıp `F` tuşuna bas.

## 6. Manuel tork testi

Üçüncü terminalde CubeSat’e tork vermek için:

```bash
gz topic -t /world/space_torque_test/wrench/persistent \
-m gz.msgs.EntityWrench \
-p 'entity { name: "cubesat" type: MODEL } wrench { torque { x: 0 y: 0 z: 0.001 } }'
```

Torku kesmek için:

```bash
gz topic -t /world/space_torque_test/wrench/clear \
-m gz.msgs.Entity \
-p 'name: "cubesat" type: MODEL'
```

Not: Tork kesilince CubeSat dönmeye devam edebilir. Bu normaldir çünkü ortam sıfır sürtünmeli uzay ortamı olarak ayarlanmıştır.

## 7. Simülasyonu resetlemek

```bash
gz service -s /world/space_torque_test/control \
--reqtype gz.msgs.WorldControl \
--reptype gz.msgs.Boolean \
--timeout 300 \
--req 'reset { all: true }'
```

## 8. ESP32 kullanılacaksa

ESP32’den gelen loadcell tork verisi Gazebo’ya aktarılabilir. Önce port kontrol edilir:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Genelde port `/dev/ttyUSB0` olur. İzin vermek için:

```bash
sudo chmod a+rw /dev/ttyUSB0
```

Bridge scriptini çalıştırmak için:

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash

python3 ~/cubesat_ws/esp32_tau_to_gazebo_interactive.py \
--port /dev/ttyUSB0 \
--baud 115200 \
--world space_torque_test \
--model cubesat \
--scale 1.0
```

Script çalışırken kullanılabilecek komutlar:

```text
t = tare
e = motor enable
s = stop
```

Arduino Serial Monitor açık olmamalıdır. Seri portu aynı anda sadece bir program kullanabilir.

## 9. Önemli dosyalar

Gazebo world dosyası:

```bash
~/cubesat_ws/src/cubesat_description/worlds/space_torque_test.sdf
```

CubeSat URDF/Xacro dosyası:

```bash
~/cubesat_ws/src/cubesat_description/urdf/cubesat.urdf.xacro
```

ESP32-Gazebo bridge scripti:

```bash
~/cubesat_ws/esp32_tau_to_gazebo_interactive.py
```

## Kısa çalışma sırası

```text
1. tar.gz dosyasını aç
2. colcon build yap
3. Gazebo world aç
4. CubeSat modelini spawn et
5. Manuel tork veya ESP32 tork testi yap
```
