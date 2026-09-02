#!/usr/bin/env bash
set -euo pipefail

RULES_DST="/etc/udev/rules.d/99-phybot-imu.rules"
LEGACY_RULES_DST="/etc/udev/rules.d/99-phybot-imu-joy.rules"

if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required to remove the IMU udev configuration." >&2
        exit 1
    fi
    exec sudo bash "$0" "$@"
fi

rm -f "${RULES_DST}" "${LEGACY_RULES_DST}"

udevadm control --reload-rules
udevadm trigger

# Only remove links created by udev; never remove a real device node.
for link in /dev/ttyimu /dev/ttyjoy; do
    if [[ -L "${link}" ]]; then
        rm -f "${link}"
    fi
done

echo "IMU USB configuration removed."
echo "Removed rules:"
echo "  ${RULES_DST}"
echo "  ${LEGACY_RULES_DST} (legacy, if present)"

