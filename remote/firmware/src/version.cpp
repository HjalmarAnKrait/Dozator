#include "version.h"
#include "fw_version_gen.h"

const char kFwName[]    = FW_NAME;
const char kFwVersion[] = FW_VERSION;
// Метка в образе прошивки: страница обновления ищет её в файле и не даст залить
// прошивку пульта в дозатор (и наоборот). Формат одинаковый у обеих прошивок.
extern "C" __attribute__((used)) const char kFwMarker[] = "DOZFW:" FW_NAME ":" FW_VERSION ":";
