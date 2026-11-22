/*
 * Bremsdreirad – ESP32 + TMC2209 + NEMA17, mit WLAN-Bedienoberflaeche
 *
 *  - ESP32 oeffnet ein eigenes WLAN (kein Router noetig):
 *      Name: Bremsdreirad   Passwort: physik123   ->  im Browser http://192.168.4.1
 *  - Live: gemessene Drehzahl (Hallsensor Exzenter), Zustand beider Hallsensoren, Treiberstatus
 *  - Steuerung: Bremse oeffnen, Referenzfahrt, Testlauf, Scharf schalten (Rampe), STOP, Feinjustage
 *  - Log jedes Bremslaufs als Diagramm, Tabelle und CSV-Download
 *  - Alle Befehle gehen weiterhin auch ueber den seriellen Monitor (115200 Baud), "help"
 */
#include <Arduino.h>
#include <stdarg.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include <Preferences.h>
#include "web_page.h"

// ---------------- WLAN ----------------
const char *AP_SSID = "Bremsdreirad";
const char *AP_PASS = "physik123";      // mind. 8 Zeichen

// ---------------- Pins (ESP32 DevKit V1) ----------------
constexpr int PIN_STEP      = 25;
constexpr int PIN_DIR       = 26;
constexpr int PIN_EN        = 27;   // LOW = Treiber an
constexpr int PIN_UART_RX   = 16;   // direkt an PDN_UART
constexpr int PIN_UART_TX   = 17;   // ueber 1 kOhm an PDN_UART
constexpr int PIN_HALL_EXZ  = 32;   // Hallsensor am Exzenter (A3144 an 5 V, OUT mit 10k nach 3V3, LOW bei Magnet)
constexpr int PIN_HALL_RAMP = 33;   // Hallsensor am Rampenende

// ---------------- Treiber ----------------
constexpr float    R_SENSE        = 0.11f; // BTT/FYSETC-Module: 0.11 Ohm
constexpr uint8_t  DRIVER_ADDRESS = 0;     // MS1 + MS2 auf GND
constexpr uint16_t MICROSTEPS     = 8;     // 1/8: 20 Hz = 32000 Schritte/s
constexpr uint32_t STEPS_PER_REV  = 200UL * MICROSTEPS;
constexpr uint16_t MAX_CURRENT_MA = 1400;  // Grenze fuer TMC2209-Module ohne Luefter

TMC2209Stepper driver(&Serial2, R_SENSE, DRIVER_ADDRESS);
FastAccelStepperEngine engine;
FastAccelStepper *stepper = nullptr;
Preferences prefs;
WebServer server(80);

// ---------------- Einstellungen (werden gespeichert) ----------------
struct Settings {
  float    freqHz        = 5.0f;   // Bremsfrequenz = Exzenter-Umdrehungen pro Sekunde
  float    accelHzPerS   = 200.0f; // Hochlauf: 200 Hz/s -> 20 Hz nach 0,1 s
  uint32_t runTimeMs     = 3000;   // Dauer eines Bremslaufs
  uint16_t currentMa     = 1000;   // Motorstrom (RMS)
  int32_t  releaseOffset = 0;      // Schritte von Hallsensor-Index bis "Bremse offen"
} cfg;

// ---------------- Zustand ----------------
enum class Mode { IDLE, HOMING, ARMED, RUNNING, STOPPING, PARKING };
Mode mode = Mode::IDLE;
bool openAfterHoming = true;       // nach Referenzfahrt in Stellung "offen"

const char *modeName() {
  switch (mode) {
    case Mode::IDLE:     return "Bereit";
    case Mode::HOMING:   return "Referenzfahrt";
    case Mode::ARMED:    return "Scharf - warte auf Rampe";
    case Mode::RUNNING:  return "Bremslauf";
    case Mode::STOPPING: return "Bremst ab";
    case Mode::PARKING:  return "Faehrt auf offen";
  }
  return "?";
}

bool     homed = false;
int32_t  indexPos = 0;             // Motorposition beim letzten Index-Impuls
uint32_t runStartMs = 0, runStartUs = 0, triggerUs = 0;
bool     runFinished = false;
float    runSollHz = 0;
uint32_t logId = 0;                // zaehlt hoch, wenn ein neues Log fertig ist

volatile bool     rampTriggered = false;
volatile uint32_t rampTriggerUs = 0;
volatile uint32_t exzPulseCount = 0;
volatile uint32_t exzLastUs = 0;
volatile uint32_t exzPeriodUs = 0;

struct LogEntry { uint32_t tUs; int32_t pos; };
constexpr size_t LOG_MAX = 2000;
LogEntry logBuf[LOG_MAX];
size_t   logLen = 0;
uint32_t lastSeenPulse = 0;

// Treiberstatus (gecacht, UART nicht bei jeder Web-Abfrage)
bool drvUart = false, drvOt = false, drvOtpw = false;
uint32_t drvCheckedMs = 0;

// ---------------- Ausgabe an Serial + Web ----------------
String reply;
void out(const char *fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  Serial.print(buf);
  reply += buf;
}

// ---------------- Interrupts ----------------
void IRAM_ATTR isrRamp() {
  if (!rampTriggered) { rampTriggered = true; rampTriggerUs = micros(); }
}
void IRAM_ATTR isrExz() {
  uint32_t now = micros();
  uint32_t dt = now - exzLastUs;
  if (dt > 2000) {                 // Entprellen: max. 500 Impulse/s
    exzPeriodUs = dt;
    exzLastUs = now;
    exzPulseCount++;
  }
}

// ---------------- Hilfsfunktionen ----------------
uint32_t stepsPerSec(float hz)        { return (uint32_t)(hz * STEPS_PER_REV + 0.5f); }
int32_t  modPos(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }

float liveHz() {
  uint32_t last = exzLastUs, per = exzPeriodUs;
  if (per == 0) return 0;
  uint32_t since = micros() - last;
  if (since > 1000000UL || since > 2 * per) return 0;   // steht
  return 1e6f / per;
}

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
  driver.toff(4);
  driver.blank_time(24);
  driver.I_scale_analog(false);   // Strom nur per UART, Poti egal
  driver.pdn_disable(true);       // PDN_UART als UART benutzen
  driver.mstep_reg_select(true);  // Mikroschritte aus dem Register
  driver.rms_current(cfg.currentMa);
  driver.microsteps(MICROSTEPS);
  driver.en_spreadCycle(true);    // SpreadCycle: volles Drehmoment bei hoher Drehzahl
  driver.pwm_autoscale(true);
}

void checkDriver() {
  drvUart = driver.test_connection() == 0;
  if (drvUart) { drvOt = driver.ot(); drvOtpw = driver.otpw(); }
  drvCheckedMs = millis();
}

void printDriverStatus() {
  checkDriver();
  out("UART-Verbindung: %s\n", drvUart ? "OK" : "FEHLER (Verkabelung PDN_UART / 1k pruefen)");
  if (!drvUart) return;
  out("Strom: %u mA | Mikroschritte: %u | SpreadCycle: %s\n",
      driver.rms_current(), driver.microsteps(), driver.en_spreadCycle() ? "an" : "aus");
  out("Uebertemperatur: %s | Vorwarnung: %s | Kurzschluss A/B: %d/%d | offene Spule A/B: %d/%d\n",
      drvOt ? "JA" : "nein", drvOtpw ? "JA" : "nein",
      driver.s2ga(), driver.s2gb(), driver.ola(), driver.olb());
}

void printSettings() {
  out("Frequenz %.2f Hz (%lu Schritte/s) | Hochlauf %.0f Hz/s | Laufzeit %lu ms | Strom %u mA | Offset %ld | Referenz: %s\n",
      cfg.freqHz, (unsigned long)stepsPerSec(cfg.freqHz), cfg.accelHzPerS,
      (unsigned long)cfg.runTimeMs, cfg.currentMa, (long)cfg.releaseOffset, homed ? "ja" : "NEIN");
}

void printHelp() {
  out("Befehle:\n"
      "  f <Hz>  a <Hz/s>  t <ms>  i <mA>   Frequenz, Hochlauf, Laufzeit, Strom\n"
      "  open      Bremse oeffnen (faehrt Stellung 'offen' an, referenziert falls noetig)\n"
      "  home      Referenzfahrt, danach 'offen'\n"
      "  j <n>     Exzenter um n Schritte drehen;  cal = Stellung als 'offen' speichern\n"
      "  run       Bremslauf jetzt;  arm = warten auf Rampe;  stop = sofort anhalten\n"
      "  off / on  Motor stromlos / bestromt;  status;  log\n");
}

// ---------------- Log ----------------
// Zeile k (ab 1): Umdrehung, t_ms, Periode_ms, f_Hz, Schritte, Schrittfehler
void logRow(size_t k, float &tMs, float &perMs, float &hz, int32_t &steps, int32_t &err) {
  uint32_t dt = logBuf[k].tUs - logBuf[k - 1].tUs;
  tMs = (logBuf[k].tUs - runStartUs) / 1000.0f;
  perMs = dt / 1000.0f;
  hz = dt ? 1e6f / dt : 0;
  steps = logBuf[k].pos - logBuf[k - 1].pos;
  err = steps - (int32_t)STEPS_PER_REV;
}

void printLog() {
  if (logLen < 2) { out("Kein Log (weniger als 2 Umdrehungen).\n"); return; }
  Serial.printf("\n# Bremslauf: Soll %.2f Hz, Trigger->Start %lu us\n", runSollHz, (unsigned long)(runStartUs - triggerUs));
  Serial.println("umdrehung;t_ms;periode_ms;frequenz_hz;schritte;schrittfehler");
  uint32_t errors = 0;
  for (size_t k = 1; k < logLen; k++) {
    float t, p, hz; int32_t s, e; logRow(k, t, p, hz, s, e);
    if (abs(e) > (int32_t)STEPS_PER_REV / 20) errors++;
    Serial.printf("%u;%.2f;%.2f;%.3f;%ld;%ld\n", (unsigned)k, t, p, hz, (long)s, (long)e);
  }
  Serial.printf("# %u Umdrehungen, %lu mit Schrittverlust-Verdacht\n", (unsigned)(logLen - 1), (unsigned long)errors);
}

// ---------------- Ablauf ----------------
void startHoming(bool thenOpen = true) {
  openAfterHoming = thenOpen;
  stepper->enableOutputs();
  stepper->setSpeedInHz(STEPS_PER_REV);          // 1 Umdrehung pro Sekunde
  stepper->setAcceleration(STEPS_PER_REV * 4);
  lastSeenPulse = exzPulseCount;
  stepper->runForward();
  mode = Mode::HOMING;
  out("Referenzfahrt ...\n");
}

void parkOpen() {
  // naechste Position mit (pos - index) mod SPR == releaseOffset anfahren
  stepper->enableOutputs();
  int32_t pos = stepper->getCurrentPosition();
  int32_t delta = modPos(indexPos + cfg.releaseOffset - pos, STEPS_PER_REV);
  stepper->setSpeedInHz(STEPS_PER_REV * 2);
  stepper->setAcceleration(STEPS_PER_REV * 8);
  stepper->moveTo(pos + delta);
  mode = Mode::PARKING;
}

void startRun(uint32_t trigUs) {
  if (!homed) { out("Erst Referenzfahrt ('home')!\n"); mode = Mode::IDLE; return; }
  logLen = 0;
  lastSeenPulse = exzPulseCount;
  triggerUs = trigUs;
  runSollHz = cfg.freqHz;
  stepper->enableOutputs();
  stepper->setSpeedInHz(stepsPerSec(cfg.freqHz));
  stepper->setAcceleration((int32_t)(cfg.accelHzPerS * STEPS_PER_REV));
  stepper->runForward();
  runStartUs = micros();
  runStartMs = millis();
  mode = Mode::RUNNING;
  out("Bremslauf mit %.2f Hz ...\n", cfg.freqHz);
}

void handleCommand(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  float val = sp < 0 ? NAN : line.substring(sp + 1).toFloat();
  cmd.toLowerCase();

  if (cmd == "stop") {
    stepper->forceStop();
    runFinished = false;
    mode = Mode::IDLE; homed = false;
    out("Gestoppt. Position unsicher -> 'Bremse oeffnen' faehrt neu Referenz.\n");
    return;
  }
  bool busy = (mode == Mode::RUNNING || mode == Mode::HOMING || mode == Mode::PARKING || mode == Mode::STOPPING);
  if (busy) { out("Laeuft gerade - erst STOP oder abwarten.\n"); return; }
  if (mode == Mode::ARMED && cmd != "status" && cmd != "log") { mode = Mode::IDLE; out("Scharf-Modus beendet.\n"); }

  if (cmd == "help") printHelp();
  else if (cmd == "f" && !isnan(val)) { cfg.freqHz = constrain(val, 0.5f, 25.0f); saveSettings(); printSettings(); }
  else if (cmd == "a" && !isnan(val)) { cfg.accelHzPerS = constrain(val, 10.0f, 2000.0f); saveSettings(); printSettings(); }
  else if (cmd == "t" && !isnan(val)) { cfg.runTimeMs = (uint32_t)constrain(val, 100.0f, 60000.0f); saveSettings(); printSettings(); }
  else if (cmd == "i" && !isnan(val)) {
    cfg.currentMa = (uint16_t)constrain(val, 200.0f, (float)MAX_CURRENT_MA);
    driver.rms_current(cfg.currentMa); saveSettings(); printSettings();
  }
  else if (cmd == "open") {
    if (homed) { parkOpen(); out("Fahre auf Stellung 'offen' ...\n"); }
    else startHoming(true);
  }
  else if (cmd == "home") startHoming(true);
  else if (cmd == "j" && !isnan(val)) {
    stepper->enableOutputs(); stepper->setSpeedInHz(STEPS_PER_REV / 2); stepper->setAcceleration(STEPS_PER_REV * 4);
    stepper->move((int32_t)val); out("Gedreht um %ld Schritte.\n", (long)val);
  }
  else if (cmd == "cal") {
    if (!homed) { out("Erst Referenzfahrt ('home')!\n"); return; }
    cfg.releaseOffset = modPos(stepper->getCurrentPosition() - indexPos, STEPS_PER_REV);
    saveSettings();
    out("Stellung 'offen' gespeichert: Offset %ld Schritte\n", (long)cfg.releaseOffset);
  }
  else if (cmd == "run") startRun(micros());
  else if (cmd == "arm") {
    if (!homed) { out("Erst Referenzfahrt ('home')!\n"); return; }
    rampTriggered = false; mode = Mode::ARMED;
    out("Scharf - warte auf Magnet am Rampenende ...\n");
  }
  else if (cmd == "off") { stepper->disableOutputs(); homed = false; out("Motor stromlos (Referenz verworfen).\n"); }
  else if (cmd == "on")  { stepper->enableOutputs(); out("Motor bestromt.\n"); }
  else if (cmd == "status") { printSettings(); printDriverStatus(); }
  else if (cmd == "log") printLog();
  else out("Unbekannter Befehl - 'help'\n");
}

// ---------------- Webserver ----------------
void webCmd() {
  reply = "";
  handleCommand(server.arg("c"));
  server.send(200, "text/plain; charset=utf-8", reply.length() ? reply : "OK");
}

void webStatus() {
  if (mode != Mode::RUNNING && millis() - drvCheckedMs > 2000) checkDriver();
  char buf[400];
  snprintf(buf, sizeof buf,
    "{\"mode\":\"%s\",\"homed\":%s,\"f\":%.2f,\"a\":%.0f,\"t\":%lu,\"i\":%u,\"off\":%ld,"
    "\"pos\":%ld,\"pulses\":%lu,\"hz\":%.3f,\"hallExz\":%d,\"hallRamp\":%d,"
    "\"uart\":%s,\"ot\":%s,\"otpw\":%s,\"logId\":%lu}",
    modeName(), homed ? "true" : "false", cfg.freqHz, cfg.accelHzPerS, (unsigned long)cfg.runTimeMs,
    cfg.currentMa, (long)cfg.releaseOffset, (long)stepper->getCurrentPosition(),
    (unsigned long)exzPulseCount, liveHz(), digitalRead(PIN_HALL_EXZ) == LOW, digitalRead(PIN_HALL_RAMP) == LOW,
    drvUart ? "true" : "false", drvOt ? "true" : "false", drvOtpw ? "true" : "false", (unsigned long)logId);
  server.send(200, "application/json", buf);
}

void webLog(bool csv) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, csv ? "text/csv; charset=utf-8" : "application/json", "");
  char buf[128];
  if (csv) {
    snprintf(buf, sizeof buf, "# Soll %.2f Hz; Trigger->Start %lu us\numdrehung;t_ms;periode_ms;frequenz_hz;schritte;schrittfehler\n",
             runSollHz, (unsigned long)(runStartUs - triggerUs));
  } else {
    snprintf(buf, sizeof buf, "{\"soll\":%.3f,\"delayUs\":%lu,\"spr\":%lu,\"rows\":[",
             runSollHz, (unsigned long)(runStartUs - triggerUs), (unsigned long)STEPS_PER_REV);
  }
  server.sendContent(buf);
  for (size_t k = 1; k < logLen; k++) {
    float t, p, hz; int32_t s, e; logRow(k, t, p, hz, s, e);
    if (csv) snprintf(buf, sizeof buf, "%u;%.2f;%.3f;%.3f;%ld;%ld\n", (unsigned)k, t, p, hz, (long)s, (long)e);
    else     snprintf(buf, sizeof buf, "%s[%u,%.2f,%.3f,%.3f,%ld,%ld]", k > 1 ? "," : "", (unsigned)k, t, p, hz, (long)s, (long)e);
    server.sendContent(buf);
  }
  if (!csv) server.sendContent("]}");
  server.sendContent("");
}

void setupWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);
  MDNS.begin("bremsdreirad");
  server.on("/", [] { server.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/api/cmd", webCmd);
  server.on("/api/status", webStatus);
  server.on("/api/log", [] { webLog(false); });
  server.on("/api/log.csv", [] { webLog(true); });
  server.onNotFound([] { server.send(404, "text/plain", "nicht gefunden"); });
  server.begin();
  Serial.printf("WLAN '%s' (Passwort %s) -> http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Bremsdreirad ===");

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

  setupWeb();
  printDriverStatus();
  printSettings();
  printHelp();
}

void loop() {
  server.handleClient();

  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { reply = ""; handleCommand(line); line = ""; }
    else line += c;
  }

  // neue Exzenter-Impulse verarbeiten
  uint32_t pulses = exzPulseCount;
  if (pulses != lastSeenPulse) {
    lastSeenPulse = pulses;
    uint32_t pulseUs = exzLastUs;
    int32_t pos = stepper->getCurrentPosition();
    // Position auf den Impulszeitpunkt zurueckrechnen (Loop kann durch WLAN verzoegert sein)
    float sps = stepper->getCurrentSpeedInMilliHz() / 1000.0f;
    pos -= (int32_t)(sps * (micros() - pulseUs) / 1e6f);

    if (mode == Mode::HOMING) {
      indexPos = pos;
      homed = true;
      stepper->stopMove();
      mode = Mode::STOPPING;
    } else if (mode == Mode::RUNNING && logLen < LOG_MAX) {
      logBuf[logLen++] = { pulseUs, pos };
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
        if (runFinished) {
          runFinished = false;
          logId++;
          reply = ""; printLog();
          homed = false; startHoming(true);   // nach jedem Lauf neu referenzieren
        } else if (openAfterHoming) parkOpen();
        else mode = Mode::IDLE;
      }
      break;
    case Mode::PARKING:
      if (!stepper->isRunning()) {
        mode = Mode::IDLE;
        Serial.println("Bereit - Bremse offen.");
      }
      break;
    default: break;
  }
}
