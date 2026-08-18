#!/usr/bin/env bash
#
# Installs drip into the user prefix. No root, and nothing outside $HOME.
#
# Run with --uninstall to take it all back out again.
#
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${HOME}/.local"
qmldir="${prefix}/lib/qt6/qml"
plasmoid_id="dev.drip.applet"

bold=$'\e[1m'; dim=$'\e[2m'; red=$'\e[31m'; green=$'\e[32m'; yellow=$'\e[33m'; reset=$'\e[0m'
say()  { printf '%s==>%s %s\n' "${bold}" "${reset}" "$*"; }
warn() { printf '%s !!%s %s\n' "${yellow}" "${reset}" "$*"; }
die()  { printf '%s !!%s %s\n' "${red}" "${reset}" "$*" >&2; exit 1; }
ok()   { printf '%s  ✓%s %s\n' "${green}" "${reset}" "$*"; }

# --------------------------------------------------------------------------
# Uninstall
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--uninstall" ]]; then
    say "Removing drip"
    systemctl --user disable --now drip.service 2>/dev/null || true
    rm -f "${HOME}/.config/systemd/user/drip.service"
    systemctl --user daemon-reload 2>/dev/null || true

    rm -f "${prefix}/bin/dripd"
    rm -rf "${qmldir}/dev/drip"
    rm -rf "${HOME}/.local/share/plasma/plasmoids/${plasmoid_id}"
    rm -f "${HOME}/.local/share/knotifications6/drip.notifyrc"
    rm -f "${HOME}/.local/share/icons/hicolor/scalable/apps/drip.svg"
    rm -f "${HOME}/.config/environment.d/drip.conf"

    ok "drip removed. Your settings in ~/.config/driprc and any received files were left alone."
    echo "   Remove those too with: rm ~/.config/driprc"
    systemctl --user restart plasma-plasmashell 2>/dev/null || true
    exit 0
fi

# --------------------------------------------------------------------------
# Preflight. Fail with something actionable rather than a compiler error.
# --------------------------------------------------------------------------
say "Checking prerequisites"

# Name the packages per distro, since "install Qt6" is not an instruction.
suggest_packages() {
    local id=""
    [[ -r /etc/os-release ]] && id="$(. /etc/os-release && echo "${ID_LIKE:-$ID}")"
    case "${id}" in
        *arch*)
            echo "sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules \\"
            echo "    qt6-base qt6-declarative qt6-tools \\"
            echo "    kconfig kcoreaddons ki18n knotifications kio kwindowsystem plasma-framework"
            ;;
        *debian*|*ubuntu*)
            echo "sudo apt install build-essential cmake ninja-build extra-cmake-modules \\"
            echo "    qt6-base-dev qt6-declarative-dev \\"
            echo "    libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev \\"
            echo "    libkf6notifications-dev libkf6kio-dev libkf6windowsystem-dev \\"
            echo "    plasma-workspace-dev"
            ;;
        *fedora*|*rhel*)
            echo "sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules \\"
            echo "    qt6-qtbase-devel qt6-qtdeclarative-devel \\"
            echo "    kf6-kconfig-devel kf6-kcoreaddons-devel kf6-ki18n-devel \\"
            echo "    kf6-knotifications-devel kf6-kio-devel kf6-kwindowsystem-devel \\"
            echo "    plasma-workspace-devel"
            ;;
        *suse*)
            echo "sudo zypper install -t pattern devel_C_C++ && sudo zypper install cmake ninja \\"
            echo "    extra-cmake-modules qt6-base-devel qt6-declarative-devel \\"
            echo "    kf6-kconfig-devel kf6-kcoreaddons-devel kf6-ki18n-devel \\"
            echo "    kf6-knotifications-devel kf6-kio-devel kf6-kwindowsystem-devel"
            ;;
        *)
            echo "Install: a C++20 compiler, cmake, ninja, extra-cmake-modules,"
            echo "Qt 6 (Base, Declarative) and KDE Frameworks 6"
            echo "(Config, CoreAddons, I18n, Notifications, KIO, WindowSystem)."
            ;;
    esac
}

missing=()
for tool in cmake ninja; do
    command -v "${tool}" >/dev/null 2>&1 || missing+=("${tool}")
done
command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || missing+=("a C++ compiler")

if (( ${#missing[@]} )); then
    warn "Missing build tools: ${missing[*]}"
    echo
    suggest_packages
    echo
    die "Install those, then run this again."
fi
ok "build tools"

if ! command -v tailscale >/dev/null 2>&1; then
    die "tailscale is not installed. See https://tailscale.com/download"
fi
if ! tailscale status >/dev/null 2>&1; then
    warn "tailscale is installed but not connected. Run: sudo tailscale up"
    warn "Continuing -- drip will pick it up once it is."
else
    ok "tailscale is up"
fi

# The one piece of setup that is genuinely required, and the one people miss.
# Without it the LocalAPI socket refuses us and everything else looks broken.
if ! tailscale debug prefs 2>/dev/null | grep -q "\"OperatorUser\": *\"${USER}\""; then
    warn "You are not the Tailscale operator, so drip cannot talk to tailscaled"
    warn "without sudo. This is a one-time change:"
    echo
    echo "    sudo tailscale set --operator=${USER}"
    echo
    read -r -p "Run that now? [Y/n] " reply
    if [[ ! "${reply}" =~ ^[Nn] ]]; then
        sudo tailscale set --operator="${USER}" || die "Could not set the operator."
        ok "operator set to ${USER}"
    else
        warn "Skipped. drip will not see your devices until you do this."
    fi
else
    ok "you are the tailscale operator"
fi

# --------------------------------------------------------------------------
# Build and install
# --------------------------------------------------------------------------
say "Building"
if ! cmake -B "${repo_root}/build" -S "${repo_root}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${prefix}" \
    -DKDE_INSTALL_QMLDIR="${qmldir}" >/tmp/drip-cmake.log 2>&1; then
    cat /tmp/drip-cmake.log
    echo
    warn "Configuration failed -- usually a missing development package."
    echo
    suggest_packages
    exit 1
fi
cmake --build "${repo_root}/build"

say "Installing"
cmake --install "${repo_root}/build" >/dev/null

# metadata.json names "drip" as a theme icon, which is how every other plasmoid
# does it -- so the mark has to be findable by name, not just inside the package.
say "Installing the icon"
mkdir -p "${HOME}/.local/share/icons/hicolor/scalable/apps"
install -m644 "${repo_root}/applet/contents/icons/drip.svg" \
    "${HOME}/.local/share/icons/hicolor/scalable/apps/drip.svg"
gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true

# Qt does not search the user prefix for QML modules by default.
say "Registering the QML import path"
mkdir -p "${HOME}/.config/environment.d"
cat > "${HOME}/.config/environment.d/drip.conf" <<EOF
QML_IMPORT_PATH=\${HOME}/.local/lib/qt6/qml:\${QML_IMPORT_PATH}
EOF
systemctl --user set-environment "QML_IMPORT_PATH=${qmldir}"

say "Starting the engine"
mkdir -p "${HOME}/.config/systemd/user"
install -m644 "${repo_root}/packaging/drip.service" "${HOME}/.config/systemd/user/drip.service"
systemctl --user daemon-reload
systemctl --user enable --now drip.service

# --------------------------------------------------------------------------
# Verify, so the script says whether it worked rather than just "done"
# --------------------------------------------------------------------------
say "Checking it works"
sleep 1

if ! systemctl --user is-active --quiet drip.service; then
    journalctl --user -u drip.service -n 20 --no-pager
    die "The engine did not stay running (log above)."
fi
ok "engine running"

if probe_output="$("${prefix}/bin/dripd" --probe 2>&1)"; then
    device_count="$(sed -n 's/^devices (\([0-9]*\)).*/\1/p' <<<"${probe_output}")"
    ok "talking to tailscaled — ${device_count:-0} device(s) visible"
else
    warn "dripd could not reach tailscaled:"
    sed 's/^/    /' <<<"${probe_output}"
fi

say "Restarting Plasma so it picks up the widget"
systemctl --user restart plasma-plasmashell

cat <<EOF

${green}${bold}drip is installed.${reset}

  ${bold}Add it to your tray${reset}
    Right-click the system tray → Configure System Tray → Entries
    → set ${bold}drip${reset} to "Shown".

  ${bold}Use it${reset}
    Drag a file onto the tray icon, hold, and the panel opens.
    Drop it on a device to send. Or click a device to browse for files.

  ${dim}Check the engine at any time:  dripd --probe${reset}
  ${dim}Uninstall:                     ./packaging/install.sh --uninstall${reset}

EOF
