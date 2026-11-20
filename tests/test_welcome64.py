"""Build the same renderer with different optimizations and exercise guards."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class WelcomeRendererTests(unittest.TestCase):
    def test_host_rendering(self):
        for optimization in ("-O0", "-O2"):
            with self.subTest(optimization=optimization), tempfile.TemporaryDirectory() as d:
                binary = pathlib.Path(d)/"welcome-host"
                subprocess.run(["cc","-std=c11",optimization,"-Wall","-Wextra","-Werror",
                                "-Ikernel/include", "tests/welcome64_host.c",
                                "kernel/src64/welcome64.c","-o",str(binary)],
                               cwd=ROOT, check=True)
                subprocess.run([str(binary)],check=True)

if __name__ == "__main__":
    unittest.main()
