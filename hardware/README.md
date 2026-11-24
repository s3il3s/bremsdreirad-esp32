# Bremsdreirad – 3D-Modell

Parametrisches OpenSCAD-Modell (`bremsdreirad.scad`) + fertige STL-Dateien (`stl/`).
Gesamtansicht: `part = "assembly"`. Einzelteil: z. B. `part = "pleuel"` setzen und als STL exportieren.

## Bevor ihr druckt: messen und Parameter anpassen

| Parameter | Standard | Was messen |
|---|---|---|
| `disc_bolt_pcd`, `disc_bolts` | 42.5 mm, 5 | Lochkreis der Scheibe (M365: 25 mm Nachbarloch / 40 mm diagonal) |
| `disc_center_d` | 29 mm | Innenloch der Scheibe (manche Scheiben: 38 mm) |
| `caliper_hole_dist`, `caliper_bolt_d` | 51 mm, M6 | Lochabstand + Schraubengröße am Bremssattel |
| `caliper_angle`, `caliper_r` | 150°, 55 mm | Lage des Sattels, so dass die Beläge mittig auf der Reibfläche sitzen |
| `fork_l_in` | 38 mm | Platz zwischen Scheibe und Gabelplatte (Sattelbreite) |
| `eccentric_e` | 4 mm | Exzentrizität -> Hub = 2·e (STLs für e = 2…6 liegen bei) |
| `akku` | 76×35×24 | Maße eures Akkus |

**Wichtig:** Den Bremssattel habe ich nur als Platzhalter-Block modelliert, da es viele verschiedene Scooter-Sättel gibt. Die Langlöcher (±7 mm) gleichen etwas aus. Wenn der Sattel eine andere Befestigung hat (z. B. radial statt axial verschraubt), muss `fork_profile()` angepasst werden.

## Druckteile (PLA/PETG, 3 Wände, 25–30 % Infill)

| Datei | Anzahl | Hinweis |
|---|---|---|
| rad_hinten | 1 | mit Scheibenflansch |
| rad_vorne | 2 | |
| reifen_tpu | (3) | nur falls ihr statt Fahrradschlauch TPU-Reifen wollt: in der .scad `belag = "tpu"` setzen und Räder neu exportieren |
| grundplatte_hinten / _vorne | je 1 | 220 × 120 mm, Druckbett ≥ 220 mm (sonst diagonal legen) |
| gabel_links (Sattelseite) / gabel_rechts | je 1 | |
| achsbock_vorne | 2 | zweites Teil im Slicer spiegeln |
| motorhalter | 1 | |
| exzenter_e2 … e6 | je 1 | zum Variieren des Hubs |
| pleuel | 1 | Lager von unten einpressen |
| hall_arm | 1 | Hallsensor-Halter am Motorhalter, auf der Seite liegend drucken |
| distanz_lang / distanz_kurz | je 1 | alternativ M8-Unterlegscheiben |
| elektronik_traeger, akku_wanne, hallsensor_halter | je 1 | |

Alle Teile sind so ausgerichtet, dass sie ohne Stützmaterial druckbar sind.

## Kaufteile

- 6× Kugellager 608 (8×22×7) für die Räder
- 1× Kugellager 6804 / 61804 (20×32×7) für das Pleuelauge
- 3× Schraube M8 als Achse (hinten M8×100, vorne M8×50) + Muttern
- 5× M5×12 + Muttern (Bremsscheibe; Muttern von unten in die Taschen schieben, evtl. mit einem Tropfen Kleber fixieren)
- M4-Schrauben + Muttern (Lochraster 20 mm), M3-Schrauben für den NEMA17, 1× M3-Madenschraube
- M4-Gewindestange ca. 60 mm, M4-Gabelkopf (Modellbau) für den Sattelhebel
- Druckfeder Ø ≤ 10 mm, ca. 20–25 mm lang (Federhülse im Pleuel)
- 2× M2,5-Schrauben für die Lochrasterplatine 50×70
- 2× Hallsensor A3144 (Rampenende + Exzenter) + Magnete
- 1× Neodym-Magnet 5×2 mm, in die Tasche an der Stirnseite des Exzenters kleben (Polung: A3144 schaltet auf Südpol)
- 4× M3×8 mit flachem Kopf (DIN 7984) für den NEMA17 – die Köpfe sitzen in Senkungen, damit das Pleuel frei läuft
- 2× M3×12 + Muttern für den Hallsensor-Arm
- Schrittmotortreiber (z. B. TMC2209 oder DRV8825), ESP32-DevKit, Akku 3S oder 24-V-Versorgung

## Funktion des Pleuels

Im Pleuel steckt eine M4-Gewindestange. Auf ihrem Ende sitzen Mutter und Unterlegscheibe, dazwischen liegt die Feder (bis zum Deckel).
- **Exzenter zieht (Bremsen):** Der Deckel drückt die Feder zusammen. Die Bremskraft wird durch die Feder begrenzt.
- **Exzenter schiebt (Lösen):** Der Hülsenboden schiebt die Mutter. Der Hebel wird dadurch zwangsweise zurückgestellt.
Mit einer zweiten Mutter außen am Deckel stellt ihr die Vorspannung ein.
Falls euer Sattel durch **Drücken** bremst, setzt ihr die Feder auf die andere Seite der Mutter.

## Elektronik-Hinweise

- 20 Hz = 1200 U/min. Das schafft ein NEMA17 nur mit 24 V und wenig Mikroschritten (1/4 oder 1/8). Für die Schrittpulse eignet sich die Bibliothek *FastAccelStepper* auf dem ESP32.
- Da der Exzenter starr gekoppelt ist, bremst das Rad, sobald der Motor dreht. Beim Signal vom Hallsensor muss der Motor also schnell beschleunigen (≈ 0,1 s). Die ersten Pulse haben deshalb noch eine niedrigere Frequenz. Loggt die Schritte mit Zeitstempel mit, dann kennt ihr den genauen Bremsverlauf.
- Hallsensor am Exzenter: 1 Impuls pro Umdrehung. Damit messt ihr die echte Bremsfrequenz, erkennt Schrittverluste (Schritte pro Impuls zählen) und fahrt vor dem Start die Ruhestellung „gelöst“ an. Den Ausgang an einen Interrupt-Pin des ESP32 mit Pull-up (10 kΩ) legen.
- Legt etwas Ballast über das Hinterrad (Gewindegrid nutzen). Das gebremste Rad braucht Last, sonst blockiert es sofort.

## Lauffläche aus Fahrradschlauch

Die Räder haben ein flaches Felgenbett (148 mm Ø, 18 mm breit) mit 0,5 mm hohen Bordkanten und flachen Rillen für den Kleber.
Mit 1 mm Schlauchgummi ergibt das wieder 150 mm Raddurchmesser.

1. Schlauch **längs** in Streifen von **18 mm** Breite schneiden.
2. Länge ca. **430–440 mm** (Umfang 465 mm, etwas kürzer, damit der Streifen gespannt sitzt).
3. Felgenbett und Gummi anrauen (Schleifpapier), mit Spiritus entfetten.
4. Mit **Kontaktkleber** (z. B. Pattex) oder Vulkanisierlösung **vollflächig** aufkleben, Enden schräg überlappend stoßen.
5. Ist euer Schlauch nicht 1 mm dick: `schlauch_t` in der .scad anpassen und Räder neu exportieren.
