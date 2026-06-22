#pragma once
#include <U8g2lib.h>
#include "../../app/AppState.h"

// Each screen module exports one render function with this signature.
// Functions are declared here and defined in individual *.cpp files.

void renderParking        (U8G2& oled, const AppState& s);
void renderParked         (U8G2& oled, const AppState& s);
void renderCharging       (U8G2& oled, const AppState& s);
void renderCharged        (U8G2& oled, const AppState& s);
void renderDosing         (U8G2& oled, const AppState& s);
void renderDone           (U8G2& oled, const AppState& s);
void renderServiceMenu    (U8G2& oled, const AppState& s);
void renderServicePitch   (U8G2& oled, const AppState& s);
void renderServiceSleep   (U8G2& oled, const AppState& s);
void renderServiceSyringes(U8G2& oled, const AppState& s);
void renderServiceSyringeEdit(U8G2& oled, const AppState& s);
void renderServiceFont       (U8G2& oled, const AppState& s);
