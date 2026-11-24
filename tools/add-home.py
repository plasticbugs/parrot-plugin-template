#!/usr/bin/env python3
"""tools/add-home.py SDK_DIR -- add the way back to the menu from the
controllers (cvhome.h: both fire buttons and *, or a USB pad's Select and
Start, held 2 s) to an older copy of the cart's SDK, such as a plugin's own
sdk/ from before it. SDK_DIR is the folder that holds firmware/revc/sdk.
Each change is made once: running it again changes nothing. It stops, and
changes nothing, if a file isn't as expected."""
import pathlib, shutil, sys

here = pathlib.Path(__file__).resolve().parent.parent / "sdk/firmware/revc/sdk"
sdk = pathlib.Path(sys.argv[1]) / "firmware/revc/sdk"
if not (sdk / "cvkernel.c").exists():
    sys.exit(f"{sdk}: no cvkernel.c")

HOME_K = r'''// ---- back to the menu from the controllers (cvhome.h) ----------------------------------
bool cvpad_held(uint32_t buttons) __attribute__((weak));   // cvpad.c, if the plugin has USB pads
static cvhome_t home;
static bool home_off, home_forced;
static uint32_t t_home;

void cvk_home(bool on) { home_off = !on; }

// the kernel's last report of a kind, A0-A5 (cvk_report()'s bits), without taking it
static uint8_t report_bits(uint32_t k) {
    const uint32_t in = report[k];
    uint8_t a = 0;
    for (uint l = 0; l < 6; l++) if (in >> line_gpio[l] & 1) a |= (uint8_t)(1u << l);
    return a;
}

// controller 1: the joystick report's A4 is the left fire, the keypad's the
// right one (low = pressed), the keypad's A0-A3 its lines
static bool console_held(void) {
    if (!report_n[REPORT_KIND_JOY] || !report_n[REPORT_KIND_KEY]) return false;
    const uint8_t j = report_bits(REPORT_KIND_JOY), k = report_bits(REPORT_KIND_KEY);
    return cvhome_console(!(j & 0x10), !(k & 0x10), k);
}

static void home_task(uint32_t t) {
    if (!t_home) {
        if (home_off) return;
        const bool held = console_held() || (cvpad_held && cvpad_held(CVHOME_PAD));
        if (!cvhome_step(&home, t, held)) return;
        t_home = t | 1;
        rom_ram[findex(CVHOME_NMI - 0x8000)] = perm_lut[0xC7];  // the next NMI restarts the BIOS
        ht_mark(HT_Y_BOOT, t, HM_RESET, 1);
    } else if (!home_forced && t - t_home > CVHOME_FORCE_US) {
        home_forced = true;                 // no NMI: RST 0 wherever it runs; the header stays 55 AA
        for (uint32_t a = 2; a < sizeof rom_ram; a++) rom_ram[findex(a)] = perm_lut[0xC7];
    }
}

'''

HOME_F = r'''// ---- back to the menu from the controllers (cvhome.h) ----------------------------------
bool cvpad_held(uint32_t buttons) __attribute__((weak));   // cvpad.c, if the plugin has USB pads
static cvhome_t home;
static bool home_off, home_forced;
static uint32_t t_home;

void cvf_home(bool on) { home_off = !on; }

static void home_task(uint32_t t) {
    if (!t_home) {
        if (home_off) return;
        const uint8_t fire = f_rep[1], key = f_rep[2];  // controller 1, as the kernel last read it (low = pressed)
        const bool held = cvhome_console(!(fire & 1), !(fire & 2), key) || (cvpad_held && cvpad_held(CVHOME_PAD));
        if (!cvhome_step(&home, t, held)) return;
        t_home = t | 1;
        fixed_ram[bindex(CVHOME_NMI - 0x8000)] = perm_lut[0xC7];    // the next NMI restarts the BIOS
        ht_mark(HT_Y_BOOT, t, HM_RESET, 1);
    } else if (!home_forced && t - t_home > CVHOME_FORCE_US) {
        home_forced = true;                 // no NMI: RST 0 wherever it runs; the header stays 55 AA
        for (uint32_t a = 2; a < BANK; a++) fixed_ram[bindex(a)] = perm_lut[0xC7];
    }
}

'''

TASK_OLD = "    if (console_off) to_menu_now(1);\n    if (!t_menu) return;\n"
TASK_NEW = "    if (console_off) to_menu_now(1);\n    if (!t_menu) {\n        if (driving) home_task(t);\n        return;\n    }\n"
HDR_DOC = """// each pass of the plugin's loop: the power watch, and back to the menu
// (it doesn't return then) after RESET or the console going off
void {p}_task(uint32_t now_us);
"""
HDR_NEW = """// each pass of the plugin's loop: the power watch, and back to the menu
// (it doesn't return then) after RESET, the console going off, or both fire
// buttons and * (a USB pad's Select and Start) held for 2 seconds (cvhome.h)
void {p}_task(uint32_t now_us);

// false: the fire buttons and * (Select and Start) held don't go back to the
// menu, for a plugin whose game needs them held together (true again: they do)
void {p}_home(bool on);
"""
PAD_H_OLD = "bool cvpad_get(int n, gamepad_t *p);\n"
PAD_H_NEW = PAD_H_OLD + """
// some pad holds all of these buttons (GP_*) now. Unlike cvpad_get() it
// leaves the presses it latches alone: cvk_task() and cvf_task() look with
// it for Select and Start (cvhome.h)
bool cvpad_held(uint32_t buttons);
"""
PAD_C_OLD = "bool cvpad_info(int n, cvpad_info_t *out) {"
PAD_C_NEW = """bool cvpad_held(uint32_t buttons) {
    bool held = false;
    const uint32_t irq = save_and_disable_interrupts();
    for (int n = 0; n < CVPAD_MAX && !held; n++) {
        const slot_t *s = &pads[n];
        if (s->driver == CVPAD_NONE || !s->live) continue;
        const uint32_t b = s->driver == CVPAD_SWITCH ? s->sw.pad.buttons : s->hp.pad.buttons;
        held = (b & buttons) == buttons;
    }
    restore_interrupts(irq);
    return held;
}

""" + PAD_C_OLD

edits = []          # (path, new text)
def plan(name, fn):
    p = sdk / name
    if not p.exists():
        return
    old = p.read_text()
    new = fn(old)
    if new is None:
        sys.exit(f"{p}: not as expected, nothing changed")
    if new != old:
        edits.append((p, new))

def once(text, mark, old, new):
    if mark in text:
        return text
    if text.count(old) != 1:
        return None
    return text.replace(old, new)

def c_file(home, inc_after):
    def fn(t):
        if "home_task" in t:
            return t
        t = once(t, '#include "cvhome.h"', inc_after, '#include "cvhome.h"\n' + inc_after)
        if t is None: return None
        t = once(t, "HOME_BLOCK", "static uint32_t t_menu;\nstatic bool titled;\n", home + "static uint32_t t_menu;\nstatic bool titled;\n")
        if t is None: return None
        return once(t, "if (driving) home_task(t);", TASK_OLD, TASK_NEW)
    return fn

plan("cvkernel.c", c_file(HOME_K, '#include "cvlaunch.h"\n#include "../sdplayer/htrace.h"'))
plan("cvframes.c", c_file(HOME_F, '#include "../sdplayer/perm.h"'))
plan("cvkernel.h", lambda t: once(t, "cvk_home(", HDR_DOC.format(p="cvk"), HDR_NEW.format(p="cvk")))
plan("cvframes.h", lambda t: once(t, "cvf_home(", HDR_DOC.format(p="cvf"), HDR_NEW.format(p="cvf")))
plan("cvpad.h", lambda t: once(t, "cvpad_held(", PAD_H_OLD, PAD_H_NEW))
plan("cvpad.c", lambda t: once(t, "bool cvpad_held(", PAD_C_OLD, PAD_C_NEW))
for p, t in edits:
    p.write_text(t)
if not (sdk / "cvhome.h").exists() or (sdk / "cvhome.h").read_text() != (here / "cvhome.h").read_text():
    shutil.copyfile(here / "cvhome.h", sdk / "cvhome.h")
    edits.append((sdk / "cvhome.h", None))
print(f"{sdk}: " + (", ".join(p.name for p, _ in edits) if edits else "already has it"))
