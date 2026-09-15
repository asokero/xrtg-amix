"""Host-side session control tests; no X server or target writes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class SessionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="xrtg-session-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.lib = self.root / "lib"
        self.lib.mkdir()
        self.home = self.root / "home"
        self.home.mkdir()
        self.log = self.root / "calls"
        self.env = dict(os.environ, HOME=str(self.home), CALLS=str(self.log),
                        DISPLAY=":test", XAUTHORITY="/test/authority")
        self.env.pop("XRTG_SESSION", None)
        (self.lib / "Xdefaults").touch()
        (self.lib / "backdrop").touch()
        (self.lib / "session.conf").write_text(
            'XRTG_BACKDROP="' + str(self.lib / "backdrop") + '"\n')
        for name in ("twm", "start-amiwm", "olwm", "olwsm", "xterm", "xclock", "xrdb", "xv",
                     "xsetroot", "xmodmap", "sleep"):
            body = 'echo "' + name + ' $*" >> "$CALLS"\n'
            if name in ("twm", "start-amiwm", "olwm", "olwsm"):
                body += ('echo "env:$DISPLAY:$XAUTHORITY:$SHELL" >> "$CALLS"\n'
                         '/bin/sleep 0.3\nexit 7\n')
            if name == "sleep":
                body += '/bin/sleep 0.03\n'
            self.command(name, body)
        source = (Path(__file__).resolve().parents[1] /
                  "session/xrtg-session.sh").read_text()
        source = source.replace("/usr/X/lib/xrtg", str(self.lib))
        source = source.replace("/usr/X/bin", str(self.bin))
        source = source.replace("/usr/local/bin", str(self.bin))
        source = source.replace("/tmp/xdm-", str(self.root / "xdm-"))
        self.script = self.root / "session"
        self.script.write_text(source)

    def command(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/sh\n" + body)
        path.chmod(0o755)

    def run_session(self, *args):
        result = subprocess.run(["/bin/sh", str(self.script), *args],
                                env=self.env, capture_output=True, text=True,
                                timeout=5)
        calls = self.log.read_text() if self.log.exists() else ""
        return result, calls

    def test_default_waits_for_wm_and_loads_backdrop(self):
        result, calls = self.run_session()
        self.assertEqual(result.returncode, 7)
        self.assertIn("twm -f ", calls)
        self.assertIn("xv -root -quit ", calls)
        self.assertNotIn("start-amiwm", calls)

    def use_repository_config(self):
        config = (Path(__file__).resolve().parents[1] /
                  "session/session.conf").read_text()
        (self.lib / "session.conf").write_text(config)

    def test_default_keymap_uses_authenticated_home(self):
        self.use_repository_config()
        keymap = self.home / ".Xmodmap"
        keymap.touch()
        result, calls = self.run_session()
        self.assertEqual(result.returncode, 7)
        self.assertIn("xmodmap " + str(keymap) + "\n", calls)

    def test_keymap_local_override(self):
        self.use_repository_config()
        (self.home / ".Xmodmap").touch()
        keymap = self.root / "custom-keymap"
        keymap.touch()
        (self.lib / "session.conf.local").write_text(
            'XRTG_XMODMAP="' + str(keymap) + '"\n')
        _, calls = self.run_session()
        self.assertIn("xmodmap " + str(keymap) + "\n", calls)
        self.assertNotIn("xmodmap " + str(self.home / ".Xmodmap"), calls)

    def test_keymap_can_be_disabled(self):
        self.use_repository_config()
        (self.home / ".Xmodmap").touch()
        (self.lib / "session.conf.local").write_text("XRTG_XMODMAP=\n")
        _, calls = self.run_session()
        self.assertNotIn("xmodmap ", calls)

    def test_amiwm_preserves_environment_and_skips_backdrop(self):
        (self.lib / "session.conf.local").write_text(
            "SHELL=/usr/public/bin/bash\nexport SHELL\n")
        result, calls = self.run_session("amiwm")
        self.assertEqual(result.returncode, 7)
        self.assertIn("start-amiwm \n", calls)
        self.assertIn("env::test:/test/authority:/usr/public/bin/bash", calls)
        self.assertNotIn("twm -f", calls)
        self.assertNotIn("xv ", calls)
        self.assertIn("xrdb -load", calls)
        self.assertIn("xterm -geometry", calls)

    def test_openlook(self):
        result, calls = self.run_session("openlook")
        self.assertEqual(result.returncode, 7)
        self.assertIn("olwm \n", calls)
        self.assertNotIn("twm -f", calls)
        self.assertNotIn("start-amiwm", calls)
        self.assertNotIn("xv -root -quit", calls)
        self.assertIn("olwsm \n", calls)
        self.assertIn("env::test:/test/authority:", calls)

    def test_openlook_preference_and_override(self):
        (self.home / ".xrtg-session").write_text("openlook\n")
        _, calls = self.run_session()
        self.assertIn("olwm \n", calls)
        self.log.unlink()
        _, calls = self.run_session("default")
        self.assertIn("twm -f", calls)
        self.assertNotIn("olwm", calls)

    def test_openlook_missing(self):
        (self.bin / "olwm").unlink()
        _, calls = self.run_session("openlook")
        self.assertIn("window manager missing", calls)
        self.assertNotIn("twm -f", calls)

    def test_workspace_missing(self):
        (self.bin / "olwsm").unlink()
        _, calls = self.run_session("openlook")
        self.assertIn("workspace manager missing", calls)
        self.assertNotIn("olwm \n", calls)

    def test_workspace_start_failure(self):
        self.command("olwsm", "exit 1\n")
        _, calls = self.run_session("openlook")
        self.assertIn("olwsm exited during startup", calls)

    def test_workspace_exit_terminates_window_manager(self):
        self.command("olwm", 'trap \'echo wm-stopped >> "$CALLS"; exit 0\' 15\n'
                     'while :; do /bin/sleep 0.05; done\n')
        self.command("olwsm", "/bin/sleep 0.15\nexit 9\n")
        result, calls = self.run_session("openlook")
        self.assertEqual(result.returncode, 9)
        self.assertIn("wm-stopped", calls)

    def test_openlook_start_failure(self):
        self.command("olwm", "exit 1\n")
        _, calls = self.run_session("openlook")
        self.assertIn("openlook exited during startup", calls)
        self.assertNotIn("twm -f", calls)

    def test_user_preference(self):
        (self.home / ".xrtg-session").write_text("amiwm\n")
        result, calls = self.run_session()
        self.assertEqual(result.returncode, 7)
        self.assertIn("start-amiwm", calls)

    def test_explicit_default_overrides_user(self):
        (self.home / ".xrtg-session").write_text("amiwm\n")
        _, calls = self.run_session("default")
        self.assertIn("twm -f", calls)
        self.assertNotIn("start-amiwm", calls)

    def test_system_default(self):
        (self.lib / "session.conf.local").write_text("XRTG_SESSION=amiwm\n")
        _, calls = self.run_session()
        self.assertIn("start-amiwm", calls)

    def test_preference_is_not_shell_code(self):
        (self.home / ".xrtg-session").write_text('echo INJECTED >&2\n')
        result, calls = self.run_session()
        self.assertNotIn("INJECTED", result.stderr)
        self.assertIn("unknown session", calls)
        self.assertNotIn("twm -f", calls)

    def test_missing_amiwm_opens_failsafe(self):
        (self.bin / "start-amiwm").unlink()
        result, calls = self.run_session("amiwm")
        self.assertEqual(result.returncode, 0)
        self.assertIn("window manager missing", calls)
        self.assertNotIn("twm -f", calls)

    def test_failed_start_opens_failsafe(self):
        self.command("start-amiwm", "exit 1\n")
        _, calls = self.run_session("amiwm")
        self.assertIn("amiwm exited during startup", calls)
        self.assertNotIn("twm -f", calls)

    def test_failsafe_bypasses_bad_preference(self):
        (self.home / ".xrtg-session").write_text("invalid\n")
        _, calls = self.run_session("failsafe")
        self.assertIn("xterm -geometry 80x24-0-0 -ls", calls)
        self.assertNotIn("twm -f", calls)
        self.assertNotIn("unknown session", calls)


if __name__ == "__main__":
    unittest.main()
