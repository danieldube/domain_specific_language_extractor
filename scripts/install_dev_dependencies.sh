#!/usr/bin/env bash
set -euo pipefail

SYSTEM_NAME="$(uname -s)"

install_with_apt() {
  local sudo_cmd=""
  if [[ "${EUID}" -ne 0 ]]; then
    sudo_cmd="sudo"
  fi

  ${sudo_cmd} apt-get update
  ${sudo_cmd} apt-get install -y \
    clang \
    clang-format \
    clang-tidy \
    cmake \
    ninja-build \
    libclang-18-dev \
    python3-pip \
    python3-venv
}

install_with_brew() {
  brew update
  brew install llvm cmake ninja python
}

case "${SYSTEM_NAME}" in
  Linux)
    if command -v apt-get >/dev/null 2>&1; then
      install_with_apt
    else
      echo "Unsupported Linux distribution: install clang/clang-tidy/clang-format, libclang-dev, cmake, ninja manually." >&2
      exit 1
    fi
    ;;
  Darwin)
    if command -v brew >/dev/null 2>&1; then
      install_with_brew
    else
      echo "Homebrew is required to install clang/llvm, cmake, and ninja." >&2
      exit 1
    fi
    ;;
  *)
    echo "Unsupported operating system ${SYSTEM_NAME}." >&2
    echo "Install clang/clang-format/clang-tidy, libclang development headers, cmake, ninja, and Python 3 manually." >&2
    exit 1
    ;;
esac

python3 -m pip install --upgrade pip
python3 -m pip install --upgrade cmake-format
