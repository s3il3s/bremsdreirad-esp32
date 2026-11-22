# Bremsdreirad – ESP32 + TMC2209 mit WLAN-Bedienung

## WLAN
Der ESP32 öffnet ein eigenes WLAN – kein Router nötig:
- **Name:** `Bremsdreirad` · **Passwort:** `physik123` (oben in `main.cpp` änderbar)
- Handy/Laptop verbinden, dann im Browser **http://192.168.4.1** öffnen
- Live: gemessene Drehzahl am Exzenter, Zustand beider Hallsensoren, Treiberstatus
- Knöpfe: STOP, Bremse öffnen, Referenzfahrt, Testlauf, Scharf (Rampe), Feinjustage ±10/±50 Schritte, „als offen speichern“
- Letzter Bremslauf als Diagramm (Ist- vs. Soll-Frequenz), Tabelle und CSV-Download

Hinweis: Das Handy meldet evtl. „kein Internet“ – trotzdem verbunden bleiben.

PlatformIO-Projekt. In VS Code: **Datei → Ordner öffnen** (diesen Ordner) → PlatformIO lädt die Bibliotheken automatisch.
Dann **Upload** (Pfeil unten in der Statusleiste) und **Serial Monitor** (Stecker-Symbol), 115200 Baud.

## Verkabelung

| ESP32 | TMC2209-Modul | Hinweis |
|---|---|---|
| GPIO 25 | STEP | |
| GPIO 26 | DIR | |
| GPIO 27 | EN | LOW = an |
| GPIO 17 (TX2) | PDN_UART | **über 1 kΩ** |
| GPIO 16 (RX2) | PDN_UART | direkt |
| 3V3 | VIO / VDD | Logikversorgung |
| GND | GND (beide) | gemeinsame Masse mit Akku |
| – | MS1, MS2 → GND | UART-Adresse 0 |
| – | VM ← Akku + (über Schalter) | **100 µF Elko direkt an VM/GND** |
| – | A1 A2 B1 B2 → Motor | Spulenpaare mit Multimeter prüfen |

| ESP32 | Sensor |
|---|---|
| GPIO 32 | A3144 am Exzenter (OUT) |
| GPIO 33 | A3144 am Rampenende (OUT) |
| 3V3 / GND | VCC / GND beider Sensoren, OUT je 10 kΩ nach 3V3 |

ESP32 bekommt 5 V über den Step-down-Wandler an VIN.
**Niemals den Motor ab- oder anstecken, solange VM anliegt** – das zerstört den Treiber.

## Erste Inbetriebnahme (auf dem Tisch, Bremse zunächst ausgehängt)

1. `status` → muss „UART-Verbindung: OK“ zeigen.
2. `i 800` → vorsichtig mit wenig Strom starten.
3. `home` → Exzenter dreht langsam bis zum Magneten.
4. Mit `j 50` / `j -50` den Exzenter in die Stellung „Bremse ganz gelöst“ drehen, dann `cal`.
5. `f 5` und `run` → 3 s Lauf, danach kommt das Log mit echter Frequenz pro Umdrehung.
6. Frequenz schrittweise bis `f 20` erhöhen. Steht in der Spalte `schrittfehler` mehr als ±80, verliert der Motor Schritte.
7. Bremse einhängen und Schritt 5–6 wiederholen. Strom ggf. bis `i 1400` erhöhen (Kühlkörper!).

## Messung

`arm` → Dreirad auf die Rampe; der Magnet am Rampenende startet den Bremslauf.
Das Log (CSV, Trennzeichen `;`) aus dem Seriellen Monitor in eine Tabelle kopieren.
Nach jedem Lauf fährt der Exzenter automatisch neu in die Referenz und in die gelöste Stellung.

Einstellungen (`f`, `a`, `t`, `i`, `cal`) bleiben nach dem Ausschalten erhalten.
