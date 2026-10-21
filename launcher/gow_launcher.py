"""God of War: Ascension Recompiled -- player launcher.

A small Tk front end for MyGameRecomp.exe: it keeps the paths and options in
launcher/settings.json, checks the setup, starts the game with the matching
environment, keeps timestamped logs and backs up / restores the save data.

Only the Python 3 standard library is used. The pure functions at the top
(build_env, parse_extra_env, check_setup, backup_saves, ...) do not touch Tk,
so launcher/test_launcher.py can test them without a display.

This file contains no game data and no paths to any; the player points it at
their own build and their own dump.
"""

import datetime
import json
import os
import subprocess
import sys
import zipfile

LAUNCHER_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(LAUNCHER_DIR)
SETTINGS_PATH = os.path.join(LAUNCHER_DIR, "settings.json")
LOG_DIR = os.path.join(LAUNCHER_DIR, "logs")
BACKUP_DIR = os.path.join(LAUNCHER_DIR, "backups")

TITLE_ID = "BCUS98232"
SAVE_SUBDIR = os.path.join("dev_hdd0", "home", "00000001", "savedata")

# CROSS (CELL_PAD_CTRL_CROSS = 0x4000) pressed at 40 s skips the opening movie
# (docs/DEBUGGING.md, "Faster runs"; parser in ps3recomp libs/input/cellPad.c).
SKIP_INTRO_PAD_SCRIPT = "40:0x4000"

# Language codes accepted by PS3_LANG (ps3recomp libs/system/cellSysutil.c).
# Blank = follow the Windows language. Only languages on your disc have text.
LANGUAGES = ["", "en", "en-gb", "fr", "es", "de", "it", "nl", "pt-br", "pt",
             "ru", "ko", "zh-t", "zh-s", "fi", "sv", "da", "no", "pl", "tr"]

DEFAULT_SETTINGS = {
    "exe_path": "",          # <build dir>/MyGameRecomp.exe
    "eboot_path": "",        # <vfs root>/PS3_GAME/USRDIR/EBOOT.elf
    "gamedata_root": "",     # PS3_GAMEDATA_ROOT; blank = <exe dir>/gamedata
    "hdd1_root": "",         # PS3_HDD1_ROOT; blank = <vfs root>/cache
    "window": True,          # PS3RECOMP_VK_WINDOW
    "fullscreen": False,     # PS3RECOMP_VK_FULLSCREEN
    "guest_programs": True,  # PS3RECOMP_VK_GUEST_PROGRAMS
    "vk_device": "",         # PS3RECOMP_VK_DEVICE
    "language": "",          # PS3_LANG
    "skip_intro": False,     # PAD_SCRIPT=40:0x4000
    "release_guard": True,   # GOW_RELEASE_GUARD (0 = off)
    "console": False,        # own console window instead of a log file
    "keep_logs": 10,
    "extra_env": "",         # KEY=VALUE lines, applied last
    "ui_lang": "en",         # launcher UI language: en | pt
}

# Player-facing knobs. Each one was checked against the source that reads it.
KNOB_HELP = {
    "window": ("PS3RECOMP_VK_WINDOW=1",
               "Show the game in a window (ps3recomp libs/video/rsx_vulkan_backend.c). "
               "Off = headless, nothing on screen."),
    "fullscreen": ("PS3RECOMP_VK_FULLSCREEN=1",
                   "Borderless full screen; needs the window (rsx_vulkan_backend.c)."),
    "guest_programs": ("PS3RECOMP_VK_GUEST_PROGRAMS=1",
                       "Run the game's own shaders (needs a build with glslang and "
                       "SPIRV-Tools DLLs next to the exe). Off = black frames."),
    "vk_device": ("PS3RECOMP_VK_DEVICE=<n>",
                  "Vulkan GPU index. Blank = first discrete GPU."),
    "language": ("PS3_LANG=<code>",
                 "System language reported to the game (cellSysutil.c). "
                 "Blank = Windows language. Only languages on your disc work."),
    "skip_intro": ("PAD_SCRIPT=" + SKIP_INTRO_PAD_SCRIPT,
                   "Presses CROSS at 40 s, which skips the opening movie "
                   "(cellPad.c; docs/DEBUGGING.md 'Faster runs')."),
    "release_guard": ("GOW_RELEASE_GUARD=0 when unticked",
                      "Stability mitigation for a VRAM-cache release bug "
                      "(port/gow_vram_guard.c). Keep it on."),
    "gamedata_root": ("PS3_GAMEDATA_ROOT",
                      "Folder that holds dev_hdd0/.../savedata (cellSaveData.c). "
                      "Blank = <exe folder>/gamedata."),
    "hdd1_root": ("PS3_HDD1_ROOT",
                  "Writable cache folder for /dev_hdd1 (ppu_fs.cpp). "
                  "Blank = <game folder>/cache."),
    "console": ("(no env var)",
                "Run in its own console window instead of writing launcher/logs/*.log."),
    "extra_env": ("KEY=VALUE per line",
                  "Advanced: extra environment variables, applied last, so they "
                  "override every option above. Lines starting with # are ignored."),
}

# --------------------------------------------------------------------------
# Settings
# --------------------------------------------------------------------------


def load_settings(path=SETTINGS_PATH):
    """Defaults merged with the JSON file (if any). Never writes."""
    settings = dict(DEFAULT_SETTINGS)
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            for key in DEFAULT_SETTINGS:
                if key in data:
                    settings[key] = data[key]
    except (OSError, ValueError):
        pass
    return settings


def save_settings(settings, path=SETTINGS_PATH):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=2, sort_keys=True)
        f.write("\n")


def guess_paths(settings, repo_root=REPO_ROOT):
    """Fill a blank exe path from the usual build locations (docs/BUILDING.md)."""
    if not settings.get("exe_path"):
        for rel in (("local", "port", "build_vk"), ("port", "build_vk")):
            cand = os.path.join(repo_root, *rel, "MyGameRecomp.exe")
            if os.path.isfile(cand):
                settings["exe_path"] = cand
                break
    if not settings.get("eboot_path"):
        cand = os.path.join(repo_root, "local", "vfs", "PS3_GAME", "USRDIR", "EBOOT.elf")
        if os.path.isfile(cand):
            settings["eboot_path"] = cand
    return settings


# --------------------------------------------------------------------------
# Environment
# --------------------------------------------------------------------------


def parse_extra_env(text):
    """KEY=VALUE lines -> (dict, list of error strings). Blank lines and # comments skipped."""
    env, errors = {}, []
    for num, raw in enumerate((text or "").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            errors.append("line %d: expected KEY=VALUE, got %r" % (num, line))
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if not key or any(c.isspace() for c in key):
            errors.append("line %d: bad variable name %r" % (num, key))
            continue
        env[key] = value.strip()
    return env, errors


def effective_gamedata_root(settings):
    """The folder the game saves under: the setting, else <exe dir>/gamedata.

    The runtime's default is the cwd-relative ./gamedata; the launcher always
    runs the exe from its own folder and passes the root explicitly, so the
    game and the backup buttons always look at the same place."""
    root = (settings.get("gamedata_root") or "").strip()
    if root:
        return os.path.abspath(root)
    exe = (settings.get("exe_path") or "").strip()
    if not exe:
        return ""
    return os.path.join(os.path.dirname(os.path.abspath(exe)), "gamedata")


def savedata_dir(settings):
    root = effective_gamedata_root(settings)
    return os.path.join(root, SAVE_SUBDIR) if root else ""


def build_env(settings, base_env=None):
    """Environment for the game process. Pure: no I/O.

    Order: base environment, then the options, then the extra KEY=VALUE lines
    (which therefore win)."""
    env = dict(os.environ if base_env is None else base_env)

    # Same as scripts/play.bat: keep the lifter's function tracer quiet when the
    # build was lifted with tracing (PPU_TRACE_MAX defaults to 4000 lines).
    env["PPU_TRACE_ADDRS"] = "0"
    env["PPU_TRACE_MAX"] = "0"

    def flag(name, on):
        if on:
            env[name] = "1"
        else:
            env.pop(name, None)

    flag("PS3RECOMP_VK_WINDOW", bool(settings.get("window", True)))
    flag("PS3RECOMP_VK_FULLSCREEN", bool(settings.get("fullscreen")) and bool(settings.get("window", True)))
    flag("PS3RECOMP_VK_GUEST_PROGRAMS", bool(settings.get("guest_programs", True)))

    device = str(settings.get("vk_device") or "").strip()
    if device:
        env["PS3RECOMP_VK_DEVICE"] = device
    lang = str(settings.get("language") or "").strip()
    if lang:
        env["PS3_LANG"] = lang

    if settings.get("skip_intro"):
        env["PAD_SCRIPT"] = SKIP_INTRO_PAD_SCRIPT

    # The guard is on unless the variable starts with '0' (gow_vram_guard.c).
    if settings.get("release_guard", True):
        env.pop("GOW_RELEASE_GUARD", None)
    else:
        env["GOW_RELEASE_GUARD"] = "0"

    gamedata = effective_gamedata_root(settings)
    if gamedata:
        env["PS3_GAMEDATA_ROOT"] = gamedata
    hdd1 = (settings.get("hdd1_root") or "").strip()
    if hdd1:
        env["PS3_HDD1_ROOT"] = os.path.abspath(hdd1)

    extra, _errors = parse_extra_env(settings.get("extra_env", ""))
    env.update(extra)
    return env


def build_command(settings):
    exe = os.path.abspath(settings["exe_path"])
    return [exe, os.path.abspath(settings["eboot_path"])], os.path.dirname(exe)


# --------------------------------------------------------------------------
# Setup check ("doctor"). Never runs the exe.
# --------------------------------------------------------------------------

OK, WARN, ERR = "ok", "warning", "error"


def read_archive_list(path):
    """ARCHIVES / LOCALIZED_ARCHIVES: first line is a count, then one file per line."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = [ln.strip() for ln in f.read().splitlines()]
    lines = [ln for ln in lines if ln]
    if lines and lines[0].isdigit():
        lines = lines[1:]
    return lines


def check_setup(settings):
    """List of (level, message). level is OK, WARN or ERR."""
    out = []
    exe = (settings.get("exe_path") or "").strip()
    eboot = (settings.get("eboot_path") or "").strip()

    if not exe:
        out.append((ERR, "Game exe not set (MyGameRecomp.exe in your build folder)."))
    elif not os.path.isfile(exe):
        out.append((ERR, "Game exe not found: %s" % exe))
    else:
        out.append((OK, "Game exe: %s" % exe))
        exe_dir = os.path.dirname(os.path.abspath(exe))
        dlls = sorted(n for n in os.listdir(exe_dir) if n.lower().endswith(".dll"))
        lower = [n.lower() for n in dlls]
        if not dlls:
            out.append((ERR, "No DLLs next to the exe (SDL2.dll, FFmpeg, SPIRV-Tools ...)."))
        else:
            out.append((OK, "%d DLL(s) next to the exe." % len(dlls)))
        if dlls and "sdl2.dll" not in lower:
            out.append((ERR, "SDL2.dll missing next to the exe."))
        if settings.get("guest_programs", True) and dlls and \
                not any(n.startswith("spirv-tools") for n in lower):
            out.append((WARN, "SPIRV-Tools DLLs not found; guest shaders need them "
                              "(docs/BUILDING.md, 'Guest shaders')."))
        if dlls and not any(n.startswith("avcodec") for n in lower):
            out.append((WARN, "No FFmpeg (avcodec*.dll): movies will play black. Optional."))

    if not eboot:
        out.append((ERR, "EBOOT.elf not set (<game folder>/PS3_GAME/USRDIR/EBOOT.elf)."))
    elif not os.path.isfile(eboot):
        out.append((ERR, "EBOOT.elf not found: %s" % eboot))
    else:
        out.append((OK, "EBOOT.elf: %s" % eboot))
        usrdir = os.path.dirname(os.path.abspath(eboot))
        ps3_game = os.path.dirname(usrdir)
        if os.path.basename(usrdir).upper() != "USRDIR" or \
                os.path.basename(ps3_game).upper() != "PS3_GAME":
            out.append((WARN, "EBOOT.elf should sit in <game folder>/PS3_GAME/USRDIR/; "
                              "the game folder is derived from that path."))
        out.extend(_check_archives(usrdir))

    root = effective_gamedata_root(settings)
    if root:
        saves = savedata_dir(settings)
        found = list_save_folders(saves)
        if found:
            out.append((OK, "Save data: %d folder(s) in %s" % (len(found), saves)))
        else:
            out.append((OK, "No save data yet (will be created in %s)." % saves))
    hdd1 = (settings.get("hdd1_root") or "").strip()
    if hdd1 and not os.path.isdir(hdd1):
        out.append((WARN, "Cache folder does not exist yet: %s" % hdd1))

    extra, errors = parse_extra_env(settings.get("extra_env", ""))
    for e in errors:
        out.append((ERR, "Extra environment, " + e))
    return out


def _check_archives(usrdir):
    out = []
    arch = os.path.join(usrdir, "ARCHIVES")
    if not os.path.isfile(arch):
        return [(ERR, "ARCHIVES file not found in %s" % usrdir)]
    names = read_archive_list(arch)
    missing = [n for n in names if not os.path.isfile(os.path.join(usrdir, n))]
    if missing:
        out.append((ERR, "%d of %d archives missing: %s" % (len(missing), len(names), ", ".join(missing))))
    else:
        out.append((OK, "All %d archives listed in ARCHIVES are present." % len(names)))
    loc = os.path.join(usrdir, "LOCALIZED_ARCHIVES")
    if os.path.isfile(loc):
        lnames = read_archive_list(loc)
        have = [n for n in lnames if os.path.isfile(os.path.join(usrdir, n))]
        if not have:
            out.append((ERR, "None of the language archives (LOCALIZED_ARCHIVES) are present."))
        elif len(have) < len(lnames):
            out.append((WARN, "Language archives present: %s (others missing; fine if you "
                              "do not use those languages)." % ", ".join(have)))
        else:
            out.append((OK, "All %d language archives present." % len(lnames)))
    return out


# --------------------------------------------------------------------------
# Logs
# --------------------------------------------------------------------------


def new_log_path(log_dir=LOG_DIR, now=None):
    now = now or datetime.datetime.now()
    os.makedirs(log_dir, exist_ok=True)
    return os.path.join(log_dir, "gow_%s.log" % now.strftime("%Y%m%d_%H%M%S"))


def list_logs(log_dir=LOG_DIR):
    if not os.path.isdir(log_dir):
        return []
    names = sorted(n for n in os.listdir(log_dir) if n.startswith("gow_") and n.endswith(".log"))
    return [os.path.join(log_dir, n) for n in names]


def rotate_logs(keep, log_dir=LOG_DIR):
    """Delete the oldest logs so at most `keep` remain. Returns the deleted paths."""
    logs = list_logs(log_dir)
    keep = max(1, int(keep))
    removed = []
    for path in logs[:-keep] if len(logs) > keep else []:
        try:
            os.remove(path)
            removed.append(path)
        except OSError:
            pass
    return removed


# --------------------------------------------------------------------------
# Save backup / restore
# --------------------------------------------------------------------------


def list_save_folders(saves):
    if not saves or not os.path.isdir(saves):
        return []
    return sorted(n for n in os.listdir(saves)
                  if n.startswith(TITLE_ID) and os.path.isdir(os.path.join(saves, n)))


def backup_saves(settings, backup_dir=BACKUP_DIR, now=None):
    """Zip every <savedata>/BCUS98232* folder. Returns the zip path, or None if nothing to save."""
    saves = savedata_dir(settings)
    folders = list_save_folders(saves)
    if not folders:
        return None
    now = now or datetime.datetime.now()
    os.makedirs(backup_dir, exist_ok=True)
    path = os.path.join(backup_dir, "saves_%s.zip" % now.strftime("%Y%m%d_%H%M%S"))
    n = 1
    while os.path.exists(path):
        path = os.path.join(backup_dir, "saves_%s_%d.zip" % (now.strftime("%Y%m%d_%H%M%S"), n))
        n += 1
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for folder in folders:
            top = os.path.join(saves, folder)
            for dirpath, _dirs, files in os.walk(top):
                for name in files:
                    full = os.path.join(dirpath, name)
                    z.write(full, os.path.relpath(full, saves).replace(os.sep, "/"))
    return path


def restore_backup(zip_path, settings):
    """Extract a backup into the save folder. Only BCUS98232* entries that stay
    inside the save folder are written. Returns the list of restored files."""
    saves = savedata_dir(settings)
    if not saves:
        raise ValueError("Save-data folder unknown: set the exe path or the save-data root.")
    target = os.path.abspath(saves)
    restored = []
    with zipfile.ZipFile(zip_path) as z:
        members = []
        for info in z.infolist():
            name = info.filename.replace("\\", "/")
            if info.is_dir() or not name.startswith(TITLE_ID):
                continue
            dest = os.path.abspath(os.path.join(target, *name.split("/")))
            if os.path.commonpath([target, dest]) != target:
                continue
            members.append((info, dest))
        if not members:
            raise ValueError("No %s save folders in %s" % (TITLE_ID, zip_path))
        for info, dest in members:
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as dst:
                dst.write(src.read())
            restored.append(dest)
    return restored


def list_backups(backup_dir=BACKUP_DIR):
    if not os.path.isdir(backup_dir):
        return []
    return sorted(os.path.join(backup_dir, n) for n in os.listdir(backup_dir) if n.endswith(".zip"))


def open_path(path):
    """Open a file or folder with the Windows shell (Explorer / Notepad)."""
    if sys.platform == "win32":
        os.startfile(path)  # noqa: S606 - local path chosen by the player
    else:
        subprocess.Popen(["xdg-open", path])


# --------------------------------------------------------------------------
# UI strings (English, with a PT-BR toggle)
# --------------------------------------------------------------------------

STRINGS = {
    "en": {
        "title": "God of War: Ascension Recompiled - Launcher",
        "paths": "Paths", "exe": "Game exe (MyGameRecomp.exe)", "eboot": "EBOOT.elf (PS3_GAME/USRDIR)",
        "gamedata": "Save-data root (optional)", "hdd1": "Cache root (optional)", "browse": "Browse...",
        "options": "Options", "window": "Show window", "fullscreen": "Full screen (borderless)",
        "guest_programs": "Game shaders (guest programs)", "skip_intro": "Skip intro movie",
        "release_guard": "Release guard (stability mitigation)", "console": "Own console window (no log file)",
        "vk_device": "GPU index", "language": "Game language", "keep_logs": "Logs to keep",
        "extra_env": "Extra environment (KEY=VALUE per line, advanced)",
        "check": "Check setup", "play": "Play", "stop": "Stop", "open_log": "Open last log",
        "backup": "Back up saves", "restore": "Restore saves...", "open_backups": "Backups folder",
        "save": "Save settings", "state_idle": "Not running", "state_running": "Running (pid %d)",
        "state_exit": "Exited with code %s", "ui_lang": "PT-BR",
        "env_errors": "Fix the extra environment first:\n", "setup_errors": "The setup check found errors:\n\n%s\n\nPlay anyway?",
        "no_saves": "No save data found in:\n%s", "backed_up": "Saved to:\n%s",
        "restore_running": "Stop the game before restoring saves.",
        "restore_confirm": "Restore %s?\n\nCurrent saves are backed up first.", "restored": "Restored %d file(s).",
        "no_log": "No log yet.", "running": "The game is already running.",
    },
    "pt": {
        "title": "God of War: Ascension Recompiled - Inicializador",
        "paths": "Caminhos", "exe": "Executavel (MyGameRecomp.exe)", "eboot": "EBOOT.elf (PS3_GAME/USRDIR)",
        "gamedata": "Pasta de saves (opcional)", "hdd1": "Pasta de cache (opcional)", "browse": "Procurar...",
        "options": "Opcoes", "window": "Mostrar janela", "fullscreen": "Tela cheia (sem borda)",
        "guest_programs": "Shaders do jogo (guest programs)", "skip_intro": "Pular video de abertura",
        "release_guard": "Release guard (mitigacao de estabilidade)", "console": "Console proprio (sem arquivo de log)",
        "vk_device": "Indice da GPU", "language": "Idioma do jogo", "keep_logs": "Logs a manter",
        "extra_env": "Ambiente extra (CHAVE=VALOR por linha, avancado)",
        "check": "Verificar instalacao", "play": "Jogar", "stop": "Parar", "open_log": "Abrir ultimo log",
        "backup": "Backup dos saves", "restore": "Restaurar saves...", "open_backups": "Pasta de backups",
        "save": "Salvar configuracoes", "state_idle": "Parado", "state_running": "Rodando (pid %d)",
        "state_exit": "Encerrado com codigo %s", "ui_lang": "English",
        "env_errors": "Corrija o ambiente extra primeiro:\n", "setup_errors": "A verificacao encontrou erros:\n\n%s\n\nJogar mesmo assim?",
        "no_saves": "Nenhum save encontrado em:\n%s", "backed_up": "Salvo em:\n%s",
        "restore_running": "Feche o jogo antes de restaurar os saves.",
        "restore_confirm": "Restaurar %s?\n\nOs saves atuais recebem backup antes.", "restored": "%d arquivo(s) restaurado(s).",
        "no_log": "Nenhum log ainda.", "running": "O jogo ja esta rodando.",
    },
}


# --------------------------------------------------------------------------
# GUI
# --------------------------------------------------------------------------


class Tooltip:
    def __init__(self, widget, text):
        self.widget, self.text, self.tip = widget, text, None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _event=None):
        import tkinter as tk
        if self.tip:
            return
        x = self.widget.winfo_rootx() + 16
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry("+%d+%d" % (x, y))
        tk.Label(self.tip, text=self.text, justify="left", background="#ffffe0",
                 relief="solid", borderwidth=1, wraplength=420).pack(ipadx=4, ipady=2)

    def hide(self, _event=None):
        if self.tip:
            self.tip.destroy()
            self.tip = None


class LauncherApp:
    def __init__(self, root):
        import tkinter as tk
        self.tk = tk
        self.root = root
        self.settings = guess_paths(load_settings())
        self.proc = None
        self.log_file = None
        self.vars = {}
        self.build_ui()
        self.poll()

    def t(self, key):
        lang = self.settings.get("ui_lang", "en")
        return STRINGS.get(lang, STRINGS["en"]).get(key, STRINGS["en"][key])

    # ---- layout -----------------------------------------------------------
    def build_ui(self):
        tk = self.tk
        from tkinter import ttk
        for child in self.root.winfo_children():
            child.destroy()
        self.root.title(self.t("title"))
        frame = ttk.Frame(self.root, padding=8)
        frame.grid(sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)

        paths = ttk.LabelFrame(frame, text=self.t("paths"), padding=6)
        paths.grid(row=0, column=0, sticky="ew")
        paths.columnconfigure(1, weight=1)
        for row, (key, kind) in enumerate((("exe_path", "exe"), ("eboot_path", "eboot"),
                                           ("gamedata_root", "gamedata"), ("hdd1_root", "hdd1"))):
            var = tk.StringVar(value=self.settings.get(key, ""))
            self.vars[key] = var
            label = ttk.Label(paths, text=self.t(kind))
            label.grid(row=row, column=0, sticky="w")
            ttk.Entry(paths, textvariable=var, width=60).grid(row=row, column=1, sticky="ew", padx=4)
            ttk.Button(paths, text=self.t("browse"),
                       command=lambda k=key: self.browse(k)).grid(row=row, column=2)
            if key in KNOB_HELP:
                Tooltip(label, "%s\n%s" % KNOB_HELP[key])

        opts = ttk.LabelFrame(frame, text=self.t("options"), padding=6)
        opts.grid(row=1, column=0, sticky="ew", pady=6)
        checks = ("window", "fullscreen", "guest_programs", "skip_intro", "release_guard", "console")
        for i, key in enumerate(checks):
            var = tk.BooleanVar(value=bool(self.settings.get(key)))
            self.vars[key] = var
            cb = ttk.Checkbutton(opts, text=self.t(key), variable=var)
            cb.grid(row=i // 2, column=i % 2, sticky="w", padx=4)
            Tooltip(cb, "%s\n%s" % KNOB_HELP[key])
        row = (len(checks) + 1) // 2
        small = ttk.Frame(opts)
        small.grid(row=row, column=0, columnspan=2, sticky="w", pady=(4, 0))
        self.vars["vk_device"] = tk.StringVar(value=str(self.settings.get("vk_device", "")))
        self.vars["language"] = tk.StringVar(value=str(self.settings.get("language", "")))
        self.vars["keep_logs"] = tk.StringVar(value=str(self.settings.get("keep_logs", 10)))
        lab = ttk.Label(small, text=self.t("vk_device"))
        lab.pack(side="left")
        Tooltip(lab, "%s\n%s" % KNOB_HELP["vk_device"])
        ttk.Entry(small, textvariable=self.vars["vk_device"], width=4).pack(side="left", padx=(2, 12))
        lab = ttk.Label(small, text=self.t("language"))
        lab.pack(side="left")
        Tooltip(lab, "%s\n%s" % KNOB_HELP["language"])
        ttk.Combobox(small, textvariable=self.vars["language"], values=LANGUAGES,
                     width=7).pack(side="left", padx=(2, 12))
        ttk.Label(small, text=self.t("keep_logs")).pack(side="left")
        ttk.Entry(small, textvariable=self.vars["keep_logs"], width=4).pack(side="left", padx=2)

        lab = ttk.Label(opts, text=self.t("extra_env"))
        lab.grid(row=row + 1, column=0, columnspan=2, sticky="w", pady=(6, 0))
        Tooltip(lab, "%s\n%s" % KNOB_HELP["extra_env"])
        self.extra = tk.Text(opts, height=4, width=70)
        self.extra.insert("1.0", self.settings.get("extra_env", ""))
        self.extra.grid(row=row + 2, column=0, columnspan=2, sticky="ew")

        buttons = ttk.Frame(frame)
        buttons.grid(row=2, column=0, sticky="ew")
        self.play_btn = ttk.Button(buttons, text=self.t("play"), command=self.play)
        self.stop_btn = ttk.Button(buttons, text=self.t("stop"), command=self.stop)
        for b in (ttk.Button(buttons, text=self.t("check"), command=self.check),
                  self.play_btn, self.stop_btn,
                  ttk.Button(buttons, text=self.t("open_log"), command=self.open_last_log),
                  ttk.Button(buttons, text=self.t("save"), command=self.save),
                  ttk.Button(buttons, text=self.t("ui_lang"), command=self.toggle_lang)):
            b.pack(side="left", padx=2)
        saves = ttk.Frame(frame)
        saves.grid(row=3, column=0, sticky="ew", pady=(4, 0))
        for b in (ttk.Button(saves, text=self.t("backup"), command=self.backup),
                  ttk.Button(saves, text=self.t("restore"), command=self.restore),
                  ttk.Button(saves, text=self.t("open_backups"), command=self.open_backups)):
            b.pack(side="left", padx=2)

        self.state = tk.StringVar(value=self.t("state_idle"))
        ttk.Label(frame, textvariable=self.state).grid(row=4, column=0, sticky="w", pady=(6, 0))
        self.report = tk.Text(frame, height=10, width=90, state="disabled")
        self.report.grid(row=5, column=0, sticky="nsew")
        frame.rowconfigure(5, weight=1)
        self.update_state()

    # ---- helpers ----------------------------------------------------------
    def collect(self):
        for key, var in self.vars.items():
            self.settings[key] = var.get()
        self.settings["vk_device"] = self.settings["vk_device"].strip()
        try:
            self.settings["keep_logs"] = max(1, int(self.settings["keep_logs"]))
        except (TypeError, ValueError):
            self.settings["keep_logs"] = DEFAULT_SETTINGS["keep_logs"]
        self.settings["extra_env"] = self.extra.get("1.0", "end").rstrip("\n")
        return self.settings

    def show(self, lines):
        self.report.configure(state="normal")
        self.report.delete("1.0", "end")
        self.report.insert("1.0", "\n".join(lines))
        self.report.configure(state="disabled")

    def info(self, msg):
        from tkinter import messagebox
        messagebox.showinfo(self.t("title"), msg)

    def browse(self, key):
        from tkinter import filedialog
        current = self.vars[key].get()
        start = os.path.dirname(current) if current else REPO_ROOT
        if key == "exe_path":
            path = filedialog.askopenfilename(initialdir=start, filetypes=[("MyGameRecomp.exe", "*.exe")])
        elif key == "eboot_path":
            path = filedialog.askopenfilename(initialdir=start, filetypes=[("EBOOT.elf", "*.elf")])
        else:
            path = filedialog.askdirectory(initialdir=current or start)
        if path:
            self.vars[key].set(os.path.normpath(path))

    # ---- actions ----------------------------------------------------------
    def save(self):
        save_settings(self.collect())
        self.show(["Settings saved to %s" % SETTINGS_PATH])

    def toggle_lang(self):
        self.collect()
        self.settings["ui_lang"] = "pt" if self.settings.get("ui_lang") == "en" else "en"
        save_settings(self.settings)
        self.vars = {}
        self.build_ui()

    def check(self):
        results = check_setup(self.collect())
        self.show(["[%s] %s" % (lvl.upper(), msg) for lvl, msg in results])
        return results

    def play(self):
        from tkinter import messagebox
        if self.running():
            self.info(self.t("running"))
            return
        settings = self.collect()
        _extra, errors = parse_extra_env(settings["extra_env"])
        if errors:
            messagebox.showerror(self.t("title"), self.t("env_errors") + "\n".join(errors))
            return
        results = self.check()
        errs = [m for lvl, m in results if lvl == ERR]
        if errs and not messagebox.askyesno(self.t("title"), self.t("setup_errors") % "\n".join(errs)):
            return
        save_settings(settings)
        cmd, cwd = build_command(settings)
        env = build_env(settings)
        try:
            if settings.get("console"):
                self.proc = subprocess.Popen(cmd, cwd=cwd, env=env,
                                             creationflags=subprocess.CREATE_NEW_CONSOLE)
                self.show(["Started in its own console: %s" % " ".join(cmd)])
            else:
                log = new_log_path()
                self.log_file = open(log, "wb")
                self.proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                             stdout=self.log_file, stderr=subprocess.STDOUT,
                                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                rotate_logs(settings["keep_logs"])
                self.show(["Started: %s" % " ".join(cmd), "Log: %s" % log])
        except OSError as e:
            self.close_log()
            self.proc = None
            messagebox.showerror(self.t("title"), str(e))
        self.update_state()

    def stop(self):
        # Only the process this launcher started; never anything found by name.
        if self.running():
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.update_state()

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def close_log(self):
        if self.log_file:
            self.log_file.close()
            self.log_file = None

    def update_state(self):
        if self.running():
            self.state.set(self.t("state_running") % self.proc.pid)
            self.play_btn.state(["disabled"])
            self.stop_btn.state(["!disabled"])
        else:
            if self.proc is not None:
                self.state.set(self.t("state_exit") % self.proc.returncode)
                self.close_log()
            else:
                self.state.set(self.t("state_idle"))
            self.play_btn.state(["!disabled"])
            self.stop_btn.state(["disabled"])

    def poll(self):
        self.update_state()
        self.root.after(1000, self.poll)

    def open_last_log(self):
        logs = list_logs()
        if not logs:
            self.info(self.t("no_log"))
            return
        open_path(logs[-1])

    def backup(self):
        settings = self.collect()
        path = backup_saves(settings)
        if path:
            self.info(self.t("backed_up") % path)
        else:
            self.info(self.t("no_saves") % savedata_dir(settings))

    def restore(self):
        from tkinter import filedialog, messagebox
        if self.running():
            self.info(self.t("restore_running"))
            return
        os.makedirs(BACKUP_DIR, exist_ok=True)
        path = filedialog.askopenfilename(initialdir=BACKUP_DIR, filetypes=[("zip", "*.zip")])
        if not path or not messagebox.askyesno(self.t("title"), self.t("restore_confirm") % os.path.basename(path)):
            return
        settings = self.collect()
        try:
            backup_saves(settings)          # safety copy of what is there now
            files = restore_backup(path, settings)
        except (OSError, ValueError, zipfile.BadZipFile) as e:
            messagebox.showerror(self.t("title"), str(e))
            return
        self.info(self.t("restored") % len(files))

    def open_backups(self):
        os.makedirs(BACKUP_DIR, exist_ok=True)
        open_path(BACKUP_DIR)

    def on_close(self):
        try:
            save_settings(self.collect())
        except OSError:
            pass
        # The game keeps running if the launcher is closed; the child has its
        # own handle to the log file, so ours can be closed either way.
        self.close_log()
        self.root.destroy()


def main():
    import tkinter as tk
    root = tk.Tk()
    app = LauncherApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()


if __name__ == "__main__":
    main()
