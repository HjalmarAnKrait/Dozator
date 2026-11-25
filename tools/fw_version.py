# PlatformIO extra script (pre): версия прошивки и релизный файл.
#
# Версия = номер из файла VERSION в папке проекта + короткий хеш коммита,
# «*» в конце — сборка из незакоммиченных изменений. Пишется в
# include/fw_version_gen.h (только если поменялась — иначе всё пересобиралось бы).
# После сборки firmware.bin копируется в <repo>/release/<имя>-<версия>.bin.
#
# В platformio.ini:  extra_scripts = pre:<путь>/tools/fw_version.py
#                    custom_fw_name = dozator | remote
Import("env")
import os, shutil, subprocess

proj = env.subst("$PROJECT_DIR")
name = env.GetProjectOption("custom_fw_name")


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=proj, stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return None


base = open(os.path.join(proj, "VERSION")).read().strip()
commit = git("rev-parse", "--short=7", "HEAD")
if commit:
    dirty = "*" if git("status", "--porcelain", "--untracked-files=no") else ""
    version = "%s+%s%s" % (base, commit, dirty)
else:
    version = base

header = os.path.join(proj, "include", "fw_version_gen.h")
text = (
    "// Сгенерировано tools/fw_version.py при сборке — не править и не коммитить.\n"
    "#pragma once\n"
    '#define FW_NAME    "%s"\n'
    '#define FW_VERSION "%s"\n' % (name, version)
)
old = open(header).read() if os.path.exists(header) else ""
if old != text:
    with open(header, "w") as f:
        f.write(text)
print("Firmware %s %s" % (name, version))

root = git("rev-parse", "--show-toplevel") or proj
release_dir = os.path.join(root, "release")


def copy_release(source, target, env):
    os.makedirs(release_dir, exist_ok=True)
    dst = os.path.join(release_dir, "%s-%s.bin" % (name, version.replace("*", "-dirty")))
    shutil.copyfile(str(target[0]), dst)
    print("Release: %s" % os.path.relpath(dst, root))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_release)
