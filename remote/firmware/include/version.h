#pragma once
// Версия прошивки: номер из файла VERSION + хеш коммита (tools/fw_version.py).
extern const char kFwName[];      // "remote"
extern const char kFwVersion[];   // например "1.1.0+a3f9c1e", «*» — незакоммиченные изменения
// Метка для страницы обновления. На неё обязательно должна быть ссылка в коде
// (лог при старте) — иначе линкер выкинет строку, и страница не узнает прошивку.
extern "C" const char kFwMarker[];
