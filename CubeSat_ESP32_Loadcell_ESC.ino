#include <Arduino.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <math.h>

// =====================================================
//                      LOAD CELLS
// =====================================================
HX711 scale1;
HX711 scale2;

#define DT1  4
#define SCK1 5
#define DT2  18
#define SCK2 19

float counts_per_gram_1 = 413.200867f;
float counts_per_gram_2 = 416.686218f;

// Moment kolu (metre)
float r = 0.03f;

// Kucuk gurultuleri yok say
const float GRAM_DEADBAND = 3.0f;

// Load cell filtresi
const float LOADCELL_ALPHA = 0.35f;

// Motorlar stop durumundayken kucuk offsetleri yavas duzelt
const float AUTO_ZERO_WINDOW_G = 8.0f;
const float AUTO_ZERO_ALPHA = 0.002f;
float soft_zero1_g = 0.0f;
float soft_zero2_g = 0.0f;

// Yuk hucrelerinin isaretleri, mekanik yerlesime gore test edilmelidir
const float LC1_SIGN = +1.0f;
const float LC2_SIGN = +1.0f;

float gram1_f = 0.0f;
float gram2_f = 0.0f;
float tau_net_Nm = 0.0f;

// =====================================================
//                         ESC
// =====================================================
Servo esc1;
Servo esc2;

const int ESC1_PIN   = 25;
const int ESC2_PIN   = 26;
const int ESC_MIN_US = 1000;
const int ESC_MAX_US = 2000;

const int MOTOR_START_US = 1080;
const int PWM_MOTOR1 = 1090;
const int PWM_MOTOR2 = 1090;
float US_RAMP_RATE = 3.0f; // us/s
const uint32_t HOLD_OFF_MS = 3000;

volatile bool stop_latched = true;
int us_cmd1 = ESC_MIN_US;
int us_cmd2 = ESC_MIN_US;
float us_cmd1_f = ESC_MIN_US;
float us_cmd2_f = ESC_MIN_US;
uint32_t t0_ms = 0;
uint32_t last_us_t = 0;

// =====================================================
//                   YARDIMCI FONKSIYONLAR
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

void esc_arm() {
  Serial.println("[ESC] Arm bekleniyor, motorlar minimum PWM'de. 5 saniye...");
  send_min();
  delay(5000);
  Serial.println("[ESC] Arm tamam.");
}

void tareBoth(int samples = 40) {
  Serial.println("[LOADCELL] Platform bos olsun. Tare yapiliyor...");
  send_min();
  delay(2000);
  scale1.tare(samples);
  scale2.tare(samples);

  gram1_f = 0.0f;
  gram2_f = 0.0f;
  tau_net_Nm = 0.0f;
  soft_zero1_g = 0.0f;
  soft_zero2_g = 0.0f;

  Serial.println("[LOADCELL] Tare tamam.");
}

void handle_serial_now() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == 's' || c == 'S') {
      stop_latched = true;
      reset_pwm_commands();
      send_min();
      Serial.println("[CMD] STOP");
      continue;
    }

    if (c == 'e' || c == 'E') {
      stop_latched = false;
      reset_pwm_commands();
      t0_ms = millis();
      last_us_t = micros();
      send_min();
      Serial.println("[CMD] ENABLE");
      Serial.println("[CMD] 3 saniye sonra iki motor da 1080 us ile baslayacak.");
      continue;
    }

    if (c == 't' || c == 'T') {
      tareBoth();
      continue;
    }
  }
}

// =====================================================
//                     LOAD CELL OKUMA
// =====================================================
void update_loadcells_nonblocking() {
  if (scale1.is_ready()) {
    long raw1 = scale1.read();
    float gram1_raw = (raw1 - scale1.get_offset()) / counts_per_gram_1;
    float gram1 = gram1_raw - soft_zero1_g;

    if (stop_latched && fabs(gram1) < AUTO_ZERO_WINDOW_G) {
      soft_zero1_g += AUTO_ZERO_ALPHA * gram1;
      gram1 = gram1_raw - soft_zero1_g;
    }

    if (fabs(gram1) < GRAM_DEADBAND) gram1 = 0.0f;
    gram1_f = LOADCELL_ALPHA * gram1 + (1.0f - LOADCELL_ALPHA) * gram1_f;
    if (fabs(gram1_f) < GRAM_DEADBAND) gram1_f = 0.0f;
  }

  if (scale2.is_ready()) {
    long raw2 = scale2.read();
    float gram2_raw = (raw2 - scale2.get_offset()) / counts_per_gram_2;
    float gram2 = gram2_raw - soft_zero2_g;

    if (stop_latched && fabs(gram2) < AUTO_ZERO_WINDOW_G) {
      soft_zero2_g += AUTO_ZERO_ALPHA * gram2;
      gram2 = gram2_raw - soft_zero2_g;
    }

    if (fabs(gram2) < GRAM_DEADBAND) gram2 = 0.0f;
    gram2_f = LOADCELL_ALPHA * gram2 + (1.0f - LOADCELL_ALPHA) * gram2_f;
    if (fabs(gram2_f) < GRAM_DEADBAND) gram2_f = 0.0f;
  }

  float f1_N = gram1_f * 0.00981f;
  float f2_N = gram2_f * 0.00981f;
  tau_net_Nm = r * ((LC1_SIGN * f1_N) + (LC2_SIGN * f2_N));
}

// =====================================================
//                       ESC KONTROL
// =====================================================
void update_esc_control() {
  if (stop_latched) {
    send_min();
    reset_pwm_commands();
    return;
  }

  if (millis() - t0_ms < HOLD_OFF_MS) {
    send_min();
    reset_pwm_commands();
    return;
  }

  uint32_t now = micros();
  float dt = (now - last_us_t) * 1e-6f;
  if (dt <= 0.0f || dt > 0.1f) dt = 1e-3f;
  last_us_t = now;

  if (us_cmd1_f < MOTOR_START_US) us_cmd1_f = MOTOR_START_US;
  if (us_cmd2_f < MOTOR_START_US) us_cmd2_f = MOTOR_START_US;

  float max_step = US_RAMP_RATE * dt;

  if (us_cmd1_f < PWM_MOTOR1) {
    us_cmd1_f += max_step;
    if (us_cmd1_f > PWM_MOTOR1) us_cmd1_f = PWM_MOTOR1;
  } else if (us_cmd1_f > PWM_MOTOR1) {
    us_cmd1_f -= max_step;
    if (us_cmd1_f < PWM_MOTOR1) us_cmd1_f = PWM_MOTOR1;
  }

  if (us_cmd2_f < PWM_MOTOR2) {
    us_cmd2_f += max_step;
    if (us_cmd2_f > PWM_MOTOR2) us_cmd2_f = PWM_MOTOR2;
  } else if (us_cmd2_f > PWM_MOTOR2) {
    us_cmd2_f -= max_step;
    if (us_cmd2_f < PWM_MOTOR2) us_cmd2_f = PWM_MOTOR2;
  }

  us_cmd1 = round(us_cmd1_f);
  us_cmd2 = round(us_cmd2_f);
  esc1.writeMicroseconds(us_cmd1);
  esc2.writeMicroseconds(us_cmd2);
}

// =====================================================
//                    GAZEBO ICIN SERIAL
// =====================================================
void print_torque_for_gazebo() {
  static uint32_t tlog = 0;
  if (millis() - tlog >= 20) {
    tlog += 20;
    Serial.print("TAU_NM,");
    Serial.println(tau_net_Nm, 8);
  }
}

// =====================================================
//                          SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
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
  send_min();
  esc_arm();

  t0_ms = millis();
  last_us_t = micros();
  tareBoth();

  Serial.println("[INFO] Hazir.");
  Serial.println("[INFO] Komutlar:");
  Serial.println("[INFO] e : enable");
  Serial.println("[INFO] s : stop");
  Serial.println("[INFO] t : tare");
  Serial.println("[INFO] Seri cikis formati: TAU_NM,value");
  Serial.println("[INFO] ESC1 signal GPIO25, ESC2 signal GPIO26");
  Serial.println("[INFO] Baslatmak icin 'e' gonder.");
}

// =====================================================
//                           LOOP
// =====================================================
void loop() {
  handle_serial_now();
  update_loadcells_nonblocking();
  update_esc_control();
  print_torque_for_gazebo();
  delay(1);
}
