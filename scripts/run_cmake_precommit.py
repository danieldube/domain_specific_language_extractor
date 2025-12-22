#!/usr/bin/env python3
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def run(command, *, allow_failure: bool = False):
    result = subprocess.run(command, check=False)
    if result.returncode != 0 and not allow_failure:
        raise subprocess.CalledProcessError(result.returncode, command)
    return result


def install_dependencies():
    system = platform.system()
    scripts_dir = REPO_ROOT / "scripts"
    if system == "Windows":
        installer = scripts_dir / "install_dev_dependencies.ps1"
        shell = "pwsh" if shutil.which("pwsh") else "powershell"
        command = [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(installer)]
    else:
        installer = scripts_dir / "install_dev_dependencies.sh"
        command = [str(installer)]

    result = run(command, allow_failure=True)
    if result.returncode != 0:
        sys.stderr.write(
            "Skipping dependency installation for pre-commit: installer failed. "
            "Run scripts/install_dev_dependencies.* manually if toolchain is missing.\n"
        )


def main():
    install_dependencies()
    run(["cmake", "-Bbuild", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"])


if __name__ == "__main__":
    main()
