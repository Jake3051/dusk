#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include <SDL3/SDL_events.h>
#include <dolphin/pad.h>
#include "SSystem/SComponent/c_API_controller_pad.h"
#include "dusk/config.hpp"
#include "dusk/main.h"
#include "dusk/settings.h"
#include "dusk/touch_controls.hpp"
#include "dusk/ui/ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace dusk::touch_controls {
namespace {

// ---------------------------------------------------------------------------
// Control IDs
// ---------------------------------------------------------------------------
enum CtrlId : int {
    CTRL_BTN_A = 0,
    CTRL_BTN_B,
    CTRL_BTN_X,
    CTRL_BTN_Y,
    CTRL_BTN_L,
    CTRL_BTN_R,
    CTRL_BTN_Z,
    CTRL_BTN_START,
    CTRL_DPAD,        // single widget, four hit-zones
    CTRL_STICK_MAIN,
    CTRL_STICK_C,
    CTRL_COUNT,
    CTRL_NONE = -1,
};

// ---------------------------------------------------------------------------
// Static metadata per control
// ---------------------------------------------------------------------------
struct CtrlDef {
    const char* label;   // nullptr for dpad / sticks
    uint32_t padBit;     // 0 for dpad / sticks
    ImU32 colorActive;
    ImU32 colorIdle;
    float radius;        // logical pixels at scale 1.0
};

static constexpr CtrlDef kDefs[CTRL_COUNT] = {
    {"A",  PAD_BUTTON_A,    IM_COL32( 30,160, 30,220), IM_COL32( 20,100, 20,170), 32.f},
    {"B",  PAD_BUTTON_B,    IM_COL32(180, 30, 30,220), IM_COL32(120, 20, 20,170), 32.f},
    {"X",  PAD_BUTTON_X,    IM_COL32( 30, 80,200,220), IM_COL32( 20, 55,140,170), 32.f},
    {"Y",  PAD_BUTTON_Y,    IM_COL32(200,160, 20,220), IM_COL32(140,110, 10,170), 32.f},
    {"L",  PAD_TRIGGER_L,   IM_COL32( 90, 90, 90,220), IM_COL32( 60, 60, 60,170), 34.f},
    {"R",  PAD_TRIGGER_R,   IM_COL32( 90, 90, 90,220), IM_COL32( 60, 60, 60,170), 34.f},
    {"Z",  PAD_TRIGGER_Z,   IM_COL32( 80, 50,140,220), IM_COL32( 55, 35,100,170), 28.f},
    // Start: UTF-8 ≡ (hamburger / three lines)
    {"\xe2\x89\xa1", PAD_BUTTON_START, IM_COL32(80,80,80,220), IM_COL32(55,55,55,170), 26.f},
    {nullptr, 0, IM_COL32(65,65,65,200), IM_COL32(42,42,42,160), 48.f}, // dpad
    {nullptr, 0, IM_COL32(65,65,65,200), IM_COL32(42,42,42,160), 52.f}, // main stick
    {nullptr, 0, IM_COL32(65,65,65,200), IM_COL32(42,42,42,160), 42.f}, // c-stick
};

// ---------------------------------------------------------------------------
// Finger state
// ---------------------------------------------------------------------------
struct FingerState {
    SDL_FingerID id = 0;
    int ctrl = CTRL_NONE;
    // For CTRL_DPAD: padBit stores which direction was hit on finger-down
    uint32_t dpadBit = 0;
    // For CTRL_STICK_*: current normalized output in [-1, 1]
    float stickX = 0.f;
    float stickY = 0.f;
    bool active = false;
};

static constexpr int kMaxFingers = 16;
static std::array<FingerState, kMaxFingers> g_fingers;

// ---------------------------------------------------------------------------
// Virtual pad state (read by apply_virtual_input each tick)
// ---------------------------------------------------------------------------
static uint32_t g_held = 0;
static uint32_t g_prevHeld = 0;
static float g_stickMX = 0.f, g_stickMY = 0.f;  // main stick
static float g_stickCX = 0.f, g_stickCY = 0.f;  // c-stick

// ---------------------------------------------------------------------------
// Customize mode state
// ---------------------------------------------------------------------------
static bool g_customizeMode = false;
static int g_dragCtrl = CTRL_NONE;
static SDL_FingerID g_dragFinger = 0;
static float g_dragStartNX = 0.f, g_dragStartNY = 0.f;   // normalized finger start
static float g_dragCtrlStartX = 0.f, g_dragCtrlStartY = 0.f; // ctrl pos when drag began

// ---------------------------------------------------------------------------
// Controller auto-disable tracking
// ---------------------------------------------------------------------------
static int g_controllerCount = 0;
// True when we disabled touch automatically due to a controller connecting;
// cleared when the user manually toggles the overlay or all controllers disconnect.
static bool g_autoDisabled = false;

// ---------------------------------------------------------------------------
// Settings accessors (switch dispatch avoids template array problems)
// ---------------------------------------------------------------------------
static float get_x(int id) {
    auto& t = getSettings().touch;
    switch (id) {
    case CTRL_BTN_A:      return t.btnAX.getValue();
    case CTRL_BTN_B:      return t.btnBX.getValue();
    case CTRL_BTN_X:      return t.btnXX.getValue();
    case CTRL_BTN_Y:      return t.btnYX.getValue();
    case CTRL_BTN_L:      return t.btnLX.getValue();
    case CTRL_BTN_R:      return t.btnRX.getValue();
    case CTRL_BTN_Z:      return t.btnZX.getValue();
    case CTRL_BTN_START:  return t.btnStartX.getValue();
    case CTRL_DPAD:       return t.dpadX.getValue();
    case CTRL_STICK_MAIN: return t.stickMainX.getValue();
    case CTRL_STICK_C:    return t.stickCX.getValue();
    default: return 0.5f;
    }
}

static float get_y(int id) {
    auto& t = getSettings().touch;
    switch (id) {
    case CTRL_BTN_A:      return t.btnAY.getValue();
    case CTRL_BTN_B:      return t.btnBY.getValue();
    case CTRL_BTN_X:      return t.btnXY.getValue();
    case CTRL_BTN_Y:      return t.btnYY.getValue();
    case CTRL_BTN_L:      return t.btnLY.getValue();
    case CTRL_BTN_R:      return t.btnRY.getValue();
    case CTRL_BTN_Z:      return t.btnZY.getValue();
    case CTRL_BTN_START:  return t.btnStartY.getValue();
    case CTRL_DPAD:       return t.dpadY.getValue();
    case CTRL_STICK_MAIN: return t.stickMainY.getValue();
    case CTRL_STICK_C:    return t.stickCY.getValue();
    default: return 0.5f;
    }
}

static void set_x(int id, float v) {
    v = std::clamp(v, 0.f, 1.f);
    auto& t = getSettings().touch;
    switch (id) {
    case CTRL_BTN_A:      t.btnAX.setValue(v); break;
    case CTRL_BTN_B:      t.btnBX.setValue(v); break;
    case CTRL_BTN_X:      t.btnXX.setValue(v); break;
    case CTRL_BTN_Y:      t.btnYX.setValue(v); break;
    case CTRL_BTN_L:      t.btnLX.setValue(v); break;
    case CTRL_BTN_R:      t.btnRX.setValue(v); break;
    case CTRL_BTN_Z:      t.btnZX.setValue(v); break;
    case CTRL_BTN_START:  t.btnStartX.setValue(v); break;
    case CTRL_DPAD:       t.dpadX.setValue(v); break;
    case CTRL_STICK_MAIN: t.stickMainX.setValue(v); break;
    case CTRL_STICK_C:    t.stickCX.setValue(v); break;
    default: break;
    }
}

static void set_y(int id, float v) {
    v = std::clamp(v, 0.f, 1.f);
    auto& t = getSettings().touch;
    switch (id) {
    case CTRL_BTN_A:      t.btnAY.setValue(v); break;
    case CTRL_BTN_B:      t.btnBY.setValue(v); break;
    case CTRL_BTN_X:      t.btnXY.setValue(v); break;
    case CTRL_BTN_Y:      t.btnYY.setValue(v); break;
    case CTRL_BTN_L:      t.btnLY.setValue(v); break;
    case CTRL_BTN_R:      t.btnRY.setValue(v); break;
    case CTRL_BTN_Z:      t.btnZY.setValue(v); break;
    case CTRL_BTN_START:  t.btnStartY.setValue(v); break;
    case CTRL_DPAD:       t.dpadY.setValue(v); break;
    case CTRL_STICK_MAIN: t.stickMainY.setValue(v); break;
    case CTRL_STICK_C:    t.stickCY.setValue(v); break;
    default: break;
    }
}

static float scaled_radius(int id) {
    return kDefs[id].radius * getSettings().touch.scale.getValue();
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

// Returns the PAD bit corresponding to which D-pad quadrant (px,py) lands in.
// Returns 0 if outside the D-pad area or in the dead-zone center.
static uint32_t dpad_bit_at(float px, float py, float w, float h) {
    float cx = get_x(CTRL_DPAD) * w;
    float cy = get_y(CTRL_DPAD) * h;
    float r  = scaled_radius(CTRL_DPAD);
    float dx = px - cx;
    float dy = py - cy;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < r * 0.15f || dist > r) return 0;
    // Angle: 0 = right, π/2 = down (screen coords)
    static constexpr float kPi = 3.14159265f;
    float angle = std::atan2(dy, dx);
    if (angle > -kPi * 0.25f && angle <  kPi * 0.25f) return PAD_BUTTON_RIGHT;
    if (angle >  kPi * 0.75f || angle < -kPi * 0.75f) return PAD_BUTTON_LEFT;
    if (angle >  kPi * 0.25f && angle <  kPi * 0.75f) return PAD_BUTTON_DOWN;
    return PAD_BUTTON_UP;
}

// Returns CTRL_* of whichever control (px, py) in screen pixels falls inside.
// In customize mode, uses full bounding circle for all controls (incl. D-pad center).
static int hit_test(float px, float py, float w, float h, bool customize) {
    if (customize) {
        // In customize mode any touch within the bounding circle claims the control
        for (int i = CTRL_COUNT - 1; i >= 0; --i) {
            float cx = get_x(i) * w;
            float cy = get_y(i) * h;
            float r  = scaled_radius(i);
            float dx = px - cx, dy = py - cy;
            if (dx * dx + dy * dy <= r * r) return i;
        }
        return CTRL_NONE;
    }
    // Gameplay hit-test: D-pad needs a directional hit, sticks need inside outer circle
    if (dpad_bit_at(px, py, w, h) != 0) return CTRL_DPAD;
    for (int s : {CTRL_STICK_MAIN, CTRL_STICK_C}) {
        float cx = get_x(s) * w;
        float cy = get_y(s) * h;
        float r  = scaled_radius(s);
        float dx = px - cx, dy = py - cy;
        if (dx * dx + dy * dy <= r * r) return s;
    }
    for (int i = CTRL_BTN_START; i >= CTRL_BTN_A; --i) {
        float cx = get_x(i) * w;
        float cy = get_y(i) * h;
        float r  = scaled_radius(i);
        float dx = px - cx, dy = py - cy;
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return CTRL_NONE;
}

// ---------------------------------------------------------------------------
// Finger pool helpers
// ---------------------------------------------------------------------------
static FingerState* alloc_finger(SDL_FingerID id) {
    for (auto& f : g_fingers) {
        if (!f.active) {
            f = {};
            f.id = id;
            f.active = true;
            return &f;
        }
    }
    return nullptr;
}

static FingerState* find_finger(SDL_FingerID id) {
    for (auto& f : g_fingers) {
        if (f.active && f.id == id) return &f;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Rebuild g_held / g_stickMX/MY / g_stickCX/CY from all active fingers
// ---------------------------------------------------------------------------
static void recompute_virtual_state() {
    g_held    = 0;
    g_stickMX = g_stickMY = 0.f;
    g_stickCX = g_stickCY = 0.f;
    for (auto& f : g_fingers) {
        if (!f.active || f.ctrl == CTRL_NONE) continue;
        if (f.ctrl >= CTRL_BTN_A && f.ctrl <= CTRL_BTN_START) {
            g_held |= kDefs[f.ctrl].padBit;
        } else if (f.ctrl == CTRL_DPAD) {
            g_held |= f.dpadBit;
        } else if (f.ctrl == CTRL_STICK_MAIN) {
            g_stickMX = f.stickX;
            g_stickMY = f.stickY;
        } else if (f.ctrl == CTRL_STICK_C) {
            g_stickCX = f.stickX;
            g_stickCY = f.stickY;
        }
    }
}

// ---------------------------------------------------------------------------
// Compute normalized stick output from a screen touch position
// ---------------------------------------------------------------------------
static void compute_stick(int ctrlId, float px, float py, float w, float h,
                           float& outX, float& outY) {
    float cx = get_x(ctrlId) * w;
    float cy = get_y(ctrlId) * h;
    float r  = scaled_radius(ctrlId);
    float dx = px - cx;
    float dy = -(py - cy);  // flip Y: screen-down → game-down inverted
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > r) {
        dx = dx / dist * r;
        dy = dy / dist * r;
        dist = r;
    }
    outX = (r > 0.f) ? dx / r : 0.f;
    outY = (r > 0.f) ? dy / r : 0.f;
}

// ---------------------------------------------------------------------------
// Finger event handling
// ---------------------------------------------------------------------------
static void on_finger_down(const SDL_TouchFingerEvent& ev) {
    auto& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;
    float px = ev.x * w;
    float py = ev.y * h;

    if (g_customizeMode) {
        // In customize mode: start dragging whichever control was touched
        int ctrl = hit_test(px, py, w, h, true);
        if (ctrl != CTRL_NONE && g_dragCtrl == CTRL_NONE) {
            g_dragCtrl       = ctrl;
            g_dragFinger     = ev.fingerID;
            g_dragStartNX    = ev.x;
            g_dragStartNY    = ev.y;
            g_dragCtrlStartX = get_x(ctrl);
            g_dragCtrlStartY = get_y(ctrl);
        }
        return;
    }

    int ctrl = hit_test(px, py, w, h, false);
    if (ctrl == CTRL_NONE) return;

    FingerState* f = alloc_finger(ev.fingerID);
    if (!f) return;
    f->ctrl = ctrl;

    if (ctrl >= CTRL_BTN_A && ctrl <= CTRL_BTN_START) {
        // button press: nothing extra needed
    } else if (ctrl == CTRL_DPAD) {
        f->dpadBit = dpad_bit_at(px, py, w, h);
    } else {
        // stick: compute initial deflection
        compute_stick(ctrl, px, py, w, h, f->stickX, f->stickY);
    }
    recompute_virtual_state();
}

static void on_finger_motion(const SDL_TouchFingerEvent& ev) {
    auto& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;
    float px = ev.x * w;
    float py = ev.y * h;

    if (g_customizeMode) {
        if (ev.fingerID == g_dragFinger && g_dragCtrl != CTRL_NONE) {
            float newX = g_dragCtrlStartX + (ev.x - g_dragStartNX);
            float newY = g_dragCtrlStartY + (ev.y - g_dragStartNY);
            set_x(g_dragCtrl, newX);
            set_y(g_dragCtrl, newY);
        }
        return;
    }

    FingerState* f = find_finger(ev.fingerID);
    if (!f) return;

    if (f->ctrl == CTRL_DPAD) {
        f->dpadBit = dpad_bit_at(px, py, w, h);
    } else if (f->ctrl == CTRL_STICK_MAIN || f->ctrl == CTRL_STICK_C) {
        compute_stick(f->ctrl, px, py, w, h, f->stickX, f->stickY);
    }
    recompute_virtual_state();
}

static void on_finger_up(const SDL_TouchFingerEvent& ev) {
    if (g_customizeMode) {
        if (ev.fingerID == g_dragFinger) {
            if (g_dragCtrl != CTRL_NONE) {
                config::Save();
            }
            g_dragCtrl   = CTRL_NONE;
            g_dragFinger = 0;
        }
        return;
    }

    FingerState* f = find_finger(ev.fingerID);
    if (!f) return;
    f->active = false;
    recompute_virtual_state();
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------
static ImU32 with_opacity(ImU32 col) {
    float op = getSettings().touch.opacity.getValue();
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    c.w *= op;
    return ImGui::ColorConvertFloat4ToU32(c);
}

static void draw_controls() {
    auto* dl = ImGui::GetForegroundDrawList();
    auto& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImU32 borderNormal   = with_opacity(IM_COL32(200, 200, 200, 200));
    ImU32 borderCustomize = with_opacity(IM_COL32(255, 220, 0, 230));
    ImU32 borderCol = g_customizeMode ? borderCustomize : borderNormal;

    // --- Regular buttons (A B X Y L R Z Start) ---
    for (int i = CTRL_BTN_A; i <= CTRL_BTN_START; ++i) {
        float cx = get_x(i) * w;
        float cy = get_y(i) * h;
        float r  = scaled_radius(i);
        bool  pressed = (g_held & kDefs[i].padBit) != 0;
        ImU32 fill = with_opacity(pressed ? kDefs[i].colorActive : kDefs[i].colorIdle);
        dl->AddCircleFilled({cx, cy}, r, fill);
        dl->AddCircle({cx, cy}, r, borderCol, 0, 2.f);
        if (kDefs[i].label) {
            ImVec2 ts = ImGui::CalcTextSize(kDefs[i].label);
            dl->AddText({cx - ts.x * 0.5f, cy - ts.y * 0.5f},
                        with_opacity(IM_COL32(255, 255, 255, 240)), kDefs[i].label);
        }
    }

    // --- D-pad ---
    {
        float cx  = get_x(CTRL_DPAD) * w;
        float cy  = get_y(CTRL_DPAD) * h;
        float r   = scaled_radius(CTRL_DPAD);
        float arm = r * 0.38f;
        ImU32 idle   = with_opacity(kDefs[CTRL_DPAD].colorIdle);
        ImU32 active = with_opacity(kDefs[CTRL_DPAD].colorActive);

        auto dir_col = [&](uint32_t bit) -> ImU32 {
            return (g_held & bit) ? active : idle;
        };
        // UP
        dl->AddTriangleFilled({cx - arm, cy - arm}, {cx + arm, cy - arm},
                              {cx, cy - r}, dir_col(PAD_BUTTON_UP));
        // DOWN
        dl->AddTriangleFilled({cx - arm, cy + arm}, {cx + arm, cy + arm},
                              {cx, cy + r}, dir_col(PAD_BUTTON_DOWN));
        // LEFT
        dl->AddTriangleFilled({cx - arm, cy - arm}, {cx - arm, cy + arm},
                              {cx - r, cy}, dir_col(PAD_BUTTON_LEFT));
        // RIGHT
        dl->AddTriangleFilled({cx + arm, cy - arm}, {cx + arm, cy + arm},
                              {cx + r, cy}, dir_col(PAD_BUTTON_RIGHT));
        // Center fill
        dl->AddRectFilled({cx - arm, cy - arm}, {cx + arm, cy + arm}, idle);

        // Borders
        dl->AddTriangle({cx - arm, cy - arm}, {cx + arm, cy - arm}, {cx, cy - r}, borderCol, 2.f);
        dl->AddTriangle({cx - arm, cy + arm}, {cx + arm, cy + arm}, {cx, cy + r}, borderCol, 2.f);
        dl->AddTriangle({cx - arm, cy - arm}, {cx - arm, cy + arm}, {cx - r, cy},  borderCol, 2.f);
        dl->AddTriangle({cx + arm, cy - arm}, {cx + arm, cy + arm}, {cx + r, cy},  borderCol, 2.f);
    }

    // --- Analog sticks ---
    for (int s : {CTRL_STICK_MAIN, CTRL_STICK_C}) {
        float cx      = get_x(s) * w;
        float cy      = get_y(s) * h;
        float outerR  = scaled_radius(s);
        float thumbR  = outerR * 0.38f;
        ImU32 bg      = with_opacity(kDefs[s].colorIdle);
        ImU32 thumb   = with_opacity(IM_COL32(145, 145, 145, 230));

        float sx = (s == CTRL_STICK_MAIN) ? g_stickMX : g_stickCX;
        float sy = (s == CTRL_STICK_MAIN) ? -g_stickMY : -g_stickCY; // flip Y for screen

        float tx = cx + sx * (outerR - thumbR);
        float ty = cy + sy * (outerR - thumbR);

        dl->AddCircleFilled({cx, cy}, outerR, bg);
        dl->AddCircle({cx, cy}, outerR, borderCol, 0, 2.f);
        dl->AddCircleFilled({tx, ty}, thumbR, thumb);
        dl->AddCircle({tx, ty}, thumbR, borderCol, 0, 1.5f);
    }

    // --- "C" label on C-stick ---
    {
        float cx = get_x(CTRL_STICK_C) * w;
        float cy = get_y(CTRL_STICK_C) * h;
        float r  = scaled_radius(CTRL_STICK_C);
        ImVec2 ts = ImGui::CalcTextSize("C");
        float lx = cx + r * 0.55f - ts.x * 0.5f;
        float ly = cy - r * 0.55f - ts.y * 0.5f;
        dl->AddText({lx, ly}, with_opacity(IM_COL32(220, 200, 50, 220)), "C");
    }
}

static void clear_virtual_state() {
    g_held = g_prevHeld = 0;
    g_stickMX = g_stickMY = g_stickCX = g_stickCY = 0.f;
    for (auto& f : g_fingers) f.active = false;
    recompute_virtual_state();
}

static void draw_ui_buttons() {
    auto& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;
    float sc = getSettings().touch.scale.getValue();

    float btnH   = 36.f * sc;
    float editW  = 72.f * sc;
    float togW   = 60.f * sc;
    float gap    = 4.f * sc;
    float margin = 8.f * sc;
    bool enabled = is_enabled();

    float totalW = enabled ? (editW + gap + togW) : togW;

    ImGui::SetNextWindowPos({w - totalW - margin, h - btnH - margin}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({totalW, btnH}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##tc_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav);

    if (enabled) {
        if (g_customizeMode) {
            if (ImGui::Button("Done##tc", {editW, btnH})) {
                g_customizeMode = false;
                g_dragCtrl   = CTRL_NONE;
                g_dragFinger = 0;
                config::Save();
            }
        } else {
            if (ImGui::Button("Edit##tc", {editW, btnH})) {
                g_customizeMode = true;
                clear_virtual_state();
            }
        }
        ImGui::SameLine(0.f, gap);
    }

    if (ImGui::Button(enabled ? "Hide##tc_tog" : "Touch##tc_tog", {togW, btnH})) {
        bool nowEnabled = !enabled;
        getSettings().touch.enabled.setValue(nowEnabled);
        g_autoDisabled = false; // user has explicitly chosen, clear auto state
        config::Save();
        if (!nowEnabled) {
            g_customizeMode = false;
            clear_virtual_state();
        }
    }

    ImGui::End();
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool is_enabled() {
    return getSettings().touch.enabled.getValue();
}

void handle_event(const SDL_Event& event) {
    if (!is_enabled()) return;
    if (!dusk::IsGameLaunched) return;

    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
        on_finger_down(event.tfinger);
        break;
    case SDL_EVENT_FINGER_MOTION:
        on_finger_motion(event.tfinger);
        break;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        on_finger_up(event.tfinger);
        break;
    default:
        break;
    }
}

void draw() {
    if (!dusk::IsGameLaunched) return;
    if (dusk::ui::any_document_visible()) return;

    if (is_enabled()) {
        draw_controls();
    }
    draw_ui_buttons();
}

void apply_virtual_input(interface_of_controller_pad* pad) {
    if (!is_enabled()) return;
    if (g_customizeMode) {
        // Don't inject input while the user is repositioning buttons
        g_prevHeld = g_held;
        return;
    }

    static constexpr float kPi = 3.14159265f;

    // When the main stick is strongly deflected in one axis, also fire the
    // corresponding D-pad button so analog stick input works in menus that
    // only check PAD_BUTTON_LEFT / RIGHT / UP / DOWN.
    static constexpr float kDpadThreshold = 0.65f;
    uint32_t stickDpad = 0;
    if (std::abs(g_stickMX) >= kDpadThreshold && std::abs(g_stickMX) > std::abs(g_stickMY))
        stickDpad |= (g_stickMX > 0.f) ? PAD_BUTTON_RIGHT : PAD_BUTTON_LEFT;
    if (std::abs(g_stickMY) >= kDpadThreshold && std::abs(g_stickMY) > std::abs(g_stickMX))
        stickDpad |= (g_stickMY > 0.f) ? PAD_BUTTON_UP : PAD_BUTTON_DOWN;

    uint32_t effective = g_held | stickDpad;
    pad->mButtonFlags |= effective;
    uint32_t newPressed = effective & ~g_prevHeld;
    pad->mPressedButtonFlags |= newPressed;
    g_prevHeld = effective;

    // Blend stick only when physical stick is near neutral
    if (std::abs(pad->mMainStickPosX) < 0.1f && std::abs(pad->mMainStickPosY) < 0.1f) {
        if (g_stickMX != 0.f || g_stickMY != 0.f) {
            pad->mMainStickPosX = g_stickMX;
            pad->mMainStickPosY = g_stickMY;
            float len = std::sqrt(g_stickMX * g_stickMX + g_stickMY * g_stickMY);
            pad->mMainStickValue = len;
            // Binary angle: (0x8000/π) * atan2(X, −Y) matches JUTGamePad::CStick::calc
            pad->mMainStickAngle = static_cast<s16>(
                (0x8000 / kPi) * std::atan2f(g_stickMX, -g_stickMY));
        }
    }
    if (std::abs(pad->mCStickPosX) < 0.1f && std::abs(pad->mCStickPosY) < 0.1f) {
        if (g_stickCX != 0.f || g_stickCY != 0.f) {
            pad->mCStickPosX = g_stickCX;
            pad->mCStickPosY = g_stickCY;
            float len = std::sqrt(g_stickCX * g_stickCX + g_stickCY * g_stickCY);
            pad->mCStickValue = len;
            pad->mCStickAngle = static_cast<s16>(
                (0x8000 / kPi) * std::atan2f(g_stickCX, -g_stickCY));
        }
    }
}

void enter_customize_mode() {
    if (!is_enabled()) return;
    g_customizeMode = true;
    clear_virtual_state();
}

void notify_controller_added() {
    ++g_controllerCount;
    // Auto-disable touch when the first physical controller connects,
    // but only if we haven't been manually overridden by the user.
    if (g_controllerCount == 1 && is_enabled() && !g_autoDisabled) {
        getSettings().touch.enabled.setValue(false);
        g_autoDisabled = true;
        config::Save();
        g_customizeMode = false;
        clear_virtual_state();
    }
}

void notify_controller_removed() {
    if (g_controllerCount > 0) {
        --g_controllerCount;
    }
    // Re-enable touch when the last controller disconnects, but only if we
    // were the ones who disabled it automatically.
    if (g_controllerCount == 0 && g_autoDisabled) {
        getSettings().touch.enabled.setValue(true);
        g_autoDisabled = false;
        config::Save();
    }
}

} // namespace dusk::touch_controls
