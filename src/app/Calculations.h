#pragma once
#include "AppState.h"

namespace calc {

float volA();                // ml from preset A
float volB();                // ml from preset B
// Объём дозы: в CHARGED — по оставшемуся ходу H − L (что реально выдавится),
// в DOSING/DONE — плановый объём этой дозы, иначе — полный объём шприца из пресета.
float doseVolA();
float doseVolB();
float flowA();               // ml/min for syringe A (по объёму дозы)
float flowB();               // ml/min for syringe B (area-corrected)
float screwSpeedMmPerMin();  // linear screw speed
float motorRpm();            // motor revolutions per minute

// Объём (мл) при линейном ходе distMm мм для шприца диаметром diameterMm.
float distToVolumeMl(float diameterMm, float distMm);

struct TimeRange { float minMin; float maxMin; };
TimeRange timeRangeMin();    // allowed dose-time range [min] for current dose volume/diam

}  // namespace calc
