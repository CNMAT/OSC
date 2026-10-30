/*
 * WiiExtension.h -- XiaoSamd21Oscuino's binding of NintendoExtensionCtrl
 * (Library Manager "Nintendo Extension Ctrl" 0.8.3, by David Madison,
 * LGPL-3.0-or-later, used as installed): the library's port on Wire, a view
 * of that port for every controller type the library knows, and for each
 * type every reading the library offers. /wii carries, for the type plugged
 * in, each public, const, no-argument getter its class declares that returns
 * a number or a boolean (an integer type, float, boolean, or the drums'
 * VelocityID), in the order its header declares them, and for the turntable
 * then those of its left and right platters, which come from the class both
 * are made from (TurntableExpansion, connected() included). Left out:
 * getExpectedType(), which every class declares and which returns the
 * library's ExtensionType, and the getters every class inherits from
 * ExtensionController (getControllerType(), getRequestSize() and the like).
 * Each field is named as the library names it: WII_GET stringizes the call,
 * so the name cannot differ from the getter. Each field's range is the one
 * the library's comment on that getter states, or its type's where none
 * does. Buttons are the library's booleans, 1 while pressed.
 *
 * The page XiaoSamd21Oscuino.html lists the same kinds and fields, and
 * extras/webserial/test/test-xiaosamd21-contract.mjs holds the two, and
 * ADDRESSES.md's "wii fields", to one list, and, where the library is
 * installed, that list to the library's headers.
 */
#ifndef WII_EXTENSION_H
#define WII_EXTENSION_H

#include <NintendoExtensionCtrl.h>

// One port, and a view of it for each type: ExtensionPort::connect() runs the
// matching view's specificInit() (the Classic's try at high-resolution mode
// and read of its data mode, the Drawsome's two register writes), as in the
// library's MultipleTypes example.
static ExtensionPort wiiPort;   // Wire
static Nunchuk::Shared wiiNunchuk(wiiPort);
static ClassicController::Shared wiiClassic(wiiPort);
static GuitarController::Shared wiiGuitar(wiiPort);
static DrumController::Shared wiiDrums(wiiPort);
static DJTurntableController::Shared wiiDJ(wiiPort);
static uDrawTablet::Shared wiiUDraw(wiiPort);
static DrawsomeTablet::Shared wiiDrawsome(wiiPort);

// A field: an integer getter (booleans and the drums' VelocityID included),
// or, with WII_GETF, a float one.
struct WiiField {
  const char *name;
  int32_t lo, hi;
  int32_t (*get)();
  float (*getf)();
};
struct WiiKind {
  const char *name;
  ExtensionType type;
  const WiiField *fields;
  uint8_t count;
};

#define WII_GET(view, getter, lo, hi) \
  { #getter, lo, hi, [] { return (int32_t) view.getter(); }, nullptr }
#define WII_GETF(view, getter, lo, hi) \
  { #getter, lo, hi, nullptr, [] { return (float) view.getter(); } }

static const WiiField WII_NUNCHUK[] = {
  WII_GET(wiiNunchuk, joyX, 0, 255),       // "8 bits, 0-255"
  WII_GET(wiiNunchuk, joyY, 0, 255),
  WII_GET(wiiNunchuk, accelX, 0, 1023),    // "10 bits, 0-1023"
  WII_GET(wiiNunchuk, accelY, 0, 1023),
  WII_GET(wiiNunchuk, accelZ, 0, 1023),
  WII_GET(wiiNunchuk, buttonC, 0, 1),
  WII_GET(wiiNunchuk, buttonZ, 0, 1),
  WII_GETF(wiiNunchuk, rollAngle, -180, 180),   // "-180.0 to 180.0"
  WII_GETF(wiiNunchuk, pitchAngle, -180, 180),
};

// The sticks and triggers are 8 bits in either data mode ("8 bits, 6 shifted
// in std mode", 5 for the right stick and the triggers), so one kind covers
// both; getHighRes() says which mode the controller took.
static const WiiField WII_CLASSIC[] = {
  WII_GET(wiiClassic, getHighRes, 0, 1),
  WII_GET(wiiClassic, leftJoyX, 0, 255),
  WII_GET(wiiClassic, leftJoyY, 0, 255),
  WII_GET(wiiClassic, rightJoyX, 0, 255),
  WII_GET(wiiClassic, rightJoyY, 0, 255),
  WII_GET(wiiClassic, dpadUp, 0, 1),
  WII_GET(wiiClassic, dpadDown, 0, 1),
  WII_GET(wiiClassic, dpadLeft, 0, 1),
  WII_GET(wiiClassic, dpadRight, 0, 1),
  WII_GET(wiiClassic, buttonA, 0, 1),
  WII_GET(wiiClassic, buttonB, 0, 1),
  WII_GET(wiiClassic, buttonX, 0, 1),
  WII_GET(wiiClassic, buttonY, 0, 1),
  WII_GET(wiiClassic, triggerL, 0, 255),
  WII_GET(wiiClassic, triggerR, 0, 255),
  WII_GET(wiiClassic, buttonL, 0, 1),
  WII_GET(wiiClassic, buttonR, 0, 1),
  WII_GET(wiiClassic, buttonZL, 0, 1),
  WII_GET(wiiClassic, buttonZR, 0, 1),
  WII_GET(wiiClassic, buttonStart, 0, 1),
  WII_GET(wiiClassic, buttonSelect, 0, 1),
  WII_GET(wiiClassic, buttonPlus, 0, 1),
  WII_GET(wiiClassic, buttonMinus, 0, 1),
  WII_GET(wiiClassic, buttonHome, 0, 1),
};

static const WiiField WII_GUITAR[] = {
  WII_GET(wiiGuitar, joyX, 0, 63),         // "6 bits, 0-63"
  WII_GET(wiiGuitar, joyY, 0, 63),
  WII_GET(wiiGuitar, strum, 0, 1),
  WII_GET(wiiGuitar, strumUp, 0, 1),
  WII_GET(wiiGuitar, strumDown, 0, 1),
  WII_GET(wiiGuitar, fretGreen, 0, 1),
  WII_GET(wiiGuitar, fretRed, 0, 1),
  WII_GET(wiiGuitar, fretYellow, 0, 1),
  WII_GET(wiiGuitar, fretBlue, 0, 1),
  WII_GET(wiiGuitar, fretOrange, 0, 1),
  WII_GET(wiiGuitar, whammyBar, 0, 31),    // "5 bits, 0-31 (starting at ~15-16)"
  WII_GET(wiiGuitar, touchbar, 0, 31),     // "5 bits, 0-31"
  WII_GET(wiiGuitar, touchGreen, 0, 1),
  WII_GET(wiiGuitar, touchRed, 0, 1),
  WII_GET(wiiGuitar, touchYellow, 0, 1),
  WII_GET(wiiGuitar, touchBlue, 0, 1),
  WII_GET(wiiGuitar, touchOrange, 0, 1),
  WII_GET(wiiGuitar, buttonPlus, 0, 1),
  WII_GET(wiiGuitar, buttonMinus, 0, 1),
};

// velocityID() is the library's VelocityID, a "5 bit identifier" for the pad
// the velocity is for (0x1F, None, when it is not a valid one).
static const WiiField WII_DRUMS[] = {
  WII_GET(wiiDrums, joyX, 0, 63),          // "6 bits, 0-63"
  WII_GET(wiiDrums, joyY, 0, 63),
  WII_GET(wiiDrums, drumRed, 0, 1),
  WII_GET(wiiDrums, drumBlue, 0, 1),
  WII_GET(wiiDrums, drumGreen, 0, 1),
  WII_GET(wiiDrums, cymbalYellow, 0, 1),
  WII_GET(wiiDrums, cymbalOrange, 0, 1),
  WII_GET(wiiDrums, bassPedal, 0, 1),
  WII_GET(wiiDrums, buttonPlus, 0, 1),
  WII_GET(wiiDrums, buttonMinus, 0, 1),
  WII_GET(wiiDrums, velocityAvailable, 0, 1),
  WII_GET(wiiDrums, velocityID, 0, 31),
  WII_GET(wiiDrums, velocity, 0, 7),       // "3 bits, 0-7. 7 is fast/hard, 1 is slow/soft, 0 is not hit."
  WII_GET(wiiDrums, velocityRed, 0, 7),
  WII_GET(wiiDrums, velocityBlue, 0, 7),
  WII_GET(wiiDrums, velocityGreen, 0, 7),
  WII_GET(wiiDrums, velocityYellow, 0, 7),
  WII_GET(wiiDrums, velocityOrange, 0, 7),
  WII_GET(wiiDrums, velocityPedal, 0, 7),
};

// Each platter's turntable() is -32..31 by the library's code: five bits and
// a sign bit, sign-extended (TurntableExpansion::getTurntableSpeed). The
// combined turntable() is left's plus right's (DJTurntable.cpp), so -64..62,
// where the header's comment on it says "6 bits, -30-29".
static const WiiField WII_TURNTABLE[] = {
  WII_GET(wiiDJ, turntable, -64, 62),
  WII_GET(wiiDJ, buttonGreen, 0, 1),
  WII_GET(wiiDJ, buttonRed, 0, 1),
  WII_GET(wiiDJ, buttonBlue, 0, 1),
  WII_GET(wiiDJ, effectDial, 0, 31),       // "5 bits, 0-31. One rotation per rollover."
  WII_GET(wiiDJ, crossfadeSlider, 0, 15),  // "4 bits, 0-15. Left to right."
  WII_GET(wiiDJ, buttonEuphoria, 0, 1),
  WII_GET(wiiDJ, joyX, 0, 63),             // "6 bits, 0-63"
  WII_GET(wiiDJ, joyY, 0, 63),
  WII_GET(wiiDJ, buttonPlus, 0, 1),
  WII_GET(wiiDJ, buttonMinus, 0, 1),
  WII_GET(wiiDJ, left.connected, 0, 1),
  WII_GET(wiiDJ, left.turntable, -32, 31),
  WII_GET(wiiDJ, left.buttonGreen, 0, 1),
  WII_GET(wiiDJ, left.buttonRed, 0, 1),
  WII_GET(wiiDJ, left.buttonBlue, 0, 1),
  WII_GET(wiiDJ, right.connected, 0, 1),
  WII_GET(wiiDJ, right.turntable, -32, 31),
  WII_GET(wiiDJ, right.buttonGreen, 0, 1),
  WII_GET(wiiDJ, right.buttonRed, 0, 1),
  WII_GET(wiiDJ, right.buttonBlue, 0, 1),
};

static const WiiField WII_UDRAW[] = {
  WII_GET(wiiUDraw, penX, 0, 4095),        // "12 bits, 0-4095"
  WII_GET(wiiUDraw, penY, 0, 4095),
  WII_GET(wiiUDraw, penPressure, 0, 511),  // "9 bits, 0-511"
  WII_GET(wiiUDraw, buttonLower, 0, 1),
  WII_GET(wiiUDraw, buttonUpper, 0, 1),
  WII_GET(wiiUDraw, penDetected, 0, 1),
};

static const WiiField WII_DRAWSOME[] = {
  WII_GET(wiiDrawsome, penX, 0, 65535),    // "16 bits, 0-65535"
  WII_GET(wiiDrawsome, penY, 0, 65535),
  WII_GET(wiiDrawsome, penPressure, 0, 4095),   // "12 bits, 0-4095"
  WII_GET(wiiDrawsome, penDetected, 0, 1),
};

#define WII_COUNT_OF(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

static const WiiKind WII_KINDS[] = {
  { "Nunchuk", ExtensionType::Nunchuk, WII_NUNCHUK, WII_COUNT_OF(WII_NUNCHUK) },
  { "Classic", ExtensionType::ClassicController, WII_CLASSIC, WII_COUNT_OF(WII_CLASSIC) },
  { "Guitar", ExtensionType::GuitarController, WII_GUITAR, WII_COUNT_OF(WII_GUITAR) },
  { "Drums", ExtensionType::DrumController, WII_DRUMS, WII_COUNT_OF(WII_DRUMS) },
  { "Turntable", ExtensionType::DJTurntableController, WII_TURNTABLE, WII_COUNT_OF(WII_TURNTABLE) },
  { "uDraw", ExtensionType::uDrawTablet, WII_UDRAW, WII_COUNT_OF(WII_UDRAW) },
  { "Drawsome", ExtensionType::DrawsomeTablet, WII_DRAWSOME, WII_COUNT_OF(WII_DRAWSOME) },
};
static const uint8_t WII_KIND_COUNT = WII_COUNT_OF(WII_KINDS);

// The WII_KINDS entry for the type the library identified, or -1.
static inline int8_t wiiKindOf(ExtensionType t) {
  for (uint8_t k = 0; k < WII_KIND_COUNT; k++)
    if (WII_KINDS[k].type == t) return (int8_t) k;
  return -1;
}

#endif
