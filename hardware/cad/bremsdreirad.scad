// =====================================================================
//  Bremsdreirad – Versuch "ideale Bremsfrequenz"
//  Parametrisches OpenSCAD-Modell
//
//  Antrieb der Bremse: NEMA17 -> Exzenter (Lager 6804) -> Pleuel mit
//  Federhuelse -> Betaetigungshebel des Scooter-Bremssattels
//  Bremsscheibe: 110 mm Scooter-Scheibe (Xiaomi M365-Bohrbild, 5 Loecher)
//  Steuerung: ESP32 + Schrittmotortreiber auf Lochraster-Platine
//
//  Koordinaten: x = Fahrtrichtung (vorne +), y = links, z = oben,
//  Boden z = 0, Hinterachse bei x = 0.
//
//  Einzelteile exportieren:  part = "rad_hinten"  usw. (siehe unten)
// =====================================================================

part = "assembly";   // assembly | rad_hinten | rad_vorne | reifen | hall_arm |
                     // grundplatte_hinten | grundplatte_vorne |
                     // gabel_links | gabel_rechts | achsbock_vorne |
                     // motorhalter | exzenter | pleuel |
                     // elektronik_traeger | akku_wanne | hallsensor_halter |
                     // distanz_lang | distanz_kurz

$fn = 72;

// ---------------- Raeder ----------------
wheel_d      = 150;   // Aussendurchmesser mit Reifen
belag        = "schlauch"; // "schlauch" = aufgeklebter Fahrradschlauch, "tpu" = gedruckter Reifen
schlauch_t   = 1.0;   // Dicke Schlauchgummi (MESSEN! meist 0,8-1,2 mm)
riffel       = true;  // flache Rillen im Felgenbett, damit der Kleber besser haelt
tire_t       = belag == "schlauch" ? schlauch_t : 5;   // Belagdicke
lip_h        = belag == "schlauch" ? 0.5 : 2;          // Hoehe Bordkante ueber Felgenbett
lip_w        = 2;     // Breite Bordkante -> Felgenbett = wheel_w - 2*lip_w = 18 mm
wheel_w      = 22;    // Breite Felge
bearing_od   = 22;    // 608er Kugellager 8x22x7
bearing_w    = 7;
axle_d       = 8;     // M8-Schraube als Achse

// ---------------- Bremsscheibe (MESSEN!) ----------------
disc_d          = 110;
disc_t          = 1.8;
disc_bolts      = 5;      // M365: 5 Loecher
disc_bolt_pcd   = 42.5;   // Lochkreis (M365: 25 mm Nachbarloch / 40 mm diagonal)
disc_center_d   = 29;     // Innenloch der Scheibe (manche Scheiben: 38)
disc_offset     = 22;     // Abstand Radmitte -> Scheibe (Platz fuer den Sattel)

// ---------------- Bremssattel (MESSEN / ANPASSEN!) ----------------
caliper_angle   = 150;    // Lage um die Achse (0 = vorne, 90 = oben)
caliper_r       = 55;     // Radius Mitte Sattelbefestigung
caliper_hole_dist = 51;   // Lochabstand der Sattel-Befestigung
caliper_slot    = 14;     // Langloch-Laenge (+/- 7 mm Ausgleich)
caliper_bolt_d  = 6.4;    // M6 (bei M5: 5.5)

// ---------------- Fahrwerk ----------------
wheelbase   = 200;
plate_t     = 8;
plate_z     = 40;         // Unterkante Grundplatte (Bodenfreiheit)
plate_w     = 120;
grid        = 20;         // M4-Lochraster der Grundplatte
hole_m4     = 4.5;
fork_l_in   = 38;         // Innenseite Gabelplatte Scheibenseite (+y)
fork_r_in   = -14;        // Innenseite Gabelplatte andere Seite (-y)
fork_t      = 8;

// ---------------- Motor / Exzenter / Pleuel ----------------
motor_x     = -136;
motor_z     = 110;        // Hoehe Motorwelle
rod_y       = 30;         // Ebene von Exzenter und Pleuel
eccentric_e = 4;          // Exzentrizitaet -> Hub = 2*e (Varianten 2..6 drucken)
brg2_od     = 32;         // Lager 6804 (20x32x7) als Pleuelauge
brg2_id     = 20;
brg2_w      = 7;
lever_x     = -72;        // Angriffspunkt am Sattelhebel (nur Darstellung)
nema        = 42.3;
// Drehzahl-/Phasensensor am Exzenter
magnet_d    = 5;          // Neodym-Scheibe 5 x 2 mm
magnet_h    = 2;
magnet_r    = 8;          // Abstand Magnet -> Wellenachse
face_top    = motor_z + 42;   // Oberkante Motorhalter

R  = wheel_d/2;
pz = plate_z + plate_t;   // Oberkante Grundplatte
eps = 0.01;

// =====================================================================
//  Hilfsfunktionen
// =====================================================================
module xz(t) rotate([90,0,0]) linear_extrude(t) children();   // Profil in x-z, Dicke nach -y
module at_y(y0, t) translate([0, y0 + t, 0]) xz(t) children(); // belegt y0..y0+t
module m4_nut(h=3.5) cylinder(r=4.1, h=h, $fn=6);
module m3_nut(h=2.6) cylinder(r=3.2, h=h, $fn=6);
module slot2d(len, d) hull() { translate([-len/2,0]) circle(d=d); translate([len/2,0]) circle(d=d); }

// =====================================================================
//  RAD (Felge) – Achse entlang z gebaut, mit_scheibe = Nabenflansch fuer Bremsscheibe
// =====================================================================
module rim(with_disc=true) {
    // Druckausrichtung: Speichenscheibe liegt unten (z = -wheel_w/2), keine Stuetzen noetig
    rim_ro = R - tire_t;           // 70
    rim_ri = rim_ro - 10;          // 60
    hub_r  = 16;
    zb     = -wheel_w/2;
    top    = with_disc ? disc_offset : wheel_w/2;
    seat2  = (with_disc ? top + disc_t : wheel_w/2) - bearing_w;   // oberer Lagersitz
    difference() {
        union() {
            // Felgenring mit Bordkanten
            translate([0,0,zb]) difference() {
                union() {
                    cylinder(r=rim_ro, h=wheel_w);
                    cylinder(r=rim_ro+lip_h, h=lip_w);
                    translate([0,0,wheel_w-lip_w]) cylinder(r=rim_ro+lip_h, h=lip_w);
                }
                translate([0,0,-1]) cylinder(r=rim_ri, h=wheel_w+2);
                // Rillen im Felgenbett (0,3 mm tief) fuer besseren Halt des Klebers
                if (riffel && belag == "schlauch")
                    for (z=[lip_w+2 : 3 : wheel_w-lip_w-2]) translate([0,0,z])
                        difference() { cylinder(r=rim_ro+1, h=1); translate([0,0,-1]) cylinder(r=rim_ro-0.3, h=3); }
            }
            // Speichenscheibe (unten)
            translate([0,0,zb]) difference() {
                cylinder(r=rim_ri+1, h=4);
                for (a=[0:60:359]) rotate(a+30) translate([47,0,-1]) cylinder(r=9, h=6);
            }
            // Nabe bzw. Scheibenflansch
            translate([0,0,zb]) cylinder(r=with_disc ? 33 : hub_r, h=top - zb);
            if (with_disc) translate([0,0,top-eps]) cylinder(d=disc_center_d-0.4, h=disc_t-0.3);
        }
        // Achsbohrung + Hohlraum zwischen den Lagern
        translate([0,0,-50]) cylinder(d=axle_d+2.5, h=100);
        translate([0,0,zb+bearing_w-eps]) cylinder(d=16, h=seat2-zb-bearing_w+2*eps);
        // Lagersitze 608
        translate([0,0,zb-eps]) cylinder(d=bearing_od+0.15, h=bearing_w);
        translate([0,0,seat2]) cylinder(d=bearing_od+0.15, h=bearing_w+5);
        // Scheibenschrauben M5 (M5x12), Muttern von unten einschieben
        if (with_disc) for (i=[0:disc_bolts-1]) rotate(i*360/disc_bolts) translate([disc_bolt_pcd/2,0,0]) {
            translate([0,0,top-9]) cylinder(d=5.4, h=12, $fn=24);
            translate([0,0,zb-1]) rotate(30) cylinder(r=4.75, h=top-8-zb+1, $fn=6);
        }
    }
}

module tire() {
    if (belag == "schlauch")   // nur Darstellung: aufgeklebter Schlauchstreifen
        difference() {
            cylinder(r=R, h=wheel_w-2*lip_w, center=true);
            cylinder(r=R-tire_t, h=wheel_w, center=true);
        }
    else difference() {
        cylinder(r=R, h=wheel_w-4, center=true);
        cylinder(r=R-tire_t-0.3, h=wheel_w, center=true);
        // Profilrillen
        for (a=[0:7.5:359]) rotate(a) translate([R,0,0]) cube([1.6,1.2,wheel_w], center=true);
    }
}

// =====================================================================
//  GRUNDPLATTE (2 Teile, ueberlappt bei x = 20..60 und verschraubt)
// =====================================================================
x_rear  = -160;  x_split0 = 20;  x_split1 = 60;  x_front = 240;

module grid_holes(x0, x1) {
    for (x=[-200:grid:300]) for (y=[-plate_w/2+grid/2 : grid : plate_w/2])
        if (x > x0+6 && x < x1-6) translate([x, y, -50]) cylinder(d=hole_m4, h=100, $fn=20);
}

module wheel_slot() {
    // Durchbruch fuer Hinterrad, Nabenflansch und Bremsscheibe
    hull() { translate([-74,-13,-50]) cube([148, 27, 100]); }
    hull() { translate([-50,-13,-50]) cube([100, 13+disc_offset+disc_t+3, 100]); }
}

module base_plate(rear=true) {
    x0 = rear ? x_rear : x_split0;
    x1 = rear ? x_split1 : x_front;
    difference() {
        translate([0,0,plate_z]) linear_extrude(plate_t)
            offset(r=10) offset(delta=-10) translate([x0, -plate_w/2]) square([x1-x0, plate_w]);
        // Ueberlappung: halbe Dicke abtragen
        if (rear)  translate([x_split0, -100, plate_z+plate_t/2]) cube([200, 200, 50]);
        else       translate([x_split1-200, -100, plate_z-50+plate_t/2]) cube([200, 200, 50]);
        if (rear) wheel_slot();
        grid_holes(x0, x1);
    }
}

// =====================================================================
//  HINTERRAD-GABEL
// =====================================================================
cal_c = [caliper_r*cos(caliper_angle), R + caliper_r*sin(caliper_angle)];   // (x,z)
cal_t = caliper_angle - 90;   // Richtung tangential

module fork_profile(with_caliper) {
    difference() {
        union() {
            hull() {
                translate([-70, pz]) square([120, 6]);
                translate([0, R]) circle(r=15);
            }
            if (with_caliper) hull() {
                translate([0, R]) circle(r=15);
                translate(cal_c) rotate(cal_t) offset(r=8) square([caliper_hole_dist + caliper_slot, 4], center=true);
                translate([-75, pz]) square([60, 6]);
            }
        }
        translate([0, R]) circle(d=axle_d+0.4);
        if (with_caliper) translate(cal_c) rotate(cal_t)
            for (s=[-1,1]) translate([s*caliper_hole_dist/2, 0]) slot2d(caliper_slot, caliper_bolt_d);
        // Gewicht sparen
        if (with_caliper) translate([-32, pz+20]) circle(r=8);
    }
}

module fork(left=true) {
    y_in  = left ? fork_l_in : fork_r_in;
    y0    = left ? y_in : y_in - fork_t;
    hole_y = left ? 50 : -30;
    foot_y0 = left ? y_in : y_in - 26;
    foot_w  = left ? plate_w/2 - y_in : 26;
    difference() {
        union() {
            at_y(y0, fork_t) fork_profile(left);
            // Fussflansch nach aussen
            translate([-70, foot_y0, pz]) cube([120, foot_w, 6]);
            // Stuetzrippen
            for (x=[-50, 30]) translate([x-3, left ? y_in+fork_t : y_in-fork_t-14, pz])
                hull() { cube([6, 14, 1]); translate([0, left?0:14, 0]) cube([6, eps, 30]); }
        }
        for (x=[-60:grid:40]) translate([x, hole_y, 0]) cylinder(d=hole_m4, h=200, $fn=20);
    }
}

// Achsdistanzen (Rohr 12 x 8.4)
module spacer(l) difference() { cylinder(d=12, h=l); translate([0,0,-1]) cylinder(d=axle_d+0.4, h=l+2); }

// =====================================================================
//  VORDERACHS-BOCK (Stummelachse M8, je Seite einer)
// =====================================================================
module front_mount() {   // fuer +y-Seite gebaut, fuer -y spiegeln
    y_out = plate_w/2;
    difference() {
        union() {
            at_y(y_out-8, 8) hull() {
                translate([wheelbase-30, pz]) square([60, 6]);
                translate([wheelbase, R]) circle(r=13);
            }
            translate([wheelbase, y_out-16, R]) rotate([-90,0,0]) cylinder(r=11, h=16);
            translate([wheelbase-30, 22, pz]) cube([60, y_out-22, 6]);
            for (x=[wheelbase-25, wheelbase+19]) translate([x, y_out-8, pz])
                hull() { translate([0,-20,0]) cube([6,20,1]); cube([6, eps, 22]); }
        }
        translate([wheelbase, 0, R]) rotate([-90,0,0]) cylinder(d=axle_d+0.4, h=200);
        translate([wheelbase, y_out-16-eps, R]) rotate([-90,0,0]) rotate(30) cylinder(r=7.6, h=6.5, $fn=6); // M8-Mutter
        for (x=[wheelbase-20, wheelbase+20]) translate([x, 30, 0]) cylinder(d=hole_m4, h=200, $fn=20);
    }
}

// =====================================================================
//  MOTORHALTER NEMA17
// =====================================================================
bracket_y1 = rod_y - brg2_w/2 - 2 - 6 - 1;    // Aussenseite Motorflansch-Platte
bracket_t  = 5;

module motor_bracket() {
    difference() {
        union() {
            at_y(bracket_y1 - bracket_t, bracket_t)
                translate([motor_x - 24, pz]) square([48, face_top - pz]);
            translate([motor_x - 24, -35, pz]) cube([48, bracket_y1 + 35, 6]);
            for (x=[motor_x-24, motor_x+18]) translate([x, bracket_y1-bracket_t, pz])
                hull() { translate([0,-28,0]) cube([6,28,1]); translate([0,-eps,0]) cube([6,eps, motor_z-pz-24]); }
        }
        // Motorflansch: Zentrierbund 22 mm, Lochbild 31 mm M3
        translate([motor_x, 0, motor_z]) rotate([-90,0,0]) {
            cylinder(d=23, h=200, center=true);
            for (sx=[-1,1], sz=[-1,1]) translate([sx*15.5, sz*15.5, 0]) cylinder(d=3.4, h=200, center=true, $fn=20);
        }
        // Senkungen fuer flache M3-Koepfe (DIN 7984), damit das Pleuelauge frei laeuft
        for (sx=[-1,1], sz=[-1,1]) translate([motor_x + sx*15.5, bracket_y1 - 3, motor_z + sz*15.5])
            rotate([-90,0,0]) cylinder(d=6.4, h=10, $fn=24);
        // Befestigung Hallsensor-Arm (2x M3)
        for (sx=[-7,7]) translate([motor_x + sx, 0, motor_z + 30]) rotate([-90,0,0]) cylinder(d=3.4, h=200, center=true, $fn=20);
        // Langloecher zum Verschieben in x
        for (y=[-10, -30]) translate([motor_x, y, pz-1]) linear_extrude(10) slot2d(20, hole_m4);
    }
}

module nema17() {
    translate([motor_x, bracket_y1 - bracket_t, motor_z]) rotate([90,0,0]) {
        translate([-nema/2, -nema/2, 0]) cube([nema, nema, 40]);
        translate([0,0,-2]) cylinder(d=22, h=2);
    }
    translate([motor_x, bracket_y1 - bracket_t - 2, motor_z]) rotate([-90,0,0]) cylinder(d=5, h=24);
}

// =====================================================================
//  EXZENTER (fuer Lager 6804, 20x32x7)  -> Hub = 2 * eccentric_e
// =====================================================================
module eccentric(e=eccentric_e) {
    // gebaut entlang z: Nabe (Madenschraube) unten, Lagersitz oben
    difference() {
        union() {
            hull() {                                               // Klemmnabe + Anlaufbund (stuetzfrei)
                cylinder(d=16, h=6);
                translate([e,0,6]) cylinder(d=brg2_id+4, h=2);
            }
            translate([e,0,8]) cylinder(d=brg2_id-0.05, h=brg2_w);// Lagersitz (Pressitz)
        }
        // D-Welle 5 mm
        difference() {
            translate([0,0,-1]) cylinder(d=5.15, h=30, $fn=32);
            translate([2.0, -5, -2]) cube([5, 10, 40]);
        }
        // Madenschraube M3 + Muttertasche
        translate([0,0,3]) rotate([0,90,0]) cylinder(d=3.2, h=10, $fn=16);
        translate([4.2,0,3]) rotate([0,90,0]) rotate(30) hull() { m3_nut(); translate([-5,0,0]) m3_nut(); }
        // Magnettasche (Stirnseite, Abstand magnet_r zur Wellenachse)
        translate([magnet_r*cos(60), magnet_r*sin(60), 8+brg2_w-magnet_h-0.2]) cylinder(d=magnet_d+0.2, h=5, $fn=32);
        // Markierung der Exzentrizitaet
        translate([e,0,8+brg2_w-0.6]) linear_extrude(1) text(str("e",e), size=4, halign="center", valign="center");
    }
}

// =====================================================================
//  HALLSENSOR-ARM am Motorhalter: A3144 (TO-92) schaut axial auf den Magneten
//  in der Stirnseite des Exzenters -> 1 Impuls pro Umdrehung
// =====================================================================
arm_w = 24;
module hall_arm() {
    y0 = bracket_y1;            // Anlageflaeche am Motorhalter
    yd = rod_y + brg2_w/2 + 1.5;// Vorderseite Sensor (1,5 mm Luft zur Exzenter-Stirnseite)
    zt = face_top;
    difference() {
        translate([motor_x - arm_w/2, 0, 0]) rotate([90,0,90]) linear_extrude(arm_w) polygon([
            [y0, motor_z+28], [y0+4, motor_z+28], [y0+4, zt-6], [yd, zt-6],
            [yd, motor_z+3], [yd+5, motor_z+3], [yd+5, zt], [y0, zt]]);
        // Schrauben M3 in den Motorhalter
        for (sx=[-7,7]) translate([motor_x + sx, y0-1, motor_z + 30]) rotate([-90,0,0]) cylinder(d=3.4, h=10, $fn=20);
        // TO-92-Tasche, flache Seite zeigt zum Magneten
        translate([motor_x - 2.6, yd - eps, motor_z + magnet_r - 2.3]) cube([5.2, 4, 4.6]);
        // Kabelrille nach oben
        translate([motor_x - 1.6, yd - eps, motor_z + magnet_r]) cube([3.2, 2.2, 50]);
    }
}

// =====================================================================
//  PLEUEL mit Federhuelse
//   Ziehen (Bremsen):  Huelsendeckel drueckt Feder gegen Mutter -> Kraft ueber Feder
//   Schieben (Loesen): Huelsenboden schiebt Mutter -> zwangsweise Rueckstellung
// =====================================================================
rod_cc      = lever_x - motor_x;    // Mittenabstand Exzenter -> Hebel (Darstellung)
sleeve_len  = 28;
rod_d       = 5;                    // Gewindestange M5
sleeve_id   = 10.5;                 // Federraum: Druckfeder Aussen-D 8-10, Innen-D >= 5,5 (passt ueber M5)
sleeve_od   = 16;

module conrod() {
    // flach gebaut in x-y, Dicke z = h.  Lager 6804 sitzt in z 0..7 auf einer Schulter.
    h  = 14;
    eye_od = brg2_od + 8;
    s0 = eye_od/2 - 2;               // Beginn Huelse
    zc = 7;                          // Mittelebene Federhuelse
    difference() {
        union() {
            cylinder(d=eye_od, h=h);
            translate([s0, -sleeve_od/2, 0]) cube([sleeve_len, sleeve_od, h]);
        }
        // Lagerauge mit Schulter
        translate([0,0,-1]) cylinder(d=brg2_od+0.1, h=brg2_w+1);
        translate([0,0,-1]) cylinder(d=brg2_od-6, h=h+2);
        // Federraum + Fenster nach oben (Feder, Scheibe, Mutter einlegen; Federweg sichtbar)
        translate([s0+4, 0, zc]) rotate([0,90,0]) cylinder(d=sleeve_id, h=sleeve_len-7);
        translate([s0+4, -sleeve_id/2, zc]) cube([sleeve_len-7, sleeve_id, h]);
        // Durchgang Gewindestange im Deckel
        translate([s0, 0, zc]) rotate([0,90,0]) cylinder(d=rod_d+0.5, h=sleeve_len+5, $fn=24);
    }
}

// =====================================================================
//  ELEKTRONIK
// =====================================================================
// Traeger fuer 50x70-Lochrasterplatine (ESP32 + Treiber + Klemmen)
module electronics_tray() {
    difference() {
        union() {
            translate([0,0,0]) cube([84, 64, 4]);
            for (x=[8, 8+66], y=[9, 9+46]) translate([x, y, 0]) cylinder(d=7, h=10);
        }
        for (x=[8, 8+66], y=[9, 9+46]) translate([x, y, 1]) cylinder(d=2.5, h=20, $fn=16);
        // Befestigung am Raster (Lochabstand 20)
        for (x=[22, 62], y=[18, 38]) translate([x, y, -1]) cylinder(d=hole_m4, h=10, $fn=20);
        translate([30, 22, -1]) cube([24, 12, 10]);
    }
}

// Akku-Wanne mit Klettband-Schlitzen
akku = [76, 35, 24];   // z.B. 3S LiPo 1000-1300 mAh (Masse anpassen!)
module battery_cradle() {
    difference() {
        translate([-3,-3,0]) cube([akku[0]+6, akku[1]+6, 12]);
        translate([0,0,3]) cube([akku[0], akku[1], 20]);
        for (x=[8, akku[0]-12]) translate([x, -10, 1]) cube([4, akku[1]+20, 1.6]);  // Klettband
        for (x=[15, 55]) translate([x, akku[1]/2, -1]) cylinder(d=hole_m4, h=10, $fn=20);
    }
}

// Hallsensor (A3144 / KY-003, TO-92) unter der Platte, Abstand zum Boden ~8 mm
sensor_gap = 8;
module hall_holder() {
    hh = plate_z - sensor_gap;
    difference() {
        union() {
            translate([-8, -18, 0]) cube([16, 36, 5]);
            translate([-6, -6, 0]) cube([12, 12, hh]);
        }
        for (y=[-10, 10]) translate([0, y, -1]) cylinder(d=hole_m4, h=10, $fn=20);
        // Kabelkanal
        translate([0,0,-1]) cylinder(d=4, h=hh+2, $fn=16);
        // TO-92-Tasche
        translate([-2.6, -2.2, hh-3.2]) cube([5.2, 4.4, 4]);
    }
}

// =====================================================================
//  Platzhalter (nur Darstellung): Bremssattel + Hebel, Scheibe, Schrauben
// =====================================================================
module disc_dummy() {
    color("silver") translate([0, disc_offset, R]) rotate([-90,0,0]) difference() {
        cylinder(d=disc_d, h=disc_t);
        translate([0,0,-1]) cylinder(d=disc_center_d, h=5);
        for (i=[0:disc_bolts-1]) rotate(i*72) translate([disc_bolt_pcd/2,0,-1]) cylinder(d=5, h=5);
        for (i=[0:7]) rotate(i*45+20) translate([38,0,-1]) cylinder(d=7, h=5);
    }
}
module caliper_dummy() {
    color([0.15,0.15,0.15,0.85]) translate([cal_c[0], disc_offset - 9, cal_c[1]])
        rotate([0, -cal_t, 0]) translate([-24, 0, -14]) cube([48, fork_l_in - disc_offset + 9, 28]);
    // Hebel
    color("orange") hull() {
        translate([cal_c[0]-10, rod_y-3.5, cal_c[1]+10]) rotate([90,0,0]) cylinder(d=8, h=4, center=true);
        translate([lever_x, rod_y-3.5, motor_z+eccentric_e]) rotate([90,0,0]) cylinder(d=8, h=4, center=true);
    }
}

// =====================================================================
//  ZUSAMMENBAU
// =====================================================================
only = "";   // fuer Export einzelner Baugruppen-Koerper in Einbaulage
module c(n, col) if (only == "" || only == n) color(col) children();

module assembly() {
    c("Rad_hinten", "SteelBlue") translate([0,0,R]) rotate([-90,0,0]) rim(true);
    c("Reifen_hinten", "DimGray") translate([0,0,R]) rotate([-90,0,0]) tire();
    for (s=[-1,1]) {
        yc = s*(plate_w/2 + 4 + wheel_w/2);
        c(s>0 ? "Rad_vorne_links" : "Rad_vorne_rechts", "SteelBlue") translate([wheelbase, yc, R]) rotate([-90,0,0]) rim(false);
        c(s>0 ? "Reifen_vorne_links" : "Reifen_vorne_rechts", "DimGray") translate([wheelbase, yc, R]) rotate([-90,0,0]) tire();
    }
    c("Bremsscheibe_Platzhalter", "silver") disc_dummy();
    c("Bremssattel_Platzhalter", "black") caliper_dummy();
    c("Grundplatte_hinten", "Gainsboro") base_plate(true);
    c("Grundplatte_vorne", "LightGray") base_plate(false);
    c("Gabel_links", "Tomato") fork(true);
    c("Gabel_rechts", "Tomato") fork(false);
    c("Distanz_lang", "gold") translate([0, disc_offset + disc_t, R]) rotate([-90,0,0]) spacer(fork_l_in - disc_offset - disc_t);
    c("Distanz_kurz", "gold") translate([0, fork_r_in, R]) rotate([-90,0,0]) spacer(-wheel_w/2 - fork_r_in);
    c("Achse_hinten_M8", "silver") translate([0, fork_r_in - fork_t - 10, R]) rotate([-90,0,0]) cylinder(d=axle_d, h=fork_l_in - fork_r_in + fork_t*2 + 20);
    c("Achsbock_vorne_links", "Tomato") front_mount();
    c("Achsbock_vorne_rechts", "Tomato") mirror([0,1,0]) front_mount();
    c("Motorhalter", "MediumSeaGreen") motor_bracket();
    c("NEMA17_Platzhalter", [0.25,0.25,0.3]) nema17();
    c("Exzenter", "gold") translate([motor_x, bracket_y1 + 1, motor_z]) rotate([-90,0,0]) rotate(-90) eccentric();
    c("Hallsensor_Arm_Motor", "MediumSeaGreen") hall_arm();
    c("Hallsensor_A3144_Platzhalter", "black") translate([motor_x - 2.25, rod_y + brg2_w/2 + 1.6, motor_z + magnet_r - 2.25]) cube([4.5, 3.6, 4.5]);
    c("Pleuel", "MediumOrchid") translate([motor_x, rod_y + brg2_w/2, motor_z + eccentric_e]) rotate([90,0,0]) conrod();
    c("Gewindestange_M5", "silver") translate([motor_x + 22, rod_y - 3.5, motor_z + eccentric_e]) rotate([0,90,0])
        cylinder(d=rod_d, h=rod_cc - 22 + 4);
    c("Elektronik_Traeger", "MediumSeaGreen") translate([78, -8, pz]) electronics_tray();
    c("Platine_Platzhalter", [0.1,0.4,0.15]) translate([78+4, -8+4, pz+10]) cube([76, 56, 1.6]);
    c("ESP32_Platzhalter", [0.1,0.1,0.12]) translate([88, 4, pz+11.6]) cube([52, 28, 3]);
    c("Akku_Wanne", "MediumSeaGreen") translate([85, -30-akku[1]/2, pz]) battery_cradle();
    c("Akku_Platzhalter", [0.2,0.3,0.6]) translate([85, -30-akku[1]/2, pz+3]) cube(akku);
    c("Hallsensor_Halter", "MediumSeaGreen") translate([220, 0, plate_z]) mirror([0,0,1]) hall_holder();
}

// =====================================================================
//  Ausgabe
// =====================================================================
if      (part == "assembly")           assembly();
else if (part == "rad_hinten")         rim(true);
else if (part == "rad_vorne")          rim(false);
else if (part == "reifen")             tire();
else if (part == "grundplatte_hinten") translate([0,0,-plate_z]) base_plate(true);
else if (part == "grundplatte_vorne")  rotate([180,0,0]) translate([0,0,-plate_z-plate_t]) base_plate(false);
else if (part == "gabel_links")        rotate([90,0,0]) translate([0,-fork_l_in,0]) fork(true);
else if (part == "gabel_rechts")       rotate([-90,0,0]) translate([0,-fork_r_in,0]) fork(false);
else if (part == "achsbock_vorne")     rotate([-90,0,0]) translate([0,-plate_w/2,0]) front_mount();
else if (part == "motorhalter")        rotate([-90,0,0]) translate([0,-bracket_y1,0]) motor_bracket();
else if (part == "hall_arm")           rotate([0,-90,0]) hall_arm();
else if (part == "exzenter")           eccentric();
else if (part == "pleuel")             conrod();
else if (part == "elektronik_traeger") electronics_tray();
else if (part == "akku_wanne")         battery_cradle();
else if (part == "hallsensor_halter")  hall_holder();
else if (part == "distanz_lang")       spacer(fork_l_in - disc_offset - disc_t);
else if (part == "distanz_kurz")       spacer(-wheel_w/2 - fork_r_in);
