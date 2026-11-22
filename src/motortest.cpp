/*
 * Bremsdreirad – MOTORTEST ohne Hallsensoren
 *
 * Zum Ausprobieren von ESP32 + TMC2209 + NEMA17, bevor Sensoren und Bremse dran sind.
 *  - ESP32 oeffnet das WLAN "Bremsdreirad" (Passwort physik123) -> http://192.168.4.1
 *  - Motor an/aus, Dauerlauf mit einstellbarer Frequenz (Umdrehungen pro Sekunde),
 *    Frequenz waehrend des Laufs aendern, Richtung, Schrittweise drehen
 *  - Treiber: Strom, SpreadCycle/StealthChop, UART-Verbindung, Temperatur- und Fehlerflags
 *  - Drehzahltest: faehrt automatisch 2 -> 20 Hz in Stufen hoch (hoeren, ab wann er stottert)
 *
 * In VS Code unten in der PlatformIO-Leiste die Umgebung "env:motortest" waehlen, dann Upload.
 * Fuer das eigentliche Programm wieder "env:esp32dev" waehlen.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>

// ---------------- WLAN ----------------
const char *AP_SSID = "Bremsdreirad";
const char *AP_PASS = "physik123";

// ---------------- Pins (wie im Verdrahtungsschema) ----------------
constexpr int PIN_STEP    = 25;
constexpr int PIN_DIR     = 26;
constexpr int PIN_EN      = 27;   // LOW = Treiber an
constexpr int PIN_UART_RX = 16;   // direkt an PDN_UART
constexpr int PIN_UART_TX = 17;   // ueber 1 kOhm an PDN_UART

constexpr float    R_SENSE        = 0.11f;
constexpr uint8_t  DRIVER_ADDRESS = 0;
constexpr uint16_t MICROSTEPS     = 8;
constexpr uint32_t STEPS_PER_REV  = 200UL * MICROSTEPS;
constexpr uint16_t MAX_CURRENT_MA = 1400;

TMC2209Stepper driver(&Serial2, R_SENSE, DRIVER_ADDRESS);
FastAccelStepperEngine engine;
FastAccelStepper *stepper = nullptr;
WebServer server(80);

// ---------------- Einstellungen ----------------
float    freqHz      = 2.0f;     // Umdrehungen pro Sekunde
float    accelHzPerS = 50.0f;    // sanft fuer den Anfang
uint16_t currentMa   = 800;
bool     spreadCycle = true;
bool     forward     = true;
bool     enabled     = true;

// Drehzahltest (Stufen)
bool     sweepActive = false;
float    sweepHz = 0;
uint32_t sweepNextMs = 0;
constexpr float    SWEEP_START = 2, SWEEP_END = 20, SWEEP_STEP = 2;
constexpr uint32_t SWEEP_HOLD_MS = 2000;

String lastMsg = "Bereit.";

// ---------------- Treiber ----------------
void applyDriver() {
  driver.rms_current(currentMa);
  driver.en_spreadCycle(spreadCycle);
}

void setupDriver() {
  driver.begin();
  driver.toff(4);
  driver.blank_time(24);
  driver.I_scale_analog(false);
  driver.pdn_disable(true);
  driver.mstep_reg_select(true);
  driver.microsteps(MICROSTEPS);
  driver.pwm_autoscale(true);
  applyDriver();
}

uint32_t sps(float hz) { return (uint32_t)(hz * STEPS_PER_REV + 0.5f); }

void startRun(float hz) {
  if (!enabled) { stepper->enableOutputs(); enabled = true; }
  stepper->setAcceleration((int32_t)(accelHzPerS * STEPS_PER_REV));
  stepper->setSpeedInHz(sps(hz));
  // laeuft schon im Dauerlauf -> nur neue Drehzahl uebernehmen, sonst neu starten
  if (stepper->isRunning() && stepper->isRunningContinuously()) stepper->applySpeedAcceleration();
  else if (forward) stepper->runForward();
  else stepper->runBackward();
}

void stopSoft() { sweepActive = false; stepper->stopMove(); }

// ---------------- Befehle ----------------
String handleCommand(String line) {
  line.trim();
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  float val = sp < 0 ? NAN : line.substring(sp + 1).toFloat();
  cmd.toLowerCase();
  char buf[120];

  if (cmd == "stop") { stopSoft(); return "Bremst sanft ab."; }
  if (cmd == "notstop") {
    sweepActive = false;
    // forceStop() alleine sperrt in FastAccelStepper 2.x jeden weiteren Dauerlauf bis zum Reset.
    // forceStopAndNewPosition() setzt den Rampengenerator dagegen sauber zurueck.
    stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    return "Sofort gestoppt.";
  }
  if (cmd == "run") {
    sweepActive = false;
    startRun(freqHz);
    snprintf(buf, sizeof buf, "Dauerlauf mit %.2f Hz (%lu U/min).", freqHz, (unsigned long)(freqHz * 60));
    return buf;
  }
  if (cmd == "f" && !isnan(val)) {
    freqHz = constrain(val, 0.1f, 25.0f);
    if (stepper->isRunning() && stepper->isRunningContinuously() && !sweepActive) startRun(freqHz);
    snprintf(buf, sizeof buf, "Frequenz %.2f Hz.", freqHz); return buf;
  }
  if (cmd == "a" && !isnan(val)) {
    accelHzPerS = constrain(val, 5.0f, 2000.0f);
    snprintf(buf, sizeof buf, "Hochlauf %.0f Hz/s.", accelHzPerS); return buf;
  }
  if (cmd == "i" && !isnan(val)) {
    currentMa = (uint16_t)constrain(val, 200.0f, (float)MAX_CURRENT_MA);
    applyDriver();
    snprintf(buf, sizeof buf, "Strom %u mA.", currentMa); return buf;
  }
  if (cmd == "mode") {
    spreadCycle = !spreadCycle; applyDriver();
    return spreadCycle ? "SpreadCycle (kraeftig, fuer hohe Drehzahl)." : "StealthChop (leise, nur fuer langsam).";
  }
  if (cmd == "dir") {
    if (stepper->isRunning()) return "Erst anhalten, dann Richtung wechseln.";
    forward = !forward; return forward ? "Richtung: vorwaerts." : "Richtung: rueckwaerts.";
  }
  if (cmd == "j" && !isnan(val)) {
    if (stepper->isRunning() && stepper->isRunningContinuously()) return "Laeuft gerade im Dauerlauf.";
    if (!enabled) { stepper->enableOutputs(); enabled = true; }
    stepper->setSpeedInHz(STEPS_PER_REV);           // 1 U/s
    stepper->setAcceleration(STEPS_PER_REV * 4);
    stepper->move((int32_t)(val * STEPS_PER_REV));
    snprintf(buf, sizeof buf, "Dreht %.2f Umdrehungen.", val); return buf;
  }
  if (cmd == "on")  { stepper->enableOutputs(); enabled = true; return "Motor bestromt (haelt fest)."; }
  if (cmd == "off") {
    if (stepper->isRunning()) return "Erst anhalten.";
    stepper->disableOutputs(); enabled = false; return "Motor stromlos (frei drehbar).";
  }
  if (cmd == "sweep") {
    sweepActive = true; sweepHz = SWEEP_START; sweepNextMs = millis() + SWEEP_HOLD_MS;
    startRun(sweepHz);
    return "Drehzahltest: 2 -> 20 Hz in 2-Hz-Stufen, je 2 s. Hoer hin, ab wann er stottert.";
  }
  return "Unbekannter Befehl.";
}

// ---------------- Webseite ----------------
const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Motortest</title>
<style>
*{box-sizing:border-box}body{margin:0;font:15px system-ui,sans-serif;background:#f4f5f7;color:#1d2330}
header{padding:12px 16px;background:#1d2330;color:#fff;display:flex;justify-content:space-between;align-items:center}
#st{padding:3px 10px;border-radius:99px;background:#374151;font-size:13px}
main{max-width:640px;margin:auto;padding:12px;display:grid;gap:12px}
.card{background:#fff;border-radius:12px;padding:14px;box-shadow:0 1px 3px #0001}
h2{margin:0 0 10px;font-size:13px;text-transform:uppercase;letter-spacing:.04em;color:#6b7280}
.big{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;text-align:center}
.big div{background:#f4f5f7;border-radius:10px;padding:8px}.big span{display:block;font-size:24px;font-weight:700}.big small{color:#6b7280}
.row{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0}
button{flex:1;min-width:110px;padding:13px;border:0;border-radius:10px;background:#2563eb;color:#fff;font-size:15px;font-weight:600}
.sec{background:#e5e7eb;color:#1d2330;min-width:70px}.ok{background:#16a34a}.wa{background:#d97706}.stop{background:#dc2626;font-size:17px}
label{display:block;margin:10px 0 2px;font-weight:600}input[type=range]{width:100%}
.val{float:right;font-weight:700}#msg{font:13px ui-monospace,monospace;color:#6b7280;min-height:1.2em}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:14px}.kv span:nth-child(odd){color:#6b7280}
.bad{color:#dc2626;font-weight:700}.good{color:#16a34a;font-weight:700}
</style></head><body>
<header><b>Motortest</b><span id="st">…</span></header><main>
<div class="card"><h2>Live</h2><div class="big">
<div><span id="ist">–</span><small>Ist (Hz)</small></div>
<div><span id="rpm">–</span><small>U/min</small></div>
<div><span id="pos">–</span><small>Umdrehungen</small></div></div></div>

<div class="card"><h2>Fahren</h2>
<div class="row"><button class="stop" onclick="c('notstop')">NOT-STOP</button><button class="wa" onclick="c('stop')">Sanft anhalten</button></div>
<div class="row"><button class="ok" onclick="c('run')">Dauerlauf starten</button><button onclick="c('sweep')">Drehzahltest 2→20 Hz</button></div>
<label>Frequenz <span class="val" id="fv"></span></label>
<input id="f" type="range" min="0.5" max="25" step="0.5" oninput="fv.textContent=this.value+' Hz'" onchange="c('f '+this.value)">
<label>Hochlauf <span class="val" id="av"></span></label>
<input id="a" type="range" min="10" max="1000" step="10" oninput="av.textContent=this.value+' Hz/s'" onchange="c('a '+this.value)">
<div class="row"><button class="sec" onclick="c('j -1')">−1 U</button><button class="sec" onclick="c('j -0.25')">−¼ U</button>
<button class="sec" onclick="c('j 0.25')">+¼ U</button><button class="sec" onclick="c('j 1')">+1 U</button></div>
<div class="row"><button class="sec" onclick="c('dir')">Richtung wechseln</button></div>
<div id="msg"></div></div>

<div class="card"><h2>Treiber</h2>
<label>Motorstrom <span class="val" id="iv"></span></label>
<input id="i" type="range" min="200" max="1400" step="50" oninput="iv.textContent=this.value+' mA'" onchange="c('i '+this.value)">
<div class="row"><button class="sec" onclick="c('mode')">SpreadCycle / StealthChop</button></div>
<div class="row"><button class="sec" onclick="c('on')">Motor bestromt</button><button class="sec" onclick="c('off')">Motor stromlos</button></div>
<div class="kv" id="drv"></div></div>
</main><script>
const $=id=>document.getElementById(id);let busy=false;
async function c(x){const r=await fetch('/cmd?c='+encodeURIComponent(x));$('msg').textContent=await r.text();poll();}
function setR(id,v,u){const e=$(id);if(document.activeElement!==e){e.value=v;$(id+'v').textContent=v+u;}}
async function poll(){if(busy)return;busy=true;try{const s=await(await fetch('/status')).json();
$('st').textContent=s.running?(s.sweep?'Drehzahltest '+s.sweepHz+' Hz':'läuft'):'steht';
$('ist').textContent=s.hz.toFixed(2);$('rpm').textContent=Math.round(s.hz*60);$('pos').textContent=s.rev.toFixed(2);
setR('f',s.f,' Hz');setR('a',s.a,' Hz/s');setR('i',s.i,' mA');
const F=(b,t,f)=>b?`<span class="bad">${t}</span>`:`<span class="good">${f}</span>`;
$('drv').innerHTML=`<span>UART</span>${s.uart?'<span class="good">OK</span>':'<span class="bad">keine Verbindung – 1 kΩ / PDN_UART prüfen</span>'}
<span>Modus</span><span>${s.spread?'SpreadCycle':'StealthChop'}</span><span>Strom (eingestellt)</span><span>${s.irms} mA</span>
<span>Temperatur</span>${F(s.ot||s.otpw,s.ot?'ÜBERHITZT':'Vorwarnung',"ok")}
<span>Kurzschluss</span>${F(s.s2g,'JA – Verkabelung prüfen!','nein')}
<span>Spule offen</span>${F(s.ol&&s.running,'JA – Motorkabel prüfen!','nein')}
<span>Motor</span><span>${s.en?'bestromt':'stromlos'} · ${s.fwd?'vorwärts':'rückwärts'}</span>`;
}catch(e){$('st').textContent='keine Verbindung';}busy=false;}
setInterval(poll,500);poll();
</script></body></html>)HTML";

bool drvUart = false, drvOt = false, drvOtpw = false, drvS2g = false, drvOl = false;
uint16_t drvIrms = 0;
uint32_t drvCheckedMs = 0;

void checkDriver() {
  drvUart = driver.test_connection() == 0;
  if (drvUart) {
    drvOt = driver.ot(); drvOtpw = driver.otpw();
    drvS2g = driver.s2ga() || driver.s2gb();
    drvOl = driver.ola() || driver.olb();
    drvIrms = driver.rms_current();
  }
  drvCheckedMs = millis();
}

void webStatus() {
  if (millis() - drvCheckedMs > 1000) checkDriver();
  float hz = stepper->getCurrentSpeedInMilliHz() / 1000.0f / STEPS_PER_REV;
  float rev = (float)stepper->getCurrentPosition() / STEPS_PER_REV;
  char buf[420];
  snprintf(buf, sizeof buf,
    "{\"running\":%s,\"sweep\":%s,\"sweepHz\":%.0f,\"hz\":%.3f,\"rev\":%.3f,\"f\":%.1f,\"a\":%.0f,\"i\":%u,"
    "\"spread\":%s,\"en\":%s,\"fwd\":%s,\"uart\":%s,\"ot\":%s,\"otpw\":%s,\"s2g\":%s,\"ol\":%s,\"irms\":%u}",
    stepper->isRunning() ? "true" : "false", sweepActive ? "true" : "false", sweepHz, fabsf(hz), rev,
    freqHz, accelHzPerS, currentMa, spreadCycle ? "true" : "false", enabled ? "true" : "false",
    forward ? "true" : "false", drvUart ? "true" : "false", drvOt ? "true" : "false",
    drvOtpw ? "true" : "false", drvS2g ? "true" : "false", drvOl ? "true" : "false", drvIrms);
  server.send(200, "application/json", buf);
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Bremsdreirad MOTORTEST ===");

  Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  setupDriver();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  stepper->setDirectionPin(PIN_DIR);
  stepper->setEnablePin(PIN_EN);
  stepper->setAutoEnable(false);
  stepper->enableOutputs();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);
  server.on("/", [] { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/status", webStatus);
  server.on("/cmd", [] {
    lastMsg = handleCommand(server.arg("c"));
    Serial.println(lastMsg);
    server.send(200, "text/plain; charset=utf-8", lastMsg);
  });
  server.begin();

  checkDriver();
  Serial.printf("WLAN '%s' (Passwort %s) -> http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  Serial.printf("UART zum TMC2209: %s\n", drvUart ? "OK" : "FEHLER");
  Serial.println("Befehle auch hier: run, stop, notstop, f <Hz>, a <Hz/s>, i <mA>, j <Umdr.>, dir, mode, on, off, sweep");
}

void loop() {
  server.handleClient();

  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { if (line.length()) Serial.println(handleCommand(line)); line = ""; }
    else line += ch;
  }

  if (sweepActive && millis() >= sweepNextMs) {
    sweepHz += SWEEP_STEP;
    if (sweepHz > SWEEP_END) { stopSoft(); Serial.println("Drehzahltest fertig."); }
    else {
      startRun(sweepHz);
      sweepNextMs = millis() + SWEEP_HOLD_MS;
      Serial.printf("Drehzahltest: %.0f Hz\n", sweepHz);
    }
  }
}
