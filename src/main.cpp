/*
 * Bremsdreirad – Probecode fuer ESP32 + TMC2209 + NEMA17
 *
 * Was der Code kann:
 *  - TMC2209 ueber UART einstellen (Strom, Mikroschritte, SpreadCycle fuer hohe Drehzahl)
 *  - Exzenter mit einstellbarer Bremsfrequenz (Umdrehungen pro Sekunde) drehen
 *  - Referenzfahrt ueber den Hallsensor am Exzenter, danach Stellung "Bremse geloest"
 *  - Bremslauf manuell ("run") oder automatisch ueber den Hallsensor am Rampenende ("arm")
 *  - jede Exzenter-Umdrehung mit Zeitstempel loggen -> echte Frequenz + Schrittverluste
 *  - Einstellungen bleiben nach dem Ausschalten gespeichert
 *
 * Bedienung ueber den seriellen Monitor (115200 Baud, Zeilenende "Newline"). "help" zeigt alle Befehle.
 */
#include <Arduino.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include <Preferences.h>

// ---------------- Pins (ESP32 DevKit V1) ----------------
constexpr int PIN_STEP      = 25;
constexpr int PIN_DIR       = 26;
constexpr int PIN_EN        = 27;   // LOW = Treiber an
constexpr int PIN_UART_RX   = 16;   // direkt an PDN_UART
constexpr int PIN_UART_TX   = 17;   // ueber 1 kOhm an PDN_UART
constexpr int PIN_HALL_EXZ  = 32;   // Hallsensor am Exzenter (A3144, LOW bei Magnet)
constexpr int PIN_HALL_RAMP = 33;   // Hallsensor am Rampenende

// ---------------- Treiber ----------------
constexpr float   R_SENSE        = 0.11f; // BTT/FYSETC-Module: 0.11 Ohm
constexpr uint8_t DRIVER_ADDRESS = 0;     // MS1 + MS2 auf GND
constexpr uint16_t MICROSTEPS    = 8;     // 1/8: 20 Hz = 32000 Schritte/s
constexpr uint32_t STEPS_PER_REV = 200UL * MICROSTEPS;
constexpr uint16_t MAX_CURRENT_MA = 1400; // Grenze fuer TMC2209-Module ohne Luefter

TMC2209Stepper driver(&Serial2, R_SENSE, DRIVER_ADDRESS);
FastAccelStepperEngine engine;
FastAccelStepper *stepper = nullptr;
Preferences prefs;

// ---------------- Einstellungen (werden gespeichert) ----------------
struct Settings {
  float    freqHz       = 5.0f;   // Bremsfrequenz = Exzenter-Umdrehungen pro Sekunde
  float    accelHzPerS  = 200.0f; // Hochlauf: 200 Hz/s -> 20 Hz nach 0,1 s
  uint32_t runTimeMs    = 3000;   // Dauer eines Bremslaufs
  uint16_t currentMa    = 1000;   // Motorstrom (RMS)
  int32_t  releaseOffset = 0;     // Schritte von Hallsensor-Index bis "Bremse geloest"
} cfg;

// ---------------- Zustand ----------------
enum class Mode { IDLE, HOMING, ARMED, RUNNING, STOPPING, PARKING };
Mode mode = Mode::IDLE;

bool     homed = false;
int32_t  indexPos = 0;             // Motorposition beim letzten Index-Impuls
uint32_t runStartMs = 0;
uint32_t triggerUs = 0;
uint32_t runStartUs = 0;

volatile bool     rampTriggered = false;
volatile uint32_t rampTriggerUs = 0;
volatile uint32_t exzPulseCount = 0;
volatile uint32_t exzLastUs = 0;

// Log: ein Eintrag pro Exzenter-Umdrehung
struct LogEntry { uint32_t tUs; int32_t pos; };
constexpr size_t LOG_MAX = 2000;
LogEntry logBuf[LOG_MAX];
size_t   logLen = 0;
uint32_t lastSeenPulse = 0;
bool     runFinished = false;     // nach Bremslauf: Log ausgeben + neu referenzieren

// ---------------- Interrupts ----------------
void IRAM_ATTR isrRamp() {
  if (!rampTriggered) { rampTriggered = true; rampTriggerUs = micros(); }
}
void IRAM_ATTR isrExz() {
  uint32_t now = micros();
  if (now - exzLastUs > 2000) {    // Entprellen: max. 500 Impulse/s
    exzLastUs = now;
    exzPulseCount++;
  }
}

// ---------------- Hilfsfunktionen ----------------
uint32_t stepsPerSec(float hz)   { return (uint32_t)(hz * STEPS_PER_REV + 0.5f); }
int32_t  modPos(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }

void saveSettings() {
  prefs.putFloat("f", cfg.freqHz);
  prefs.putFloat("a", cfg.accelHzPerS);
  prefs.putUInt("t", cfg.runTimeMs);
  prefs.putUShort("i", cfg.currentMa);
  prefs.putInt("off", cfg.releaseOffset);
}
void loadSettings() {
  cfg.freqHz        = prefs.getFloat("f", cfg.freqHz);
  cfg.accelHzPerS   = prefs.getFloat("a", cfg.accelHzPerS);
  cfg.runTimeMs     = prefs.getUInt("t", cfg.runTimeMs);
  cfg.currentMa     = prefs.getUShort("i", cfg.currentMa);
  cfg.releaseOffset = prefs.getInt("off", cfg.releaseOffset);
}

void setupDriver() {
  driver.begin();
  driver.toff(4);                 // Treiber aktiv
  driver.blank_time(24);
  driver.I_scale_analog(false);   // Strom nur per UART, Poti egal
  driver.pdn_disable(true);       // PDN_UART als UART benutzen
  driver.mstep_reg_select(true);  // Mikroschritte aus dem Register
  driver.rms_current(cfg.currentMa);
  driver.microsteps(MICROSTEPS);
  driver.en_spreadCycle(true);    // SpreadCycle: volles Drehmoment bei hoher Drehzahl
  driver.pwm_autoscale(true);
}

void printDriverStatus() {
  uint8_t conn = driver.test_connection();
  Serial.printf("UART-Verbindung: %s\n", conn == 0 ? "OK" : "FEHLER (Verkabelung PDN_UART / 1k pruefen)");
  if (conn != 0) return;
  Serial.printf("Strom eingestellt: %u mA | Mikroschritte: %u | SpreadCycle: %s\n",
                driver.rms_current(), driver.microsteps(), driver.en_spreadCycle() ? "an" : "aus");
  Serial.printf("Uebertemperatur: %s | Vorwarnung: %s | Kurzschluss A/B: %d/%d | offene Spule A/B: %d/%d\n",
                driver.ot() ? "JA" : "nein", driver.otpw() ? "JA" : "nein",
                driver.s2ga(), driver.s2gb(), driver.ola(), driver.olb());
}

void printSettings() {
  Serial.printf("Frequenz %.2f Hz (%lu Schritte/s) | Hochlauf %.0f Hz/s | Laufzeit %lu ms | Strom %u mA | Offset %ld | Referenz: %s\n",
                cfg.freqHz, (unsigned long)stepsPerSec(cfg.freqHz), cfg.accelHzPerS,
                (unsigned long)cfg.runTimeMs, cfg.currentMa, (long)cfg.releaseOffset, homed ? "ja" : "NEIN");
}

void printHelp() {
  Serial.println(F(
    "\nBefehle:\n"
    "  f <Hz>     Bremsfrequenz setzen (0.5 ... 25)\n"
    "  a <Hz/s>   Hochlauf-Beschleunigung (z.B. 200 = 0,1 s bis 20 Hz)\n"
    "  t <ms>     Dauer eines Bremslaufs\n"
    "  i <mA>     Motorstrom (max. 1400)\n"
    "  home       Referenzfahrt, danach Stellung 'geloest'\n"
    "  j <Schritte>  Exzenter um Schritte drehen (zum Kalibrieren)\n"
    "  cal        aktuelle Stellung als 'Bremse geloest' speichern\n"
    "  run        Bremslauf jetzt starten (Test auf dem Tisch)\n"
    "  arm        warten auf Hallsensor am Rampenende, dann Bremslauf\n"
    "  stop       sofort anhalten\n"
    "  off / on   Motor stromlos / bestromt\n"
    "  status     Einstellungen + Treiberstatus\n"
    "  log        Log des letzten Laufs nochmal ausgeben\n"));
}

// ---------------- Ablauf ----------------
void startHoming() {
  stepper->enableOutputs();
  stepper->setSpeedInHz(STEPS_PER_REV);          // 1 Umdrehung pro Sekunde
  stepper->setAcceleration(STEPS_PER_REV * 4);
  lastSeenPulse = exzPulseCount;
  stepper->runForward();
  mode = Mode::HOMING;
  Serial.println("Referenzfahrt ...");
}

void parkReleased() {
  // naechste Position mit (pos - index) mod SPR == releaseOffset anfahren
  int32_t pos = stepper->getCurrentPosition();
  int32_t delta = modPos(indexPos + cfg.releaseOffset - pos, STEPS_PER_REV);
  stepper->setSpeedInHz(STEPS_PER_REV * 2);
  stepper->setAcceleration(STEPS_PER_REV * 8);
  stepper->moveTo(pos + delta);
  mode = Mode::PARKING;
}

void startRun(uint32_t trigUs) {
  if (!homed) { Serial.println("Erst 'home' ausfuehren!"); mode = Mode::IDLE; return; }
  logLen = 0;
  lastSeenPulse = exzPulseCount;
  triggerUs = trigUs;
  stepper->setSpeedInHz(stepsPerSec(cfg.freqHz));
  stepper->setAcceleration((int32_t)(cfg.accelHzPerS * STEPS_PER_REV));
  stepper->runForward();
  runStartUs = micros();
  runStartMs = millis();
  mode = Mode::RUNNING;
}

void printLog() {
  if (logLen < 2) { Serial.println("Kein Log (weniger als 2 Umdrehungen)."); return; }
  Serial.printf("\n# Bremslauf: Soll %.2f Hz, Verzoegerung Trigger->Start %lu us\n",
                cfg.freqHz, (unsigned long)(runStartUs - triggerUs));
  Serial.println("umdrehung;t_ms;periode_ms;frequenz_hz;schritte;schrittfehler");
  uint32_t errors = 0;
  for (size_t k = 1; k < logLen; k++) {
    uint32_t dt = logBuf[k].tUs - logBuf[k-1].tUs;
    int32_t steps = logBuf[k].pos - logBuf[k-1].pos;
    int32_t err = steps - (int32_t)STEPS_PER_REV;
    if (abs(err) > (int32_t)STEPS_PER_REV / 20) errors++;   // >5 % Abweichung
    Serial.printf("%u;%.2f;%.2f;%.3f;%ld;%ld\n", (unsigned)k,
                  (logBuf[k].tUs - runStartUs) / 1000.0f, dt / 1000.0f, 1e6f / dt,
                  (long)steps, (long)err);
  }
  Serial.printf("# %u Umdrehungen, %lu mit Schrittverlust-Verdacht\n", (unsigned)(logLen - 1), (unsigned long)errors);
  if (errors) Serial.println("# -> Strom erhoehen, Frequenz/Hochlauf senken oder Federkraft verringern");
}

void handleCommand(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  float val = sp < 0 ? NAN : line.substring(sp + 1).toFloat();
  cmd.toLowerCase();

  bool busy = (mode == Mode::RUNNING || mode == Mode::HOMING || mode == Mode::PARKING);
  if (cmd == "stop") {
    stepper->forceStop();
    runFinished = false;
    mode = Mode::IDLE; homed = false;   // Position unsicher -> neu referenzieren
    Serial.println("Gestoppt. Bitte 'home'.");
    return;
  }
  if (busy) { Serial.println("Laeuft gerade – erst 'stop' oder abwarten."); return; }

  if (cmd == "help") printHelp();
  else if (cmd == "f" && !isnan(val)) { cfg.freqHz = constrain(val, 0.5f, 25.0f); saveSettings(); printSettings(); }
  else if (cmd == "a" && !isnan(val)) { cfg.accelHzPerS = constrain(val, 10.0f, 2000.0f); saveSettings(); printSettings(); }
  else if (cmd == "t" && !isnan(val)) { cfg.runTimeMs = (uint32_t)constrain(val, 100.0f, 60000.0f); saveSettings(); printSettings(); }
  else if (cmd == "i" && !isnan(val)) {
    cfg.currentMa = (uint16_t)constrain(val, 200.0f, (float)MAX_CURRENT_MA);
    driver.rms_current(cfg.currentMa); saveSettings(); printSettings();
  }
  else if (cmd == "home") startHoming();
  else if (cmd == "j" && !isnan(val)) { stepper->enableOutputs(); stepper->setSpeedInHz(STEPS_PER_REV / 2); stepper->move((int32_t)val); }
  else if (cmd == "cal") {
    if (!homed) { Serial.println("Erst 'home' ausfuehren!"); return; }
    cfg.releaseOffset = modPos(stepper->getCurrentPosition() - indexPos, STEPS_PER_REV);
    saveSettings();
    Serial.printf("Stellung 'geloest' gespeichert: Offset %ld Schritte\n", (long)cfg.releaseOffset);
  }
  else if (cmd == "run") startRun(micros());
  else if (cmd == "arm") {
    if (!homed) { Serial.println("Erst 'home' ausfuehren!"); return; }
    rampTriggered = false; mode = Mode::ARMED;
    Serial.println("Scharf – warte auf Magnet am Rampenende ... ('stop' bricht ab)");
  }
  else if (cmd == "off") { stepper->disableOutputs(); homed = false; Serial.println("Motor stromlos."); }
  else if (cmd == "on")  { stepper->enableOutputs(); Serial.println("Motor bestromt."); }
  else if (cmd == "status") { printSettings(); printDriverStatus(); }
  else if (cmd == "log") printLog();
  else Serial.println("Unbekannter Befehl – 'help'");
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Bremsdreirad – Probecode ESP32 + TMC2209 ===");

  prefs.begin("brems", false);
  loadSettings();

  Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  setupDriver();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (!stepper) { Serial.println("FEHLER: Schrittmotor-Pin nicht nutzbar"); while (true) delay(1000); }
  stepper->setDirectionPin(PIN_DIR);
  stepper->setEnablePin(PIN_EN);       // LOW-aktiv
  stepper->setAutoEnable(false);
  stepper->enableOutputs();

  pinMode(PIN_HALL_EXZ, INPUT_PULLUP);
  pinMode(PIN_HALL_RAMP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL_EXZ), isrExz, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL_RAMP), isrRamp, FALLING);

  printDriverStatus();
  printSettings();
  printHelp();
}

void loop() {
  // Serielle Befehle
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(line); line = ""; }
    else line += c;
  }

  // neue Exzenter-Impulse verarbeiten
  uint32_t pulses = exzPulseCount;
  if (pulses != lastSeenPulse) {
    lastSeenPulse = pulses;
    int32_t pos = stepper->getCurrentPosition();
    if (mode == Mode::HOMING) {
      indexPos = pos;
      homed = true;
      stepper->stopMove();
      mode = Mode::STOPPING;
    } else if (mode == Mode::RUNNING && logLen < LOG_MAX) {
      logBuf[logLen++] = { exzLastUs, pos };
    } else if (mode == Mode::IDLE || mode == Mode::ARMED) {
      indexPos = pos;   // Index nachfuehren
    }
  }

  switch (mode) {
    case Mode::ARMED:
      if (rampTriggered) startRun(rampTriggerUs);
      break;
    case Mode::RUNNING:
      if (millis() - runStartMs >= cfg.runTimeMs) {
        stepper->setAcceleration((int32_t)(cfg.accelHzPerS * STEPS_PER_REV));
        stepper->stopMove();
        runFinished = true;
        mode = Mode::STOPPING;
      }
      break;
    case Mode::STOPPING:
      if (!stepper->isRunning()) {
        if (runFinished) { runFinished = false; printLog(); homed = false; startHoming(); }  // nach jedem Lauf neu referenzieren
        else parkReleased();
      }
      break;
    case Mode::PARKING:
      if (!stepper->isRunning()) {
        mode = Mode::IDLE;
        Serial.println("Bereit – Bremse geloest.");
      }
      break;
    default: break;
  }
}
