// input.c -- the desktop's keyboard, mouse and game pads, as what the
// console and the cart's USB port would have plugged in.
//
// Every TV frame, input_poll() reads SDL's events and state and hands the
// models (model.c) what each device holds now:
//
//   the console's controller 1   the keyboard (arrows or WASD, Z and X,
//                                the number keys, - and =), and every
//                                game pad (d-pad or left stick, face
//                                buttons, Back and Start as * and #)
//   a USB game pad (cvpad)       the first game pad, all its buttons and
//                                both sticks; or the keyboard, in its
//                                "USB pad" role
//   a USB keyboard and mouse     the keyboard and mouse, in their "USB
//                                keyboard" role (F2), for host.c's model of
//                                them (the usb-keyboard-mouse example's
//                                usbinput.h)
//
// F2 moves the keyboard between those roles. The simulator keeps the F
// keys (F1 help, F2, F5 RESET, F12 screenshot) and, outside the USB
// keyboard role, Esc (quit).
//
// Later (sim/README.md, "Hook points"): MIDI ports as USB MIDI in and out,
// and the SDK's own USB keyboard and mouse API once it has one.
#include "sim.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "wired.h"

#define MAX_PADS 8
static SDL_Window *win;
static SDL_Gamepad *pads[MAX_PADS];
static int role = ROLE_CONSOLE;
static int last_key = -1;                   // the keypad key pressed last on the keyboard

static const char *const role_names[ROLE_COUNT] = {"controller", "USB pad", "USB keyboard and mouse"};

int input_role(void) { return role; }
const char *input_role_name(void) { return role_names[role]; }

void input_set_role(int r) {
    role = r % ROLE_COUNT;
    if (win) SDL_SetWindowRelativeMouseMode(win, role == ROLE_USB_KEYBOARD);
    if (role == ROLE_USB_KEYBOARD) model_usb_mouse(0, 0, 0);   // plugged in
    printf("keyboard: %s%s\n", role_names[role],
           role == ROLE_USB_KEYBOARD ? " (the mouse is the USB mouse; F2 lets it go)" : "");
}

static void pad_added(SDL_JoystickID id) {
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_GetGamepadID(pads[i]) == id) return;
    for (int i = 0; i < MAX_PADS; i++)
        if (!pads[i]) {
            pads[i] = SDL_OpenGamepad(id);
            if (pads[i]) printf("game pad %d: %s\n", i + 1, SDL_GetGamepadName(pads[i]) ? SDL_GetGamepadName(pads[i]) : "(no name)");
            return;
        }
}

static void pad_removed(SDL_JoystickID id) {
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_GetGamepadID(pads[i]) == id) {
            printf("game pad %d: unplugged\n", i + 1);
            SDL_CloseGamepad(pads[i]);
            pads[i] = NULL;
        }
}

void input_init(void *window) {
    win = window;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) pad_added(ids[i]);
    SDL_free(ids);
}

// the keypad, from the keyboard: 0-9 (either row), * on -, # on =
static int keypad_key(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return (int)(s - SDL_SCANCODE_1) + 1;
    if (s == SDL_SCANCODE_0 || s == SDL_SCANCODE_KP_0) return 0;
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return (int)(s - SDL_SCANCODE_KP_1) + 1;
    if (s == SDL_SCANCODE_MINUS || s == SDL_SCANCODE_KP_MULTIPLY) return WIRED_STAR;
    if (s == SDL_SCANCODE_EQUALS) return WIRED_HASH;
    return -1;
}

static bool keypad_held(const bool *ks, int key) {
    for (int s = 0; s < SDL_SCANCODE_COUNT; s++)
        if (ks[s] && keypad_key((SDL_Scancode)s) == key) return true;
    return false;
}

static int16_t axis(SDL_Gamepad *p, SDL_GamepadAxis a, bool flip) {
    int v = SDL_GetGamepadAxis(p, a) / 16;  // -2048..2047
    if (flip) v = -v - 1;                   // SDL's y is down, gamepad.h's up
    return (int16_t)(v < -2048 ? -2048 : v > 2047 ? 2047 : v);
}

// a desktop pad as cvpad's gamepad_t (buttons by where they are, as both name them)
static void read_pad(SDL_Gamepad *p, gamepad_t *g) {
    static const struct { SDL_GamepadButton b; uint32_t gp; } map[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, GP_UP},         {SDL_GAMEPAD_BUTTON_DPAD_DOWN, GP_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, GP_LEFT},     {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, GP_RIGHT},
        {SDL_GAMEPAD_BUTTON_SOUTH, GP_SOUTH},        {SDL_GAMEPAD_BUTTON_EAST, GP_EAST},
        {SDL_GAMEPAD_BUTTON_WEST, GP_WEST},          {SDL_GAMEPAD_BUTTON_NORTH, GP_NORTH},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, GP_L},    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GP_R},
        {SDL_GAMEPAD_BUTTON_BACK, GP_MINUS},         {SDL_GAMEPAD_BUTTON_START, GP_PLUS},
        {SDL_GAMEPAD_BUTTON_GUIDE, GP_HOME},         {SDL_GAMEPAD_BUTTON_MISC1, GP_CAPTURE},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, GP_LSTICK},  {SDL_GAMEPAD_BUTTON_RIGHT_STICK, GP_RSTICK},
    };
    memset(g, 0, sizeof *g);
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (SDL_GetGamepadButton(p, map[i].b)) g->buttons |= map[i].gp;
    if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) g->buttons |= GP_ZL;
    if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) g->buttons |= GP_ZR;
    g->lx = axis(p, SDL_GAMEPAD_AXIS_LEFTX, false);
    g->ly = axis(p, SDL_GAMEPAD_AXIS_LEFTY, true);
    g->rx = axis(p, SDL_GAMEPAD_AXIS_RIGHTX, false);
    g->ry = axis(p, SDL_GAMEPAD_AXIS_RIGHTY, true);
    g->raw[0] = (uint16_t)(g->lx + 2048);
    g->raw[1] = (uint16_t)(g->ly + 2048);
    g->raw[2] = (uint16_t)(g->rx + 2048);
    g->raw[3] = (uint16_t)(g->ry + 2048);
}

// the keyboard as a USB pad (the "USB pad" role)
static void keyboard_pad(const bool *ks, gamepad_t *g) {
    static const struct { SDL_Scancode s; uint32_t gp; } map[] = {
        {SDL_SCANCODE_UP, GP_UP},     {SDL_SCANCODE_DOWN, GP_DOWN},  {SDL_SCANCODE_LEFT, GP_LEFT},
        {SDL_SCANCODE_RIGHT, GP_RIGHT}, {SDL_SCANCODE_Z, GP_SOUTH},  {SDL_SCANCODE_X, GP_EAST},
        {SDL_SCANCODE_A, GP_WEST},    {SDL_SCANCODE_S, GP_NORTH},    {SDL_SCANCODE_Q, GP_L},
        {SDL_SCANCODE_W, GP_R},       {SDL_SCANCODE_E, GP_ZL},       {SDL_SCANCODE_R, GP_ZR},
        {SDL_SCANCODE_BACKSPACE, GP_MINUS}, {SDL_SCANCODE_RETURN, GP_PLUS}, {SDL_SCANCODE_H, GP_HOME},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (ks[map[i].s]) g->buttons |= map[i].gp;
    if (ks[SDL_SCANCODE_J]) g->lx = -2048;  // IJKL: the left stick, all the way
    if (ks[SDL_SCANCODE_L]) g->lx = 2047;
    if (ks[SDL_SCANCODE_I]) g->ly = 2047;
    if (ks[SDL_SCANCODE_K]) g->ly = -2048;
}

static bool sim_key(SDL_Scancode s, sim_keys_t *k) {
    switch (s) {
    case SDL_SCANCODE_F1: k->help = true; return true;
    case SDL_SCANCODE_F2: k->role_next = true; return true;
    case SDL_SCANCODE_F5: k->reset = true; return true;
    case SDL_SCANCODE_F12: k->shot = true; return true;
    case SDL_SCANCODE_ESCAPE:
        if (role == ROLE_USB_KEYBOARD) return false;
        k->quit = true;
        return true;
    default: return false;
    }
}

void input_poll(sim_keys_t *k) {
    memset(k, 0, sizeof *k);
    SDL_Event e;
    float mdx = 0, mdy = 0;
    static float mfx, mfy;                  // the mouse's fractions of a count
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            k->quit = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (sim_key(e.key.scancode, k) || e.key.repeat) break;
            if (role == ROLE_USB_KEYBOARD && e.key.scancode >= 4 && e.key.scancode <= 0xE7) model_usb_key((uint8_t)e.key.scancode, true);
            else if (role == ROLE_CONSOLE && keypad_key(e.key.scancode) >= 0) last_key = keypad_key(e.key.scancode);
            break;
        case SDL_EVENT_KEY_UP:
            if (role == ROLE_USB_KEYBOARD && e.key.scancode >= 4 && e.key.scancode <= 0xE7 && e.key.scancode != SDL_SCANCODE_F2 &&
                e.key.scancode != SDL_SCANCODE_F5 && e.key.scancode != SDL_SCANCODE_F12 && e.key.scancode != SDL_SCANCODE_F1)
                model_usb_key((uint8_t)e.key.scancode, false);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            mdx += e.motion.xrel;
            mdy += e.motion.yrel;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            pad_added(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            pad_removed(e.gdevice.which);
            break;
        default:
            break;
        }
    }
    const bool *ks = SDL_GetKeyboardState(NULL);

    // the console's controller 1
    uint8_t held = 0;
    int key = -1;
    if (role == ROLE_CONSOLE) {
        if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W]) held |= WIRED_UP;
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) held |= WIRED_RIGHT;
        if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S]) held |= WIRED_DOWN;
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) held |= WIRED_LEFT;
        if (ks[SDL_SCANCODE_Z]) held |= WIRED_FIRE_L;
        if (ks[SDL_SCANCODE_X]) held |= WIRED_FIRE_R;
        if (last_key >= 0 && !keypad_held(ks, last_key)) last_key = -1;
        if (last_key < 0)                   // let go of the last one: another still held?
            for (int s = 0; s < SDL_SCANCODE_COUNT && last_key < 0; s++)
                if (ks[s] && keypad_key((SDL_Scancode)s) >= 0) last_key = keypad_key((SDL_Scancode)s);
        key = last_key;
    }
    gamepad_t usb;
    memset(&usb, 0, sizeof usb);
    bool usb_in = false;
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i]) continue;
        gamepad_t g;
        read_pad(pads[i], &g);
        if (!usb_in) { usb = g; usb_in = true; }
        const uint32_t b = g.buttons;
        if (b & GP_UP || g.ly > 1024) held |= WIRED_UP;
        if (b & GP_DOWN || g.ly < -1024) held |= WIRED_DOWN;
        if (b & GP_LEFT || g.lx < -1024) held |= WIRED_LEFT;
        if (b & GP_RIGHT || g.lx > 1024) held |= WIRED_RIGHT;
        if (b & (GP_SOUTH | GP_WEST)) held |= WIRED_FIRE_L;
        if (b & (GP_EAST | GP_NORTH)) held |= WIRED_FIRE_R;
        if (key < 0 && b & GP_MINUS) key = WIRED_STAR;
        if (key < 0 && b & GP_PLUS) key = WIRED_HASH;
    }
    model_console(held, key);

    // USB
    if (role == ROLE_USB_PAD) {
        usb_in = true;
        keyboard_pad(ks, &usb);
    }
    model_usb_pad(usb_in, &usb);
    if (role == ROLE_USB_KEYBOARD) {
        mfx += mdx;
        mfy += mdy;
        const int dx = (int)mfx, dy = (int)mfy;
        mfx -= (float)dx;
        mfy -= (float)dy;
        const SDL_MouseButtonFlags mb = SDL_GetMouseState(NULL, NULL);
        const uint8_t buttons = (uint8_t)((mb & SDL_BUTTON_LMASK ? 1 : 0) | (mb & SDL_BUTTON_RMASK ? 2 : 0) | (mb & SDL_BUTTON_MMASK ? 4 : 0));
        model_usb_mouse(dx, dy, buttons);
    }
}

// for the menu's screen after RESET: any of these starts the plugin again
bool input_start_pressed(void) {
    const bool *ks = SDL_GetKeyboardState(NULL);
    if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_X]) return true;
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && (SDL_GetGamepadButton(pads[i], SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(pads[i], SDL_GAMEPAD_BUTTON_EAST) ||
                        SDL_GetGamepadButton(pads[i], SDL_GAMEPAD_BUTTON_START)))
            return true;
    return false;
}
