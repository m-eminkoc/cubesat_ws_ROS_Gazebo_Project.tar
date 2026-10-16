# CubeSat ROS 2 / Gazebo — Kurulum ve Çalıştırma

Bu rehber, projeyi başka bir bilgisayarda çalıştırmak içindir.

## 1. Gereksinimler

- **Ubuntu 24.04**, **ROS 2 Jazzy** ve **Gazebo Harmonic** kurulu olmalı.
- ESP32, iki ESC/motor ve HX711'li iki load cell düzeneği hazır olmalı.

Terminalde:

```bash
sudo apt update
sudo apt install -y ros-jazzy-ros-gz ros-jazzy-xacro python3-colcon-common-extensions python3-serial
```

## 2. Projeyi aç ve derle

GitHub'dan `cubesat_ws_FINAL.tar.gz` dosyasını indirip **İndirilenler (Downloads)** klasörüne koy. Sonra:

```bash
cd ~
tar -xzf ~/Downloads/cubesat_ws_FINAL.tar.gz
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
source install/setup.bash
```

## 3. Arduino / ESP32

- Aynı, daha önce programlanmış ESP32 kullanılıyorsa kodu tekrar yüklemek gerekmez.
- Farklı ESP32 kullanılacaksa repodaki ino dosyasını **Arduino IDE** ile aç. ESP32 kart desteğini ve **ESP32Servo** ile **HX711** kütüphanelerini kurup kodu yükle.
- USB ile ESP32'yi bağla; **Arduino Serial Monitor kapalı** olsun. ESC güç bağlantılarını güvenli biçimde yap.
- Portu öğren:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

`Permission denied` hatasında `sudo usermod -aG dialout "$USER"` komutunu çalıştırıp **oturumu kapat ve yeniden aç**.

## 4. Çalıştırma — terminalleri sırayla aç

### T1 — Gazebo uzay ortamı

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash
export GZ_SIM_RESOURCE_PATH=$GZ_SIM_RESOURCE_PATH:$HOME/cubesat_ws/src:$HOME/cubesat_ws/install/cubesat_description/share
gz sim -r -v 4 ~/cubesat_ws/src/cubesat_description/worlds/space_torque_test.sdf
```

**T1 açık kalsın.**

### T2 — CubeSat modelini ekle

**Yeni terminal** aç:

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash

xacro ~/cubesat_ws/src/cubesat_description/urdf/cubesat.urdf.xacro > /tmp/cubesat_raw.urdf
sed "s|model://cubesat_description|file://$HOME/cubesat_ws/src/cubesat_description|g" \
  /tmp/cubesat_raw.urdf > /tmp/cubesat_fixed.urdf

ros2 run ros_gz_sim create \
  -world space_torque_test \
  -file /tmp/cubesat_fixed.urdf \
  -name cubesat \
  -x 0 -y 0 -z 0.8
```

Gazebo'da **cubesat** görünmeli.

### T3 — CubeSat açısını 0° yap

**Yeni terminalde** (ya da T2 bittikten sonra aynı terminalde):

```bash
gz service -s /world/space_torque_test/set_pose/blocking \
  --reqtype gz.msgs.Pose \
  --reptype gz.msgs.Boolean \
  --timeout 10000 \
  --req 'name: "cubesat" position {x: 0 y: 0 z: 0.8} orientation {x: 0 y: 0 z: 0 w: 1}'
```

### T4 — Motorlu 45° kontrol

**Yeni terminalde**:

```bash
cd ~/cubesat_ws
source /opt/ros/jazzy/setup.bash
source install/setup.bash

python3 ~/cubesat_ws/tools/final_hw_attitude_control.py \
  --target 45 \ 
  --port /dev/ttyUSB0 \
  --model cubesat \
  --z 0.8 \
  --max-time 120 \
  --tau-deadband 0.00001 \
  --torque-gain 1000000 \
  --max-speed 120
```

**Not:** ESP32 portu `/dev/ttyACM0` ise `--port /dev/ttyUSB0` yerine onu yaz.

## 5. Kısa notlar

- Sıra: **T1 → T2 → T3 → T4**.
- Aynı Gazebo oturumunda **T2'yi tekrar çalıştırma**; ikinci bir `cubesat` oluşturmayı denersin.
- Gazebo ekranı boşsa T1'deki dünya dosyasının yüklendiğini, CubeSat görünmüyorsa T2'nin başarıyla bittiğini kontrol et.
- Bu, gerçek motor/tork ölçümünü **Gazebo'da ölçeklenmiş yönelim görselleştirmesine** bağlayan bir demonstrasyondur; tam fiziksel uçuş simülasyonu değildir.
- Motorlar çalışırken çevreyi boş tut; bir sorun olursa **güç kaynağını kes**.
