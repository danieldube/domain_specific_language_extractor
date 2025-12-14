#!/usr/bin/env python3
import platform
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def run(command):
    subprocess.run(command, check=True)


def install_dependencies():
    system = platform.system()
    scripts_dir = REPO_ROOT / "scripts"
    if system == "Windows":
        installer = scripts_dir / "install_dev_dependencies.ps1"
        shell = "pwsh" if shutil.which("pwsh") else "powershell"
        run([shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(installer)])
    else:
        installer = scripts_dir / "install_dev_dependencies.sh"
        run([str(installer)])


def main():
    install_dependencies()
    run(["cmake", "-Bbuild", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"])


if __name__ == "__main__":
    main()
