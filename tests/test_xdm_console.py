"""Offline tests: fake SAF/process table, never touch the real display."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ConsoleTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="xdm-console-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.log = self.root / "calls"
        self.env = dict(os.environ, CALLS=str(self.log))
        self.command("whoami", "echo root")
        self.command("sacadm", 'echo "sacadm $*" >> "$CALLS"')
        self.command("ps", "echo ' PID TTY TIME COMD'")
        self.command("sleep", ":")
        self.restore = self.command("restore", 'echo restore >> "$CALLS"')
        text = (Path(__file__).resolve().parents[1] / "xdm-console.sh").read_text()
        text = text.replace("PATH=/usr/X/bin:", "PATH=" + str(self.root) + ":")
        text = text.replace("/usr/sbin/sacadm", str(self.root / "sacadm"))
        text = text.replace("/usr/bin/ps", str(self.root / "ps"))
        self.script = self.root / "console"
        self.script.write_text(text)

    def command(self, name, body):
        path = self.root / name
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)
        return str(path)

    def run_helper(self, *args):
        result = subprocess.run(["/bin/sh", str(self.script), *args],
                                env=self.env, capture_output=True, text=True,
                                timeout=5)
        log = self.log.read_text() if self.log.exists() else ""
        return result, log

    def test_normal_stop_without_extra_restore(self):
        result, log = self.run_helper()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(log, "sacadm -k -p xdm\n")

    def test_restore_after_stop(self):
        result, log = self.run_helper(self.restore)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(log, "sacadm -k -p xdm\nrestore\n")

    def test_timeout_never_restores(self):
        self.command("ps", "echo '123 ? 0:00 Xrtg'")
        result, log = self.run_helper(self.restore)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("30 seconds", result.stderr)
        self.assertNotIn("restore", log)

    def test_xdm_also_blocks_restore(self):
        self.command("ps", "echo '123 ? 0:00 xdm'")
        result, log = self.run_helper(self.restore)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("restore", log)

    def test_ps_failure_never_restores(self):
        self.command("ps", "exit 1")
        result, log = self.run_helper(self.restore)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("restore", log)

    def test_stop_failure_never_restores(self):
        self.command("sacadm", "exit 1")
        result, log = self.run_helper(self.restore)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("restore", log)

    def test_restore_failure_reported(self):
        self.command("restore", "exit 1")
        result, _ = self.run_helper(self.restore)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("restore failed", result.stderr)

    def test_nonroot_rejected(self):
        self.command("whoami", "echo nobody")
        result, log = self.run_helper()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(log, "")

    def test_bad_path_rejected_before_stop(self):
        for path in ("relative", "/nonexistent-xrtg-restore"):
            result, log = self.run_helper(path)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(log, "")


if __name__ == "__main__":
    unittest.main()
