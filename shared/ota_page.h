#pragma once
/**
 * Страница обновления прошивки — одна на дозатор и пульт.
 *
 * Устройство отдаёт её на GET /update (у пульта ещё и на «/»), а также:
 *   GET  /info    → {"device":"dozator"|"remote","title":"…","version":"…","busy":"" | "причина"}
 *   POST /update?size=N  (multipart, поле "firmware") → 200 "OK" или текст ошибки.
 *
 * До загрузки страница читает файл в браузере: образ ESP (первый байт 0xE9) и метка
 * "DOZFW:<устройство>:<версия>:" (src/version.cpp, kFwMarker; ссылка на неё — в main.cpp). Прошивку пульта в дозатор (и
 * наоборот) не пустит, покажет, какая версия заливается поверх какой.
 */
#include <Arduino.h>

static const char OTA_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Обновление прошивки</title>
<style>
:root{--bg:#0f1115;--card:#181b22;--line:#2a2f3a;--text:#e8ebf0;--muted:#8b93a3;--pri:#2f7de1;--ok:#34c759;--err:#e0473e;--warn:#e6a417}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,sans-serif;padding:16px}
.card{max-width:480px;margin:0 auto;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:18px}
h1{font-size:20px;margin:0 0 4px}
.muted{color:var(--muted);font-size:14px}
.row{margin:16px 0}
input[type=file]{width:100%;color:var(--text)}
button{width:100%;padding:14px;border:0;border-radius:10px;background:var(--pri);color:#fff;font-size:17px;font-weight:600}
button:disabled{opacity:.4}
.bar{height:10px;background:var(--line);border-radius:5px;overflow:hidden}
.bar>div{height:100%;width:0;background:var(--pri);transition:width .2s}
.msg{min-height:1.4em}
.ok{color:var(--ok)}.err{color:var(--err)}.warn{color:var(--warn)}
code{font-size:14px}
</style></head><body>
<div class="card">
  <h1 id="title">Обновление прошивки</h1>
  <div class="muted">Сейчас: <code id="cur">…</code></div>
  <div class="row warn" id="busy" hidden></div>
  <div class="row"><input type="file" id="file" accept=".bin,application/octet-stream"></div>
  <div class="row msg" id="info"></div>
  <div class="row"><button id="go" disabled>Обновить</button></div>
  <div class="row bar"><div id="prog"></div></div>
  <div class="row msg" id="msg"></div>
</div>
<script>
const $ = (id) => document.getElementById(id);
let dev = null, file = null, fileOk = false;

function say(el, text, cls) { $(el).textContent = text; $(el).className = 'row msg ' + (cls || ''); }
function updateBtn() { $('go').disabled = !(dev && fileOk && !dev.busy); }

async function loadInfo() {
  try {
    dev = await (await fetch('/info', {cache: 'no-store'})).json();
    $('title').textContent = 'Обновление: ' + dev.title;
    $('cur').textContent = dev.version;
    $('busy').hidden = !dev.busy;
    $('busy').textContent = dev.busy || '';
  } catch (e) { dev = null; $('cur').textContent = 'нет связи с устройством'; }
  updateBtn();
}

// Метка прошивки внутри образа: префикс (байты 68,79,90,70,87,58), имя устройства, версия.
// Префикс встречается и в тексте самой страницы — берём только метку с известным именем.
function findMarker(bytes) {
  const pat = [68, 79, 90, 70, 87, 58];
  outer: for (let i = 0; i + 64 < bytes.length; i++) {
    for (let j = 0; j < pat.length; j++) if (bytes[i + j] !== pat[j]) continue outer;
    let s = '';
    for (let k = i + 6; k < i + 64 && bytes[k]; k++) s += String.fromCharCode(bytes[k]);
    const p = s.split(':');
    if (p.length >= 3 && (p[0] === 'dozator' || p[0] === 'remote') && /^[0-9]/.test(p[1]))
      return {name: p[0], version: p[1]};
  }
  return null;
}

$('file').onchange = async () => {
  file = $('file').files[0]; fileOk = false; say('msg', '');
  if (!file) { say('info', ''); updateBtn(); return; }
  const bytes = new Uint8Array(await file.arrayBuffer());
  const m = findMarker(bytes);
  const names = {dozator: 'дозатора', remote: 'пульта'};
  if (bytes[0] !== 0xE9 || !m) {
    say('info', 'Это не прошивка дозатора или пульта.', 'err');
  } else if (dev && m.name !== dev.device) {
    say('info', 'Это прошивка ' + (names[m.name] || m.name) + ', а здесь ' + dev.title.toLowerCase() + '.', 'err');
  } else {
    fileOk = true;
    say('info', 'Новая версия: ' + m.version + ' (' + Math.round(file.size / 1024) + ' КБ)');
  }
  updateBtn();
};

$('go').onclick = () => {
  if (!file) return;
  $('go').disabled = true; $('file').disabled = true;
  say('msg', 'Загрузка… Не закрывай страницу и не выключай устройство.');
  const fd = new FormData();
  fd.append('firmware', file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update?size=' + file.size);
  xhr.upload.onprogress = (e) => { if (e.lengthComputable) $('prog').style.width = (100 * e.loaded / e.total) + '%'; };
  xhr.onload = () => {
    if (xhr.status === 200 && xhr.responseText.trim() === 'OK') {
      $('prog').style.width = '100%';
      if (dev.device === 'remote') {
        say('msg', 'Готово. Пульт перезагружается в обычный режим — его сеть пропадёт.', 'ok');
      } else {
        say('msg', 'Готово. Дозатор перезагружается…', 'ok');
        setTimeout(async () => { await loadInfo(); say('msg', 'Готово. Сейчас: ' + (dev ? dev.version : '?'), 'ok'); }, 9000);
      }
    } else {
      say('msg', 'Ошибка: ' + (xhr.responseText || ('HTTP ' + xhr.status)), 'err');
      $('file').disabled = false; loadInfo();
    }
  };
  xhr.onerror = () => { say('msg', 'Связь оборвалась. Обнови страницу и попробуй ещё раз.', 'err'); $('file').disabled = false; };
  xhr.send(fd);
};

loadInfo();
setInterval(() => { if (!$('file').disabled) loadInfo(); }, 5000);
</script>
</body></html>)HTML";
