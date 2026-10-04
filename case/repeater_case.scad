// Minimal case for the Heltec WiFi LoRa 32 V2 repeater and a 35 × 20.1 × 6.5 mm LiPo.
//
// The cell lies under the display end. The USB end stays open underneath so the
// SH1.25 plug and its wires fit. The PCB rests on side rails.
// No pin headers: a soldered header is taller than this lid.
//
// Seat the board against the antenna end so the window and the buttons line up.
// Slide the cell in from the USB end, under the two side lips, then plug it in
// and lay the board on the rails.
//
// Print base and lid in PLA or PETG, 0.2 mm layers, 3 perimeters, no supports.
// The lid is already flipped for printing (top face on the bed).
// Four M2×8 mm button-head screws, self-tapped into the end caps.
// The dotted hole is PRG (mode change, display on/off).
// With the USB port facing you, PRG is on the left.
//
// If the lid touches the display, raise top_clear.
// If the radio shield on the underside touches the cell, raise bottom_clear.

/* [Print] */
part = "both"; // [base, lid, both, assembly, preview]

/* [Board] */
pcb_l = 51.0;
pcb_w = 25.5;
pcb_t = 1.0;
bottom_clear = 4.8; // mm from cell top to PCB underside
top_clear = 7.0;    // mm above the PCB top, for the OLED and the ESP32 module

/* [Battery] */
bat_l = 35.0;
bat_w = 20.1;
bat_h = 6.5;

/* [Openings] */
// Centres are mm from the USB end of the PCB (x) and from the PRG edge (y).
oled_cx = 32.5;
oled_cy = 12.8;
oled_lx = 22.5; // along the board
oled_ly = 12.0; // across the board
prg_x = 4.4;
prg_y = 4.5;
rst_x = 4.4;
rst_y = 20.8;
btn_d = 6.4;
usb_w = 12.5;
usb_h = 8.0;
ant_w = 10.0;
ant_h = 4.5;

/* [Shell] */
wall = 1.8;
end_cap = 6.4;  // outer face to the PCB, each short end (holds an M2 screw)
floor_t = 1.8;
lid_t = 2.4;
lip_h = 3.0;
lip_t = 1.25;
lip_gap = 0.22;
corner_r = 3.2;
head_d = 3.8;   // M2 button head
head_h = 1.25;
pilot_d = 1.65;
clear_d = 2.15;
screw_inset_x = 2.7; // from the outer short face to the screw centre
screw_inset_y = 3.6; // from the outer long face

/* [Hidden] */
fn = 64;
play = 0.40;
bat_slack = 0.7;
bat_z_gap = 0.55;
bat_end_gap = 2.2;
rail_w = 1.6;
clip_reach = 2.8;
clip_t = 0.8;
pilot_depth = 7.5;

// ---- derived -------------------------------------------------------

pcb_x = end_cap;          // USB edge of the PCB
pcb_y = wall + play;      // PRG edge of the PCB
outer_l = end_cap + pcb_l + end_cap;
outer_w = pcb_y + pcb_w + play + wall;

cav_x = pcb_x - 0.12;
cav_y = wall;
cav_l = pcb_l + 0.12 + 0.28;
cav_w = outer_w - 2 * wall;

z_floor = floor_t;
z_pcb = z_floor + bat_h + bottom_clear;
z_top = z_pcb + pcb_t + top_clear;
base_h = z_top;
rail_h = z_pcb - z_floor;

bat_x1 = pcb_x + pcb_l - bat_end_gap;
bat_x0 = bat_x1 - bat_l - bat_slack;
bat_y0 = pcb_y + (pcb_w - bat_w) / 2;

module rounded_rect(l, w, h, r) {
    rr = min(r, min(l, w) / 2 - 0.01);
    hull()
        for (px = [rr, l - rr], py = [rr, w - rr])
            translate([px, py, 0])
                cylinder(h = h, r = rr, $fn = fn);
}

module screw_centers() {
    xs = [screw_inset_x, outer_l - screw_inset_x];
    ys = [screw_inset_y, outer_w - screw_inset_y];
    for (cx = xs, cy = ys)
        translate([cx, cy, 0])
            children();
}

module usb_cut() {
    // Jack on the PCB top. The mouth flares so a slightly recessed plug still fits.
    z0 = z_pcb + pcb_t - 1.2;
    y0 = pcb_y + pcb_w / 2 - usb_w / 2;
    translate([-1, y0, z0])
        cube([end_cap + 1.2, usb_w, usb_h]);
    hull() {
        translate([0.4, y0, z0])
            cube([0.2, usb_w, usb_h]);
        translate([-1.6, y0 - 1.5, z0 - 1.3])
            cube([0.2, usb_w + 3, usb_h + 2.5]);
    }
}

module ant_cut() {
    z0 = z_pcb + pcb_t - 0.8;
    y0 = pcb_y + pcb_w / 2 - ant_w / 2;
    translate([pcb_x + pcb_l - 0.4, y0, z0])
        cube([end_cap + 1, ant_w, ant_h]);
}

module button_holes() {
    for (p = [[prg_x, prg_y], [rst_x, rst_y]])
        translate([pcb_x + p[0], pcb_y + p[1], -lip_h - 1])
            cylinder(h = lid_t + lip_h + 2, d = btn_d, $fn = 40);
}

module oled_hole() {
    translate([pcb_x + oled_cx - oled_lx / 2,
               pcb_y + oled_cy - oled_ly / 2,
               -1])
        rounded_rect(oled_lx, oled_ly, lid_t + 2, 1.4);
}

module rails() {
    translate([cav_x, pcb_y, z_floor])
        cube([cav_l, rail_w, rail_h]);
    translate([cav_x, pcb_y + pcb_w - rail_w, z_floor])
        cube([cav_l, rail_w, rail_h]);
    translate([pcb_x + pcb_l - rail_w, pcb_y, z_floor])
        cube([rail_w + 0.2, pcb_w, rail_h]);
}

// Side lips the cell slides under. The underside is 45° so it prints without support.
module cell_clips() {
    z_tip = z_floor + bat_h + bat_z_gap;
    module one_lip(y_inner, sign) {
        y_tip = y_inner + sign * (clip_reach - 0.2);
        z_root = z_tip - clip_reach;
        module section(x, len) {
            hull() {
                translate([x, y_inner - (sign < 0 ? 0.2 : 0), z_root])
                    cube([len, 0.2, clip_reach + clip_t]);
                translate([x, y_tip, z_tip])
                    cube([len, 0.2, clip_t]);
            }
        }
        // Lead-in at the USB end, then a straight lip.
        hull() {
            translate([bat_x0, y_inner - (sign < 0 ? 0.2 : 0), z_tip])
                cube([0.2, 0.2, clip_t]);
            translate([bat_x0 + 7, y_inner - (sign < 0 ? 0.2 : 0), z_root])
                cube([0.2, 0.2, clip_reach + clip_t]);
            translate([bat_x0 + 7, y_tip, z_tip])
                cube([0.2, 0.2, clip_t]);
        }
        section(bat_x0 + 7, bat_x1 - bat_x0 - 8);
    }
    one_lip(pcb_y + rail_w, 1);
    one_lip(pcb_y + pcb_w - rail_w, -1);
}

module base() {
    difference() {
        union() {
            difference() {
                rounded_rect(outer_l, outer_w, base_h, corner_r);
                translate([cav_x, cav_y, z_floor])
                    rounded_rect(cav_l, cav_w, base_h, 1.15);
                usb_cut();
                ant_cut();
            }
            rails();
            cell_clips();
        }
        screw_centers()
            translate([0, 0, base_h - pilot_depth])
                cylinder(h = pilot_depth + 1, d = pilot_d, $fn = 28);
    }
}

module lip() {
    // Long sides and the antenna end. The USB end stays clear of the buttons.
    x0 = pcb_x + 10;
    y0 = cav_y + lip_gap;
    x1 = cav_x + cav_l - lip_gap;
    y1 = cav_y + cav_w - lip_gap;
    difference() {
        union() {
            translate([x0, y0, -lip_h])
                cube([x1 - x0, lip_t, lip_h]);
            translate([x0, y1 - lip_t, -lip_h])
                cube([x1 - x0, lip_t, lip_h]);
            translate([x1 - lip_t, y0, -lip_h])
                cube([lip_t, y1 - y0, lip_h]);
        }
        ant_cut();
    }
}

module lid() {
    difference() {
        union() {
            rounded_rect(outer_l, outer_w, lid_t, corner_r);
            lip();
        }
        screw_centers() {
            translate([0, 0, -1])
                cylinder(h = lid_t + 2, d = clear_d, $fn = 28);
            translate([0, 0, lid_t - head_h])
                cylinder(h = head_h + 1, d = head_d, $fn = 32);
        }
        button_holes();
        oled_hole();
        // Shallow mark: this hole is PRG.
        translate([pcb_x + prg_x + btn_d / 2 + 1.5,
                   pcb_y + prg_y,
                   lid_t - 0.4])
            cylinder(h = 0.5, d = 1.25, $fn = 24);
    }
}

module board_dummy() {
    pcb_top = z_pcb + pcb_t;
    color("steelblue")
        translate([pcb_x, pcb_y, z_pcb])
            cube([pcb_l, pcb_w, pcb_t]);
    color("dimgray")
        translate([pcb_x + oled_cx - 13.2, pcb_y + oled_cy - 8.6, pcb_top])
            cube([26.4, 17.2, 1.1]);
    color("black")
        translate([pcb_x + oled_cx - oled_lx / 2, pcb_y + oled_cy - oled_ly / 2, pcb_top + 0.9])
            cube([oled_lx, oled_ly, 0.35]);
    color("goldenrod")
        translate([pcb_x + 8, pcb_y + (pcb_w - 16) / 2, pcb_top])
            cube([11, 16, 3.1]);
    color("silver")
        translate([pcb_x - 1.4, pcb_y + pcb_w / 2 - 4, pcb_top])
            cube([6.2, 8, 2.6]);
    color("white")
        for (p = [[prg_x, prg_y], [rst_x, rst_y]])
            translate([pcb_x + p[0], pcb_y + p[1], pcb_top])
                cylinder(d = 3.2, h = 1.5, $fn = 24);
    color("gold")
        translate([pcb_x + pcb_l - 2.2, pcb_y + pcb_w / 2, pcb_top])
            cylinder(d = 2.4, h = 1.3, $fn = 20);
}

module cell_dummy() {
    color("dimgray")
        translate([bat_x0, bat_y0, z_floor])
            cube([bat_l, bat_w, bat_h]);
}

module screws_dummy() {
    color("silver")
        screw_centers() {
            translate([0, 0, base_h - 2.2])
                cylinder(d = 1.7, h = lid_t + 2.4, $fn = 16);
            translate([0, 0, base_h + lid_t - 0.15])
                cylinder(d = head_d - 0.15, h = 0.7, $fn = 24);
        }
}

module lid_print() {
    translate([0, 0, lid_t])
        rotate([180, 0, 0])
            lid();
}

echo(str("Outer ", outer_l, " x ", outer_w, " x ", base_h + lid_t, " mm"));
echo(str("Cell from USB end ", bat_x0 - pcb_x, " to ", bat_x1 - pcb_x, " mm"));

if (part == "base")
    base();
else if (part == "lid")
    lid_print();
else if (part == "both") {
    base();
    translate([0, outer_w + 6, 0])
        lid_print();
} else if (part == "assembly") {
    base();
    cell_dummy();
    board_dummy();
    color("black")
        translate([pcb_x + pcb_l - 0.5, pcb_y + pcb_w / 2, z_pcb + pcb_t + 1.15])
            rotate([0, 90, 0])
                cylinder(d = 1.5, h = end_cap + 16, $fn = 16);
    color("red", 0.55)
        translate([0, 0, base_h])
            lid();
    screws_dummy();
} else if (part == "preview") {
    base();
    translate([0, 0, base_h + 18])
        lid();
    board_dummy();
    cell_dummy();
}
