#include <Arduino.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <math.h>

// =====================================================
//                      LOAD CELLS
// =====================================================
HX711 scale1;
HX711 scale2;

#define DT1  21
#define SCK1 19

// GPIO35 input-only oldugu icin sadece DT/DOUT olabilir.
// SCK output olmak zorunda, bu yuzden GPIO32 kullanildi.
#define DT2  35
#define SCK2 32

float counts_per_gram_1 = 413.200867f;
float counts_per_gram_2 = 416.686218f;

float r = 0.03f;   // moment kolu, metre

const int AVG_SAMPLES = 3;

// =====================================================
//                         ESC
// =====================================================
Servo esc1;
Servo esc2;

const int ESC1_PIN   = 25;
const int ESC2_PIN   = 26;
const int ESC_MIN_US = 1000;
const int ESC_MAX_US = 2000;

// Bizim guvenli bias / neutral PWM degerimiz
const int MOTOR_BIAS_US = 1080;

// Ilk test icin sert ust limit.
// Motorlar 1085 ustune cikamaz.
const int PWM_HARD_MAX_US = 1085;

// Enable sonrasi ilk kalkis PWM'i
const int MOTOR_START_US = 1080;

// Ramp hizi
// 2 us/s: 1080'den 1085'e yaklasik 2.5 saniye
float US_RAMP_RATE = 2.0f;   // us/s

const uint32_t HOLD_OFF_MS = 3000;

// ROS/PC komutu kesilirse guvenlik stop.
// Sadece remote p komutlari basladiktan sonra aktif olur.
const uint32_t REMOTE_TIMEOUT_MS = 8000;

volatile bool stop_latched = true;
bool remote_control_active = false;

// ESC'ye gonderilecek integer PWM degerleri
int us_cmd1 = ESC_MIN_US;
int us_cmd2 = ESC_MIN_US;

// Ramp icin float PWM degerleri
float us_cmd1_f = ESC_MIN_US;
float us_cmd2_f = ESC_MIN_US;

// ROS/PC tarafindan istenen hedef PWM
int target_us1 = MOTOR_BIAS_US;
int target_us2 = MOTOR_BIAS_US;

uint32_t t0_ms = 0;
uint32_t last_us_t = 0;
uint32_t last_remote_cmd_ms = 0;

// =====================================================
//                    YARDIMCI FONKS.
// =====================================================
void send_min() {
  esc1.writeMicroseconds(ESC_MIN_US);
  esc2.writeMicroseconds(ESC_MIN_US);
}

void reset_pwm_commands() {
  us_cmd1 = ESC_MIN_US;
  us_cmd2 = ESC_MIN_US;

  us_cmd1_f = ESC_MIN_US;
  us_cmd2_f = ESC_MIN_US;
}

int clamp_enabled_pwm(int us) {
  // Guvenlik: enabled durumda motorlar 1080 altina dusmesin,
  // 1085 ustune cikmasin.
  if (us < MOTOR_BIAS_US) return MOTOR_BIAS_US;
  if (us > PWM_HARD_MAX_US) return PWM_HARD_MAX_US;
  return us;
}

void set_targets(int p1, int p2) {
  target_us1 = clamp_enabled_pwm(p1);
  target_us2 = clamp_enabled_pwm(p2);
}

void enable_system(bool remote_mode = false) {
  stop_latched = false;
  remote_control_active = remote_mode;

  reset_pwm_commands();

  t0_ms = millis();
  last_us_t = micros();

  send_min();

  Serial.println("[CMD] ENABLE");
  Serial.println("[CMD] 3 saniye sonra motorlar bias PWM seviyesine cikacak.");
}

void stop_system(const char* reason = "STOP") {
  stop_latched = true;
  remote_control_active = false;

  target_us1 = MOTOR_BIAS_US;
  target_us2 = MOTOR_BIAS_US;

  reset_pwm_commands();
  send_min();

  Serial.print("[CMD] ");
  Serial.println(reason);
}

void esc_arm() {
  Serial.println("[ESC] Arm bekleniyor (5s)...");
  send_min();
  delay(5000);
  Serial.println("[ESC] Arm tamam.");
}

void tareBoth(int samples = 40) {
  Serial.println("[LOADCELL] Platform BOS olsun. Tare yapiliyor...");
  delay(2000);
  scale1.tare(samples);
  scale2.tare(samples);
  Serial.println("[LOADCELL] Tare tamam.");
}

// =====================================================
//                    SERIAL KOMUTLAR
// =====================================================
// Desteklenen komutlar:
// e
// s
// t
// p 1085 1080
//
// p komutu ROS/Ubuntu tarafindan surekli gonderilecek.
// Ornek:
// p 1080 1080
// p 1085 1080
// p 1080 1085
// =====================================================
void handle_serial_now() {
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
      continue;
    }

    char c = line.charAt(0);

    if (c == 's' || c == 'S') {
      stop_system("STOP");
      continue;
    }

    if (c == 'e' || c == 'E') {
      target_us1 = MOTOR_BIAS_US;
      target_us2 = MOTOR_BIAS_US;
      enable_system(false);
      continue;
    }

    if (c == 't' || c == 'T') {
      tareBoth();
      continue;
    }

    if (c == 'p' || c == 'P') {
      int firstSpace = line.indexOf(' ');
      int secondSpace = line.indexOf(' ', firstSpace + 1);

      if (firstSpace < 0 || secondSpace < 0) {
        Serial.println("[WARN] PWM komutu hatali. Ornek: p 1085 1080");
        continue;
      }

      int p1 = line.substring(firstSpace + 1, secondSpace).toInt();
      int p2 = line.substring(secondSpace + 1).toInt();

      set_targets(p1, p2);

      last_remote_cmd_ms = millis();

      // Eger sistem stop durumundaysa, p komutu otomatik enable etsin.
      // Boylece Ubuntu'da --target 45 yazinca motorlar kendisi baslar.
      if (stop_latched) {
        enable_system(true);
      } else {
        remote_control_active = true;
      }

      Serial.print("[CMD] PWM_TARGET ");
      Serial.print(target_us1);
      Serial.print(" ");
      Serial.println(target_us2);

      continue;
    }

    Serial.print("[WARN] Bilinmeyen komut: ");
    Serial.println(line);
  }
}

// =====================================================
//                         SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(5);
  delay(300);

  scale1.begin(DT1, SCK1);
  scale2.begin(DT2, SCK2);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  esc1.setPeriodHertz(50);
  esc2.setPeriodHertz(50);

  esc1.attach(ESC1_PIN, ESC_MIN_US, ESC_MAX_US);
  esc2.attach(ESC2_PIN, ESC_MIN_US, ESC_MAX_US);

  reset_pwm_commands();

  esc_arm();

  t0_ms = millis();
  last_us_t = micros();

  tareBoth();

  Serial.println("[INFO] Hazir.");
  Serial.println("[INFO] Komutlar:");
  Serial.println("  e              : enable, bias PWM");
  Serial.println("  s              : stop");
  Serial.println("  t              : tare");
  Serial.println("  p PWM1 PWM2    : remote PWM command, ornek: p 1085 1080");

  Serial.print("[INFO] Motor bias PWM = ");
  Serial.println(MOTOR_BIAS_US);

  Serial.print("[INFO] PWM hard max = ");
  Serial.println(PWM_HARD_MAX_US);

  Serial.print("[INFO] Ramp rate = ");
  Serial.print(US_RAMP_RATE);
  Serial.println(" us/s");

  Serial.println("[INFO] Guvenlik: enabled durumda PWM 1080-1085 arasina kirpilir.");
}

// =====================================================
//                         LOOP
// =====================================================
void loop() {
  handle_serial_now();

  // Remote kontrol aktifken komut kesilirse stop
  if (
    remote_control_active &&
    !stop_latched &&
    (millis() - t0_ms > HOLD_OFF_MS) &&
    (millis() - last_remote_cmd_ms > REMOTE_TIMEOUT_MS)
  ) {
    stop_system("REMOTE TIMEOUT STOP");
  }

  // ================= LOAD CELL OKUMA =================
  long raw1 = scale1.read_average(AVG_SAMPLES);
  long raw2 = scale2.read_average(AVG_SAMPLES);

  float gram1 = (raw1 - scale1.get_offset()) / counts_per_gram_1;
  float gram2 = (raw2 - scale2.get_offset()) / counts_per_gram_2;

  float f1 = gram1 * 0.00981f;
  float f2 = gram2 * 0.00981f;

  float tork1 = f1 * r;
  float tork2 = f2 * r;

  // Simdilik onceki loglarla ayni kalsin.
  // Yani neutral offsetimizi bozmayalim.
  float tork_net = tork1 - tork2;

  // Eger load cell yonlerini tekrar kalibre edersek bunu degistirebiliriz:
  // float tork_net = tork1 - tork2;

  // ================= ESC KONTROL =================
  if (stop_latched) {
    send_min();
    reset_pwm_commands();
  } 
  else {
    // Enable sonrasi 3 saniye 1000 us gonder
    if (millis() - t0_ms < HOLD_OFF_MS) {
      send_min();
      reset_pwm_commands();
    } 
    else {
      uint32_t now = micros();
      float dt = (now - last_us_t) * 1e-6f;
      last_us_t = now;

      if (dt <= 0.0f) {
        dt = 1e-3f;
      }

      // HX711 okuma yavas oldugu icin dt 0.1'den buyuk olabilir.
      // Bunu 0.001'e dusurmek rampi bozuyordu.
      if (dt > 0.5f) {
        dt = 0.5f;
      }

      // -------------------------------------------------
      // ILK KALKIS
      // -------------------------------------------------
      // 3 saniyelik beklemeden sonra iki motor da direkt
      // 1080 us seviyesine alinir.
      // Boylece motorlar olu bolgede takilmaz.
      // -------------------------------------------------
      if (us_cmd1_f < MOTOR_START_US) {
        us_cmd1_f = MOTOR_START_US;
      }

      if (us_cmd2_f < MOTOR_START_US) {
        us_cmd2_f = MOTOR_START_US;
      }

      // -------------------------------------------------
      // YUMUSAK RAMP
      // -------------------------------------------------
      float max_step = US_RAMP_RATE * dt;

      if (us_cmd1_f < target_us1) {
        us_cmd1_f = min(us_cmd1_f + max_step, (float)target_us1);
      } 
      else if (us_cmd1_f > target_us1) {
        us_cmd1_f = max(us_cmd1_f - max_step, (float)target_us1);
      }

      if (us_cmd2_f < target_us2) {
        us_cmd2_f = min(us_cmd2_f + max_step, (float)target_us2);
      } 
      else if (us_cmd2_f > target_us2) {
        us_cmd2_f = max(us_cmd2_f - max_step, (float)target_us2);
      }

      us_cmd1 = round(us_cmd1_f);
      us_cmd2 = round(us_cmd2_f);

      // Son sert guvenlik kirpmasi
      us_cmd1 = clamp_enabled_pwm(us_cmd1);
      us_cmd2 = clamp_enabled_pwm(us_cmd2);

      esc1.writeMicroseconds(us_cmd1);
      esc2.writeMicroseconds(us_cmd2);
    }
  }

  // ================= SERIAL PLOTTER / ROS VERI =================
  static uint32_t tlog = 0;
  if (millis() - tlog >= 10) {
    tlog += 10;

    // Format SABIT KALACAK:
    // F1_N F2_N Tnet_Nm PWM1 PWM2
    Serial.print(f1, 4);
    Serial.print(" ");
    Serial.print(f2, 4);
    Serial.print(" ");
    Serial.print(tork_net, 4);
    Serial.print(" ");
    Serial.print(us_cmd1);
    Serial.print(" ");
    Serial.println(us_cmd2);
  }

  delay(1);
}