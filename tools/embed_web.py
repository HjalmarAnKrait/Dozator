# PlatformIO extra script (pre): веб-интерфейс дозатора внутрь прошивки.
#
# data/index.html, app.css, app.js сжимаются gzip и пишутся массивами в
# src/net/web_assets_gen.h. Так обновление дозатора — один файл .bin, и веб не
# может разойтись с прошивкой по версии. data/ остаётся исходником (его же отдаёт
# мок-сервер dev/). Файл переписывается, только если содержимое поменялось.
Import("env")
import gzip, os

proj = env.subst("$PROJECT_DIR")
ASSETS = [("index.html", "text/html"), ("app.css", "text/css"), ("app.js", "application/javascript")]

lines = [
    "// Сгенерировано tools/embed_web.py из data/ — не править и не коммитить.",
    "#pragma once",
    "#include <Arduino.h>",
    "",
    "struct WebAsset { const char* path; const char* type; const uint8_t* gz; size_t len; };",
    "",
]
entries = []
total = 0
for i, (fname, mime) in enumerate(ASSETS):
    raw = open(os.path.join(proj, "data", fname), "rb").read()
    gz = gzip.compress(raw, compresslevel=9, mtime=0)
    total += len(gz)
    body = ",".join(str(b) for b in gz)
    lines.append("static const uint8_t WEB_GZ_%d[] PROGMEM = {%s};" % (i, body))
    entries.append('    {"/%s", "%s", WEB_GZ_%d, %d},' % (fname, mime, i, len(gz)))
lines += ["", "static const WebAsset WEB_ASSETS[] = {"] + entries + ["};", ""]
text = "\n".join(lines)

out = os.path.join(proj, "src", "net", "web_assets_gen.h")
old = open(out).read() if os.path.exists(out) else ""
if old != text:
    with open(out, "w") as f:
        f.write(text)
print("Web UI embedded: %d bytes gzip" % total)
