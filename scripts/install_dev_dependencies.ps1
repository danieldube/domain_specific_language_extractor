$ErrorActionPreference = "Stop"

function Install-With-Choco {
    choco upgrade -y llvm cmake ninja python
}

function Install-With-Winget {
    winget install --id=LLVM.LLVM -e --source=winget --silent
    winget install --id=Kitware.CMake -e --source=winget --silent
    winget install --id=Ninja-build.Ninja -e --source=winget --silent
    winget install --id=Python.Python.3.12 -e --source=winget --silent
}

if (Get-Command choco -ErrorAction SilentlyContinue) {
    Install-With-Choco
} elseif (Get-Command winget -ErrorAction SilentlyContinue) {
    Install-With-Winget
} else {
    Write-Error "Install LLVM/clang, clang-format, clang-tidy, CMake, and Ninja manually. Chocolatey or winget is required for automated installation."
}

python -m pip install --upgrade pip
python -m pip install --upgrade cmake-format
