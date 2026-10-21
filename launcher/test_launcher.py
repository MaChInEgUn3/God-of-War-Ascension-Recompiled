"""Tests for the launcher's pure functions. No window, no game, temp dirs only.

    python -m unittest launcher/test_launcher.py -v
"""

import datetime
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gow_launcher as gl  # noqa: E402


def touch(path, data=b"x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


class BuildEnvTests(unittest.TestCase):
    def settings(self, **kw):
        s = dict(gl.DEFAULT_SETTINGS)
        s["exe_path"] = os.path.join("C:\\", "build", "MyGameRecomp.exe")
        s.update(kw)
        return s

    def test_defaults(self):
        env = gl.build_env(self.settings(), base_env={})
        self.assertEqual(env["PS3RECOMP_VK_WINDOW"], "1")
        self.assertEqual(env["PS3RECOMP_VK_GUEST_PROGRAMS"], "1")
        self.assertNotIn("PS3RECOMP_VK_FULLSCREEN", env)
        self.assertNotIn("PAD_SCRIPT", env)
        self.assertNotIn("GOW_RELEASE_GUARD", env)      # guard on = variable unset
        self.assertNotIn("PS3_LANG", env)
        self.assertNotIn("PS3_HDD1_ROOT", env)
        self.assertEqual(env["PPU_TRACE_MAX"], "0")
        self.assertEqual(env["PS3_GAMEDATA_ROOT"],
                         os.path.join(os.path.abspath(os.path.join("C:\\", "build")), "gamedata"))

    def test_options(self):
        env = gl.build_env(self.settings(skip_intro=True, release_guard=False, fullscreen=True,
                                         vk_device="1", language="pt-br",
                                         gamedata_root="D:\\saves", hdd1_root="D:\\cache"),
                           base_env={})
        self.assertEqual(env["PAD_SCRIPT"], "40:0x4000")
        self.assertEqual(env["GOW_RELEASE_GUARD"], "0")
        self.assertEqual(env["PS3RECOMP_VK_FULLSCREEN"], "1")
        self.assertEqual(env["PS3RECOMP_VK_DEVICE"], "1")
        self.assertEqual(env["PS3_LANG"], "pt-br")
        self.assertEqual(env["PS3_GAMEDATA_ROOT"], os.path.abspath("D:\\saves"))
        self.assertEqual(env["PS3_HDD1_ROOT"], os.path.abspath("D:\\cache"))

    def test_window_off_drops_fullscreen_and_inherited_flags(self):
        base = {"PS3RECOMP_VK_WINDOW": "1", "GOW_RELEASE_GUARD": "0", "PATH": "p"}
        env = gl.build_env(self.settings(window=False, fullscreen=True), base_env=base)
        self.assertNotIn("PS3RECOMP_VK_WINDOW", env)
        self.assertNotIn("PS3RECOMP_VK_FULLSCREEN", env)
        self.assertNotIn("GOW_RELEASE_GUARD", env)
        self.assertEqual(env["PATH"], "p")
        self.assertEqual(base["PS3RECOMP_VK_WINDOW"], "1")   # input not mutated

    def test_extra_env_wins(self):
        extra = "# comment\nPAD_SCRIPT=10:0x0008\n\nGOW_HEARTBEAT = 5\n"
        env = gl.build_env(self.settings(skip_intro=True, extra_env=extra), base_env={})
        self.assertEqual(env["PAD_SCRIPT"], "10:0x0008")
        self.assertEqual(env["GOW_HEARTBEAT"], "5")

    def test_parse_extra_env_errors(self):
        env, errors = gl.parse_extra_env("A=1\nnot a pair\nBAD KEY=2\n=3\nB==x")
        self.assertEqual(env, {"A": "1", "B": "=x"})
        self.assertEqual(len(errors), 3)


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def make_game(self, missing=()):
        usr = os.path.join(self.tmp, "vfs", "PS3_GAME", "USRDIR")
        touch(os.path.join(usr, "EBOOT.elf"))
        with open(os.path.join(usr, "ARCHIVES"), "w", newline="\r\n") as f:
            f.write("2\nA.PSARC\nB.PSARC\n")
        with open(os.path.join(usr, "LOCALIZED_ARCHIVES"), "w") as f:
            f.write("2\nENGLISH.PSARC\nFRENCH.PSARC\n")
        for n in ("A.PSARC", "B.PSARC", "ENGLISH.PSARC"):
            if n not in missing:
                touch(os.path.join(usr, n))
        build = os.path.join(self.tmp, "build")
        touch(os.path.join(build, "MyGameRecomp.exe"))
        touch(os.path.join(build, "SDL2.dll"))
        s = dict(gl.DEFAULT_SETTINGS)
        s["exe_path"] = os.path.join(build, "MyGameRecomp.exe")
        s["eboot_path"] = os.path.join(usr, "EBOOT.elf")
        return s

    def levels(self, results):
        return [lvl for lvl, _ in results]

    def test_good_setup_has_no_errors(self):
        results = gl.check_setup(self.make_game())
        self.assertNotIn(gl.ERR, self.levels(results), results)

    def test_missing_archive_and_paths(self):
        results = gl.check_setup(self.make_game(missing=("B.PSARC",)))
        errs = [m for lvl, m in results if lvl == gl.ERR]
        self.assertTrue(any("B.PSARC" in m for m in errs), errs)
        results = gl.check_setup(dict(gl.DEFAULT_SETTINGS))
        self.assertEqual(self.levels(results).count(gl.ERR), 2)

    def test_missing_sdl(self):
        s = self.make_game()
        os.remove(os.path.join(os.path.dirname(s["exe_path"]), "SDL2.dll"))
        errs = [m for lvl, m in gl.check_setup(s) if lvl == gl.ERR]
        self.assertTrue(any("DLL" in m for m in errs), errs)

    def test_logs_rotate(self):
        logs = os.path.join(self.tmp, "logs")
        t = datetime.datetime(2026, 1, 1, 12, 0, 0)
        for i in range(5):
            touch(gl.new_log_path(logs, t + datetime.timedelta(seconds=i)))
        removed = gl.rotate_logs(3, logs)
        self.assertEqual(len(removed), 2)
        left = gl.list_logs(logs)
        self.assertEqual(len(left), 3)
        self.assertTrue(left[-1].endswith("gow_20260101_120004.log"))

    def test_backup_restore_roundtrip(self):
        s = dict(gl.DEFAULT_SETTINGS)
        s["gamedata_root"] = os.path.join(self.tmp, "gamedata")
        saves = gl.savedata_dir(s)
        touch(os.path.join(saves, "BCUS98232-SYSDATA", "PARAM.SFO"), b"one")
        touch(os.path.join(saves, "OTHER00000", "f"), b"no")
        backups = os.path.join(self.tmp, "backups")
        z = gl.backup_saves(s, backups)
        with zipfile.ZipFile(z) as zf:
            self.assertEqual(zf.namelist(), ["BCUS98232-SYSDATA/PARAM.SFO"])
        touch(os.path.join(saves, "BCUS98232-SYSDATA", "PARAM.SFO"), b"two")
        restored = gl.restore_backup(z, s)
        self.assertEqual(len(restored), 1)
        with open(restored[0], "rb") as f:
            self.assertEqual(f.read(), b"one")

    def test_restore_ignores_unsafe_entries(self):
        s = dict(gl.DEFAULT_SETTINGS)
        s["gamedata_root"] = os.path.join(self.tmp, "gamedata")
        z = os.path.join(self.tmp, "evil.zip")
        with zipfile.ZipFile(z, "w") as zf:
            zf.writestr("BCUS98232-X/../../escape.txt", "bad")
            zf.writestr("BCUS98232-X/ok.bin", "good")
            zf.writestr("other.txt", "bad")
        restored = gl.restore_backup(z, s)
        self.assertEqual([os.path.basename(p) for p in restored], ["ok.bin"])
        found = [f for _d, _s, files in os.walk(self.tmp) for f in files if f == "escape.txt"]
        self.assertEqual(found, [])

    def test_settings_roundtrip(self):
        path = os.path.join(self.tmp, "settings.json")
        self.assertEqual(gl.load_settings(path), gl.DEFAULT_SETTINGS)
        s = gl.load_settings(path)
        s["skip_intro"] = True
        gl.save_settings(s, path)
        self.assertTrue(gl.load_settings(path)["skip_intro"])


if __name__ == "__main__":
    unittest.main()
