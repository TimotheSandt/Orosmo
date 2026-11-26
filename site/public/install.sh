#!/bin/sh
# Orosmo installer for Linux and macOS (POSIX sh).
#
#   curl -fsSL https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh | sh            # asks first
#   curl -fsSL https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh | sh -s -- --yes # no questions
# (the same file is served by the site as /install.sh)
#
# What it does, in order:
#   1. system packages needed to build an Orosmo project (C++23 compiler, make, OpenGL/X11 headers, Vulkan loader):
#      lists the missing ones, then installs them with apt, dnf, pacman or brew after confirmation (sudo only then);
#   2. downloads the `orosmo` tool of the release, checks it against SHA256SUMS, installs it in $OROSMO_HOME/bin;
#   3. adds $OROSMO_HOME/bin to PATH (one line in your shell's startup file, added once);
#   4. runs `orosmo install latest` (or the --version given): the prebuilt engine goes to $OROSMO_HOME/engines/<version>.
#
# Options:
#   -y, --yes           do not ask (install the system packages, modify PATH)
#   --no-deps           skip the system packages (step 1)
#   --version X.Y.Z     install this release instead of the latest one
#   --no-modify-path    do not touch the shell startup files
#   --allow-root        allow running as root (refused by default)
#   -h, --help          this help
# Environment:
#   OROSMO_HOME           install root (default: ~/.orosmo)
#   OROSMO_DOWNLOAD_BASE  directory or https:// URL holding the release files (tests, mirrors); default: GitHub releases
#
# Licence: Apache-2.0 (https://github.com/TimotheSandt/Orosmo).

set -eu

REPO="TimotheSandt/Orosmo"

say() { printf '%s\n' "$*"; }
warn() { printf 'orosmo-install: warning: %s\n' "$*" >&2; }
die() {
    printf 'orosmo-install: error: %s\n' "$*" >&2
    exit 1
}

usage() {
    if [ -f "$0" ]; then
        sed -n '2,/^# Licence/p' "$0" | sed 's/^# \{0,1\}//'
    else
        say "Usage: install.sh [--yes] [--no-deps] [--version X.Y.Z] [--no-modify-path] [--allow-root]"
    fi
}

# ── Options ──────────────────────────────────────────────────────────────────────
YES=0
NO_DEPS=0
VERSION=""
MODIFY_PATH=1
ALLOW_ROOT=0
while [ $# -gt 0 ]; do
    case "$1" in
    -y | --yes) YES=1 ;;
    --no-deps) NO_DEPS=1 ;;
    --version)
        [ $# -ge 2 ] || die "--version expects a version (x.y.z)"
        VERSION=$2
        shift
        ;;
    --version=*) VERSION=${1#--version=} ;;
    --no-modify-path) MODIFY_PATH=0 ;;
    --allow-root) ALLOW_ROOT=1 ;;
    -h | --help)
        usage
        exit 0
        ;;
    *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

if [ -n "$VERSION" ]; then
    VERSION=${VERSION#v}
    case "$VERSION" in
    *[!0-9.]* | .* | *. | *..*) die "invalid version '$VERSION' (expected x.y.z)" ;;
    *.*.*) ;;
    *) die "invalid version '$VERSION' (expected x.y.z)" ;;
    esac
fi

if [ "$(id -u)" = 0 ] && [ "$ALLOW_ROOT" = 0 ]; then
    die "do not run this installer as root: it installs into your home directory and asks before using sudo (--allow-root to override)"
fi

# ── Platform ─────────────────────────────────────────────────────────────────────
case "$(uname -s)" in
Linux) OS=linux ;;
Darwin) OS=macos ;;
*) die "unsupported system '$(uname -s)': use install.ps1 on Windows" ;;
esac
case "$(uname -m)" in
x86_64 | amd64) ARCH=x64 ;;
aarch64 | arm64) ARCH=arm64 ;;
*) die "unsupported processor '$(uname -m)' (x86_64 and arm64 only)" ;;
esac
PLATFORM="$OS-$ARCH"

OROSMO_HOME=${OROSMO_HOME:-"$HOME/.orosmo"}
export OROSMO_HOME
BIN_DIR="$OROSMO_HOME/bin"

if [ -n "${OROSMO_DOWNLOAD_BASE:-}" ]; then
    BASE=${OROSMO_DOWNLOAD_BASE%/}
elif [ -n "$VERSION" ]; then
    BASE="https://github.com/$REPO/releases/download/v$VERSION"
else
    BASE="https://github.com/$REPO/releases/latest/download"
fi
# HTTPS only (with curl, redirects too): over plain HTTP the tool and SHA256SUMS would come through the same unauthenticated channel.
case "$BASE" in
[Hh][Tt][Tt][Pp]://*) die "refusing $BASE: OROSMO_DOWNLOAD_BASE must be an https:// URL or a local directory" ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }

# Answer to a yes/no question: --yes answers yes; without a terminal (curl | sh with no tty) the answer is no.
confirm() {
    [ "$YES" = 1 ] && return 0
    # Subshell: a failed redirection on the special built-in `:` would exit a non-interactive POSIX shell.
    if [ -r /dev/tty ] && (: </dev/tty) 2>/dev/null; then
        printf '%s [y/N] ' "$1" >/dev/tty
        read -r answer </dev/tty || answer=""
        case "$answer" in y | Y | yes | YES | o | O | oui) return 0 ;; esac
        return 1
    fi
    say "(no terminal to ask: run again with --yes to accept)"
    return 1
}

# ── 1. System packages ───────────────────────────────────────────────────────────
# What an Orosmo project needs from the system (docs: engine.mk links -lGL -lX11 -lpthread -ldl -lm; volk loads the Vulkan
# loader at run time; every other third-party library is prebuilt and static in the engine package, deps/).
PM=""
PKGS=""
detect_packages() {
    if [ "$OS" = macos ]; then
        if have brew; then PM=brew; fi
        PKGS="vulkan-loader molten-vk"
        return
    fi
    if have apt-get; then
        PM=apt
        PKGS="$(apt_compiler) make libgl-dev libx11-dev libvulkan1"
    elif have dnf; then
        PM=dnf
        PKGS="gcc-c++ make mesa-libGL-devel libX11-devel vulkan-loader"
    elif have pacman; then
        PM=pacman
        PKGS="gcc make libglvnd libx11 vulkan-icd-loader"
    fi
}

# Debian/Ubuntu: g++ if the distribution's default is GCC 13 or newer, else g++-13 when the archive has it.
apt_compiler() {
    major=$(apt-cache policy g++ 2>/dev/null | sed -n 's/^ *Candidate: *\([0-9]*:\)\{0,1\}\([0-9]*\).*/\2/p' | head -n 1)
    if [ -n "$major" ] && [ "$major" -lt 13 ] 2>/dev/null && apt-cache show g++-13 >/dev/null 2>&1; then
        echo g++-13
    else
        echo g++
    fi
}

is_installed() {
    case "$PM" in
    apt) dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed' ;;
    dnf) rpm -q "$1" >/dev/null 2>&1 ;;
    pacman) pacman -Q "$1" >/dev/null 2>&1 ;;
    brew) brew list --versions "$1" >/dev/null 2>&1 ;;
    *) return 1 ;;
    esac
}

SUDO=""
CAN_ROOT=1
need_sudo() {
    if [ "$(id -u)" = 0 ]; then
        SUDO=""
    elif have sudo; then
        SUDO="sudo"
    else
        SUDO="sudo"
        CAN_ROOT=0
    fi
}

install_packages() {
    missing=""
    for p in $PKGS; do
        is_installed "$p" || missing="$missing $p"
    done
    missing=${missing# }
    if [ -z "$missing" ]; then
        say "System packages: all present ($PKGS)."
        return
    fi
    case "$PM" in
    apt) cmd="apt-get install -y --no-install-recommends $missing" ;;
    dnf) cmd="dnf install -y $missing" ;;
    pacman) cmd="pacman -S --needed --noconfirm $missing" ;;
    brew) cmd="brew install $missing" ;;
    esac
    say "System packages missing: $missing"
    if [ "$PM" = brew ]; then
        say "Command: $cmd"
    else
        need_sudo
        if [ "$PM" = apt ]; then
            say "Command: ${SUDO:+$SUDO }apt-get update && ${SUDO:+$SUDO }$cmd"
        else
            say "Command: ${SUDO:+$SUDO }$cmd"
        fi
    fi
    if [ "$CAN_ROOT" = 0 ]; then
        warn "'sudo' is not available: run the command above as root, then rerun with --no-deps"
        return
    fi
    if ! confirm "Install them now?"; then
        warn "system packages not installed: run the command above yourself (or rerun with --yes)"
        return
    fi
    # $cmd and $SUDO are split into words on purpose (package names never contain spaces).
    # shellcheck disable=SC2086
    case "$PM" in
    apt) $SUDO apt-get update && $SUDO env DEBIAN_FRONTEND=noninteractive $cmd ;;
    brew) $cmd ;;
    *) $SUDO $cmd ;;
    esac
}

check_compiler() {
    if [ "$OS" = macos ]; then
        if ! xcode-select -p >/dev/null 2>&1; then
            warn "no C++ compiler: run 'xcode-select --install' (Xcode command line tools: Apple clang, make)"
        fi
        return
    fi
    for cxx in "${CXX:-}" g++ g++-15 g++-14 g++-13 clang++; do
        if [ -z "$cxx" ] || ! have "$cxx"; then continue; fi
        major=$("$cxx" -dumpversion 2>/dev/null | cut -d. -f1)
        case "$cxx" in
        clang++*) min=17 ;;
        *) min=13 ;;
        esac
        if [ -n "$major" ] && [ "$major" -ge "$min" ] 2>/dev/null; then
            if [ "$cxx" != g++ ] && [ "$cxx" != "${CXX:-}" ]; then
                say "C++ compiler: $cxx $major (the default g++ is older: put 'CXX := $cxx' in your project's config.local.mk)"
            else
                say "C++ compiler: $cxx $major"
            fi
            return
        fi
    done
    warn "no C++23 compiler found (GCC 13+ or Clang 17+): projects will not build until one is installed"
}

if [ "$NO_DEPS" = 1 ]; then
    say "System packages: skipped (--no-deps)."
else
    detect_packages
    if [ -z "$PM" ]; then
        if [ "$OS" = macos ]; then
            warn "Homebrew not found (https://brew.sh): install it, then: brew install $PKGS"
        else
            warn "no supported package manager (apt, dnf, pacman): install a C++23 compiler (GCC 13+), make, the OpenGL and X11 development files and the Vulkan loader yourself"
        fi
    else
        install_packages
    fi
    check_compiler
fi

# ── 2. The orosmo tool ───────────────────────────────────────────────────────────
TMP=$(mktemp -d 2>/dev/null || mktemp -d -t orosmo)
trap 'rm -rf "$TMP"' EXIT INT TERM

fetch() { # fetch <file name> <destination>
    case "$BASE" in
    https://*)
        if have curl; then
            curl -fsSL --proto '=https' --proto-redir '=https' --retry 3 -o "$2" "$BASE/$1"
        elif have wget; then
            # wget cannot restrict the protocol of redirects (its --https-only is for recursive mode): curl is preferred.
            wget -q -O "$2" "$BASE/$1"
        else
            die "curl or wget is needed to download $1"
        fi
        ;;
    *) cp "${BASE#file://}/$1" "$2" ;;
    esac
}

sha256() {
    if have sha256sum; then
        sha256sum "$1" | cut -d ' ' -f 1
    elif have shasum; then
        shasum -a 256 "$1" | cut -d ' ' -f 1
    else
        die "sha256sum or shasum is needed to check the download"
    fi
}

TOOL="orosmo-$PLATFORM"
say "Downloading $TOOL from $BASE ..."
fetch "$TOOL" "$TMP/orosmo" || die "download of $BASE/$TOOL failed"
fetch SHA256SUMS "$TMP/SHA256SUMS" || die "download of $BASE/SHA256SUMS failed: the tool cannot be checked"
expected=$(awk -v f="$TOOL" '$2 == f || $2 == "*" f { print $1; exit }' "$TMP/SHA256SUMS")
[ -n "$expected" ] || die "SHA256SUMS has no entry for $TOOL"
actual=$(sha256 "$TMP/orosmo")
[ "$expected" = "$actual" ] || die "checksum mismatch for $TOOL (expected $expected, got $actual): corrupted or tampered download"
say "SHA-256 verified."

mkdir -p "$BIN_DIR"
chmod 755 "$TMP/orosmo"
mv -f "$TMP/orosmo" "$BIN_DIR/orosmo"
say "Installed $BIN_DIR/orosmo"

# ── 3. PATH ──────────────────────────────────────────────────────────────────────
# The line is written with $HOME when OROSMO_HOME is the default, so a moved home directory keeps working.
if [ "$OROSMO_HOME" = "$HOME/.orosmo" ]; then
    # shellcheck disable=SC2016
    path_line='export PATH="$HOME/.orosmo/bin:$PATH"'
    home_line=""
else
    path_line="export PATH=\"$BIN_DIR:\$PATH\""
    home_line="export OROSMO_HOME=\"$OROSMO_HOME\""
fi

startup_file() {
    case "$(basename "${SHELL:-sh}")" in
    zsh) echo "${ZDOTDIR:-$HOME}/.zshrc" ;;
    bash)
        if [ "$OS" = macos ]; then echo "$HOME/.bash_profile"; else echo "$HOME/.bashrc"; fi
        ;;
    fish) echo "$HOME/.config/fish/conf.d/orosmo.fish" ;;
    *) echo "$HOME/.profile" ;;
    esac
}

add_line() { # add_line <file> <line> : once
    if [ -f "$1" ] && grep -qxF "$2" "$1"; then
        return 1
    fi
    mkdir -p "$(dirname "$1")"
    printf '%s\n' "$2" >>"$1"
}

case ":$PATH:" in
*":$BIN_DIR:"*) on_path=1 ;;
*) on_path=0 ;;
esac
if [ "$MODIFY_PATH" = 1 ]; then
    rc=$(startup_file)
    if [ "${rc##*.}" = fish ]; then
        p_line="fish_add_path -g \"$BIN_DIR\""
        h_line=${home_line:+"set -gx OROSMO_HOME \"$OROSMO_HOME\""}
    else
        p_line=$path_line
        h_line=$home_line
    fi
    changed=0
    if [ -n "$h_line" ] && add_line "$rc" "$h_line"; then changed=1; fi
    if add_line "$rc" "$p_line"; then changed=1; fi
    if [ "$changed" = 1 ]; then
        say "Added to $rc: $p_line"
    else
        say "PATH: $rc already sets it."
    fi
elif [ "$on_path" = 0 ]; then
    say "PATH not modified (--no-modify-path): add $BIN_DIR to PATH yourself."
fi

# ── 4. The engine ────────────────────────────────────────────────────────────────
say "Installing the engine ($([ -n "$VERSION" ] && echo "$VERSION" || echo latest)) ..."
"$BIN_DIR/orosmo" install "${VERSION:-latest}"

say ""
say "Orosmo is installed."
if [ "$on_path" = 0 ]; then
    say "Open a new terminal (or run: ${home_line:+$home_line; }export PATH=\"$BIN_DIR:\$PATH\"), then:"
else
    say "Next:"
fi
say "  orosmo init mygame && cd mygame && orosmo run"
