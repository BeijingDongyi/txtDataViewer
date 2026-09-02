#!/usr/bin/env bash
set -euo pipefail

RULES_NAME="99-phybot-imu.rules"
RULES_DST="/etc/udev/rules.d/${RULES_NAME}"
LEGACY_RULES_DST="/etc/udev/rules.d/99-phybot-imu-joy.rules"
TTYUSB_MIN=0
TTYUSB_MAX=5
PROBE_BAUD_RATES=(921600 115200 460800 230400 9600)
JOY_BAUD_RATE=100000
IMU_GLOBS=(
    "/dev/ttyUSB"{0..5}
    "/dev/ttyACM"{0..5}
    "/dev/ttyS"{0..9}
    "/dev/ttyAMA"{0..9}
    "/dev/ttyTHS"{0..9}
)
JOY_GLOBS=(
    "/dev/ttyUSB"{0..5}
)

usage() {
    cat <<EOF
Usage:
  sudo $0 [install|capture|list|probe|joy-probe|diagnose|status|remove]

Commands:
  install  Same as capture. Default command.
  capture  Choose IMU from USB/UART ports, optionally choose USB aircraft joystick, then install rules.
  list     Show IMU candidates and aircraft joystick USB candidates.
  probe    Actively probe IMU candidates by sending LOG VERSION at common baud rates.
  joy-probe Actively probe ttyUSB0-ttyUSB5 for aircraft joystick SBUS data.
  diagnose Same as list, with installed rule/link status.
  status   Show installed rules and current device links.
  remove   Remove installed udev rules created by this script.
EOF
}

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        echo "Please run as root: sudo $0 ${COMMAND}" >&2
        exit 1
    fi
}

get_property() {
    local dev="$1"
    local key="$2"

    udevadm info --query=property --name="${dev}" 2>/dev/null \
        | sed -n "s/^${key}=//p" \
        | head -n 1
}

collect_existing_devices() {
    local -n out="$1"
    shift
    local dev

    out=()
    for dev in "$@"; do
        [[ -e "${dev}" ]] || continue
        out+=("${dev}")
    done
}

print_device_list() {
    local -n devs_ref="$1"
    local empty_message="$2"
    local dev vendor product serial path model driver
    local index=1

    for dev in "${devs_ref[@]}"; do
        vendor="$(get_property "${dev}" ID_VENDOR_ID)"
        product="$(get_property "${dev}" ID_MODEL_ID)"
        serial="$(get_property "${dev}" ID_SERIAL_SHORT)"
        path="$(get_property "${dev}" ID_PATH)"
        model="$(get_property "${dev}" ID_MODEL)"
        driver="$(get_property "${dev}" ID_USB_DRIVER)"
        printf '%d) %s | model=%s vendor=%s product=%s serial=%s path=%s driver=%s\n' \
            "${index}" "${dev}" "${model:-unknown}" "${vendor:-unknown}" "${product:-unknown}" \
            "${serial:-none}" "${path:-unknown}" "${driver:-unknown}"
        index=$((index + 1))
    done

    if (( index == 1 )); then
        echo "${empty_message}"
    fi
}

list_devices() {
    local imu_devs=()
    local joy_devs=()

    collect_existing_devices imu_devs "${IMU_GLOBS[@]}"
    collect_existing_devices joy_devs "${JOY_GLOBS[@]}"

    echo "IMU candidates (USB + UART):"
    print_device_list imu_devs "No IMU candidate serial ports found."
    echo
    echo "Aircraft joystick candidates (USB only):"
    print_device_list joy_devs "No /dev/ttyUSB0-/dev/ttyUSB5 devices found."
}

run_imu_probe() {
    local output_mode="$1"
    local imu_devs=()

    collect_existing_devices imu_devs "${IMU_GLOBS[@]}"

    if (( ${#imu_devs[@]} == 0 )); then
        [[ "${output_mode}" == "human" ]] && echo "No IMU candidate serial ports found."
        return 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        [[ "${output_mode}" == "human" ]] && echo "python3 is required for active IMU probing." >&2
        return 1
    fi

    if [[ "${output_mode}" == "human" ]]; then
        echo "Active IMU probe:"
        echo "  Ports: ${imu_devs[*]}"
        echo "  Baud rates: ${PROBE_BAUD_RATES[*]}"
        echo "  Command: LOG VERSION"
        echo
    fi

    python3 - "${output_mode}" "${PROBE_BAUD_RATES[*]}" "${imu_devs[@]}" <<'PY'
import os
import select
import sys
import termios
import time
import tty

output_mode = sys.argv[1]
baud_rates = [int(x) for x in sys.argv[2].split()]
ports = sys.argv[3:]

def baud_const(rate):
    return getattr(termios, f"B{rate}", None)

def configure(fd, baud):
    tty.setraw(fd, termios.TCSANOW)
    attrs = termios.tcgetattr(fd)
    attrs[4] = baud
    attrs[5] = baud
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)

def probe_once(port, rate):
    baud = baud_const(rate)
    if baud is None:
        return None, "unsupported baud"

    fd = None
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        configure(fd, baud)
        os.write(fd, b"LOG DISABLE\r\n")
        time.sleep(0.03)
        os.write(fd, b"LOG VERSION\r\n")

        deadline = time.monotonic() + 0.45
        data = b""
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], 0.05)
            if not readable:
                continue
            try:
                chunk = os.read(fd, 512)
            except BlockingIOError:
                continue
            if not chunk:
                continue
            data += chunk
            text = data.decode(errors="ignore")
            if "OK" in text or "VERSION" in text or "HiPNUC" in text or "HI" in text:
                try:
                    os.write(fd, b"LOG ENABLE\r\n")
                except OSError:
                    pass
                return text.strip().replace("\r", " ").replace("\n", " "), None
            if b"\x5a\xa5" in data:
                return f"binary HiPNUC frame marker 0x5A 0xA5, bytes={len(data)}", None
        return "", None
    except (OSError, termios.error) as exc:
        return None, str(exc)
    finally:
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass

found = False
for port in ports:
    matched = False
    for rate in baud_rates:
        response, error = probe_once(port, rate)
        if error:
            if output_mode == "human":
                print(f"{port:<16} baud={rate:<7} error: {error}")
            break
        if response:
            if output_mode == "human":
                safe_response = response.replace("\x00", "")[:120]
                print(f"{port:<16} baud={rate:<7} FOUND  response={safe_response}")
            else:
                print(f"FOUND\t{port}\t{rate}")
            found = True
            matched = True
            break
        if output_mode == "human":
            print(f"{port:<16} baud={rate:<7} no response")
    if matched:
        continue

if not found and output_mode == "human":
    print()
    print("No IMU response found. Check wiring, power, TX/RX direction, and baud rate.")
PY
}

probe_imu_devices() {
    run_imu_probe human
}

run_joy_probe() {
    local output_mode="$1"
    local joy_devs=()
    local imu_probe_results
    local imu_ports

    collect_existing_devices joy_devs "${JOY_GLOBS[@]}"

    if (( ${#joy_devs[@]} == 0 )); then
        [[ "${output_mode}" == "human" ]] && echo "No /dev/ttyUSB0-/dev/ttyUSB5 devices found."
        return 1
    fi

    imu_probe_results="$(run_imu_probe raw || true)"
    imu_ports="$(printf '%s\n' "${imu_probe_results}" | awk -F '	' '$1 == "FOUND" { print $2 }')"
    if [[ -n "${imu_ports}" ]]; then
        local filtered_joy_devs=()
        local dev
        local is_imu
        for dev in "${joy_devs[@]}"; do
            is_imu=0
            while IFS= read -r imu_port; do
                [[ -z "${imu_port}" ]] && continue
                if [[ "${dev}" == "${imu_port}" ]]; then
                    is_imu=1
                    break
                fi
            done <<< "${imu_ports}"
            if (( is_imu == 0 )); then
                filtered_joy_devs+=("${dev}")
            elif [[ "${output_mode}" == "human" ]]; then
                echo "Skipping ${dev}: detected as HiPNUC IMU."
            fi
        done
        joy_devs=("${filtered_joy_devs[@]}")
    fi

    if (( ${#joy_devs[@]} == 0 )); then
        [[ "${output_mode}" == "human" ]] && echo "No aircraft joystick candidates left after excluding IMU ports."
        return 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        [[ "${output_mode}" == "human" ]] && echo "python3 is required for aircraft joystick probing." >&2
        return 1
    fi

    if [[ "${output_mode}" == "human" ]]; then
        echo "Active aircraft joystick probe:"
        echo "  Ports: ${joy_devs[*]}"
        echo "  Serial: ${JOY_BAUD_RATE} baud, 8 data bits, even parity, 2 stop bits"
        echo "  Looking for valid 25-byte SBUS frames like SpeedRun joy_test."
        echo
    fi

    python3 - "${output_mode}" "${JOY_BAUD_RATE}" "${joy_devs[@]}" <<'PY'
import fcntl
import os
import select
import struct
import sys
import termios
import time

output_mode = sys.argv[1]
baud_rate = int(sys.argv[2])
ports = sys.argv[3:]

TCGETS2 = 0x802C542A
TCSETS2 = 0x402C542B
BOTHER = 0x1000
CBAUD = 0x100F
NCCS = 19
TERMIOS2_FMT = "IIIIB19BII"

def configure_sbus(fd):
    raw = fcntl.ioctl(fd, TCGETS2, bytes(struct.calcsize(TERMIOS2_FMT)))
    values = list(struct.unpack(TERMIOS2_FMT, raw))
    iflag, oflag, cflag, lflag = values[0], values[1], values[2], values[3]
    c_cc = values[5:5 + NCCS]

    cflag &= ~CBAUD
    cflag |= BOTHER
    cflag &= ~termios.CSIZE
    cflag |= termios.CS8
    cflag &= ~termios.PARODD
    cflag |= termios.PARENB
    cflag |= termios.CSTOPB
    if hasattr(termios, "CRTSCTS"):
        cflag &= ~termios.CRTSCTS
    cflag |= termios.CREAD | termios.CLOCAL

    iflag |= termios.INPCK
    iflag &= ~(termios.IGNBRK | termios.IGNCR | termios.ISTRIP | termios.IXON | termios.IXOFF | termios.IXANY)
    oflag &= ~termios.OPOST
    lflag &= ~(termios.ICANON | termios.ECHO | termios.ECHOE | termios.ISIG)

    c_cc[termios.VMIN] = 0
    c_cc[termios.VTIME] = 1

    packed = struct.pack(
        TERMIOS2_FMT,
        iflag, oflag, cflag, lflag, values[4],
        *c_cc,
        baud_rate, baud_rate,
    )
    fcntl.ioctl(fd, TCSETS2, packed)
    termios.tcflush(fd, termios.TCIOFLUSH)

def parse_sbus_frame(frame):
    if len(frame) != 25:
        return None
    if frame[0] != 0x0F or frame[24] not in (0x00, 0x0D):
        return None
    channels = [0] * 16
    channels[0]  = (frame[1]  | (frame[2]  << 8)) & 0x07FF
    channels[1]  = ((frame[2] >> 3) | (frame[3]  << 5)) & 0x07FF
    channels[2]  = ((frame[3] >> 6) | (frame[4]  << 2) | (frame[5] << 10)) & 0x07FF
    channels[3]  = ((frame[5] >> 2) | (frame[6]  << 6)) & 0x07FF
    channels[4]  = ((frame[6] >> 5) | (frame[7]  << 3)) & 0x07FF
    channels[5]  = (frame[8]  | (frame[9]  << 8)) & 0x07FF
    channels[6]  = ((frame[9] >> 3) | (frame[10] << 5)) & 0x07FF
    channels[7]  = ((frame[10] >> 6) | (frame[11] << 2) | (frame[12] << 10)) & 0x07FF
    channels[8]  = ((frame[12] >> 2) | (frame[13] << 6)) & 0x07FF
    channels[9]  = ((frame[13] >> 5) | (frame[14] << 3)) & 0x07FF
    channels[10] = (frame[15] | (frame[16] << 8)) & 0x07FF
    channels[11] = ((frame[16] >> 3) | (frame[17] << 5)) & 0x07FF
    channels[12] = ((frame[17] >> 6) | (frame[18] << 2) | (frame[19] << 10)) & 0x07FF
    channels[13] = ((frame[19] >> 2) | (frame[20] << 6)) & 0x07FF
    channels[14] = ((frame[20] >> 5) | (frame[21] << 3)) & 0x07FF
    channels[15] = (frame[22] | (frame[23] << 8)) & 0x07FF
    if not all(0 <= ch <= 2047 for ch in channels):
        return None

    # Match the SpeedRun aircraft transmitter profile and reject random IMU bytes
    # that accidentally look like a 25-byte SBUS frame.
    primary = channels[:8]
    plausible_count = sum(40 <= ch <= 1550 for ch in primary)
    if plausible_count < 4:
        return None
    if any(ch < 40 or ch > 1900 for ch in primary):
        return None
    return channels

def extract_sbus_frames(data):
    buf = bytearray(data)
    frames = []
    while True:
        try:
            idx = buf.index(0x0F)
        except ValueError:
            break
        if idx:
            del buf[:idx]
        if len(buf) < 25:
            break
        if buf[24] in (0x00, 0x0D):
            frame = bytes(buf[:25])
            channels = parse_sbus_frame(frame)
            if channels:
                frames.append((frame, channels))
            del buf[:25]
        else:
            del buf[0]
    return frames

def read_sample(port):
    fd = None
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        configure_sbus(fd)
        deadline = time.monotonic() + 1.2
        data = b""
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], 0.05)
            if not readable:
                continue
            try:
                chunk = os.read(fd, 512)
            except BlockingIOError:
                continue
            if chunk:
                data += chunk
                if len(extract_sbus_frames(data)) >= 2:
                    break
        return data, None
    except (OSError, termios.error, struct.error) as exc:
        return b"", str(exc)
    finally:
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass

found = False
for port in ports:
    data, error = read_sample(port)
    if error:
        if output_mode == "human":
            print(f"{port:<16} error: {error}")
        continue

    frames = extract_sbus_frames(data)
    if frames:
        channels = frames[0][1]
        channel_summary = ",".join(str(ch) for ch in channels[:8])
        if output_mode == "human":
            print(f"{port:<16} FOUND  frames={len(frames)} bytes={len(data)} ch1-8={channel_summary}")
        else:
            print(f"FOUND\t{port}\t{baud_rate}\tframes={len(frames)} bytes={len(data)} ch1-8={channel_summary}")
        found = True
    elif data:
        if output_mode == "human":
            print(f"{port:<16} data seen but no valid SBUS frame  bytes={len(data)}")
    else:
        if output_mode == "human":
            print(f"{port:<16} no SBUS data")

if not found and output_mode == "human":
    print()
    print("No aircraft joystick SBUS data found. Check receiver power, USB adapter, and whether the receiver is outputting SBUS.")
PY
}

probe_joy_devices() {
    run_joy_probe human
}

choose_device() {
    local label="$1"
    local allowed_hint="$2"
    local default_choice="$3"
    shift 3
    local choice
    local devs=()
    local dev

    collect_existing_devices devs "$@"

    echo >&2
    echo "Available ${label} devices:" >&2
    print_device_list devs "No ${label} devices found." >&2
    echo >&2
    if [[ -n "${default_choice}" ]]; then
        read -r -p "Enter ${label} number or device path, or press Enter for ${default_choice}: " choice
        choice="${choice:-${default_choice}}"
    else
        read -r -p "Enter ${label} number or device path, e.g. 1 or ${devs[0]:-/dev/ttyUSB0}: " choice
    fi

    if [[ "${choice}" =~ ^[0-9]+$ ]]; then
        if (( choice < 1 || choice > ${#devs[@]} )); then
            echo "Invalid ${label} number: ${choice}" >&2
            exit 1
        fi
        choice="${devs[$((choice - 1))]}"
    fi

    if [[ ! -e "${choice}" ]]; then
        echo "Invalid ${label} device: ${choice}" >&2
        echo "${allowed_hint}" >&2
        exit 1
    fi

    for dev in "${devs[@]}"; do
        if [[ "${choice}" == "${dev}" ]]; then
            echo "${choice}"
            return
        fi
    done

    echo "Invalid ${label} device: ${choice}" >&2
    echo "${allowed_hint}" >&2
    exit 1
}

choose_optional_device() {
    local label="$1"
    local allowed_hint="$2"
    local default_choice="$3"
    local default_answer="$4"
    shift 4
    local answer

    if [[ "${default_answer}" == "yes" ]]; then
        read -r -p "Configure ${label}? [Y/n]: " answer
        answer="${answer:-y}"
    else
        read -r -p "Configure ${label}? [y/N]: " answer
    fi
    case "${answer}" in
        y|Y|yes|YES)
            choose_device "${label}" "${allowed_hint}" "${default_choice}" "$@"
            ;;
        *)
            echo ""
            ;;
    esac
}

kernel_pattern_for_dev() {
    local dev="$1"
    basename "${dev}" | sed 's/[][\*?.^$+{}|()\\]/\\&/g'
}

write_port_rule() {
    local dev="$1"
    local symlink="$2"
    local kernel_scope="$3"
    local path
    local serial
    local kernel

    path="$(get_property "${dev}" ID_PATH)"
    serial="$(get_property "${dev}" ID_SERIAL_SHORT)"
    kernel="$(kernel_pattern_for_dev "${dev}")"

    if [[ -n "${path}" ]]; then
        printf 'SUBSYSTEM=="tty", KERNEL=="%s", ENV{ID_PATH}=="%s", MODE:="0777", SYMLINK+="%s"\n' "${kernel_scope}" "${path}" "${symlink}"
        return
    fi

    if [[ -n "${serial}" ]]; then
        printf 'SUBSYSTEM=="tty", KERNEL=="%s", ENV{ID_SERIAL_SHORT}=="%s", MODE:="0777", SYMLINK+="%s"\n' "${kernel_scope}" "${serial}" "${symlink}"
        return
    fi

    printf 'SUBSYSTEM=="tty", KERNEL=="%s", MODE:="0777", SYMLINK+="%s"\n' "${kernel}" "${symlink}"
}

write_imu_rule() {
    local dev="$1"

    case "${dev}" in
        /dev/ttyUSB*|/dev/ttyACM*)
            write_port_rule "${dev}" "ttyimu" "tty*"
            ;;
        *)
            write_port_rule "${dev}" "ttyimu" "$(basename "${dev}")"
            ;;
    esac
}

write_joy_rule() {
    local dev="$1"

    write_port_rule "${dev}" "ttyjoy" "ttyUSB*"
}

capture_rules() {
    local imu_devs=()
    local joy_devs=()
    local imu_dev
    local joy_dev
    local probe_results
    local found_count
    local default_imu_dev
    local joy_probe_results
    local joy_found_count
    local default_joy_dev
    local default_joy_answer

    collect_existing_devices imu_devs "${IMU_GLOBS[@]}"
    collect_existing_devices joy_devs "${JOY_GLOBS[@]}"

    if (( ${#imu_devs[@]} == 0 )); then
        echo "Cannot find any IMU candidate serial port. Check USB/UART wiring first." >&2
        exit 1
    fi

    echo "Probing IMU candidates with HiPNUC LOG VERSION..."
    probe_results="$(run_imu_probe raw || true)"
    found_count="$(printf '%s\n' "${probe_results}" | awk -F '\t' '$1 == "FOUND" { count++ } END { print count + 0 }')"
    default_imu_dev=""

    if (( found_count > 0 )); then
        echo "Detected IMU candidate:"
        printf '%s\n' "${probe_results}" | awk -F '\t' '$1 == "FOUND" { printf "  %s at %s baud\n", $2, $3 }'
        if (( found_count == 1 )); then
            default_imu_dev="$(printf '%s\n' "${probe_results}" | awk -F '\t' '$1 == "FOUND" { print $2; exit }')"
        fi
    else
        echo "No IMU response detected automatically. You can still choose a port manually."
        echo "If this is a wiring test, check power, TX/RX direction, and baud rate."
    fi

    echo
    echo "Choose the device used by the IMU. IMU can be USB or UART."
    imu_dev="$(choose_device "IMU" "Supported IMU ports: /dev/ttyUSB0-5, /dev/ttyACM0-5, /dev/ttyS0-9, /dev/ttyAMA0-9, /dev/ttyTHS0-9." "${default_imu_dev}" "${IMU_GLOBS[@]}")"

    joy_dev=""
    if (( ${#joy_devs[@]} > 0 )); then
        echo
        echo "Aircraft joystick stays USB only."
        echo "Probing ttyUSB0-ttyUSB5 for aircraft joystick SBUS data..."
        joy_probe_results="$(run_joy_probe raw || true)"
        joy_found_count="$(printf '%s\n' "${joy_probe_results}" | awk -F '\t' '$1 == "FOUND" { count++ } END { print count + 0 }')"
        default_joy_dev=""
        default_joy_answer="no"
        if (( joy_found_count > 0 )); then
            echo "Detected aircraft joystick candidate:"
            printf '%s\n' "${joy_probe_results}" | awk -F '\t' '$1 == "FOUND" { printf "  %s at %s baud\n", $2, $3 }'
            if (( joy_found_count == 1 )); then
                default_joy_dev="$(printf '%s\n' "${joy_probe_results}" | awk -F '\t' '$1 == "FOUND" { print $2; exit }')"
                default_joy_answer="yes"
            fi
        else
            echo "No aircraft joystick SBUS data detected automatically. You can still choose a USB port manually."
        fi
        joy_dev="$(choose_optional_device "aircraft joystick" "Only /dev/ttyUSB${TTYUSB_MIN} through /dev/ttyUSB${TTYUSB_MAX} are supported for aircraft joystick." "${default_joy_dev}" "${default_joy_answer}" "${JOY_GLOBS[@]}")"
    fi

    rm -f "${LEGACY_RULES_DST}"
    {
        echo "# Generated by $(basename "$0") capture."
        echo "# Maps selected ports to stable Phybot links."
        echo "# Use /dev/ttyimu and /dev/ttyjoy in applications, not temporary kernel numbers."
        write_imu_rule "${imu_dev}"
        if [[ -n "${joy_dev}" ]]; then
            write_joy_rule "${joy_dev}"
        fi
    } > "${RULES_DST}"
    chmod 0644 "${RULES_DST}"
}

show_status() {
    echo "udev rules:"
    if [[ -f "${RULES_DST}" ]]; then
        sed 's/^/  /' "${RULES_DST}"
    else
        echo "  not installed: ${RULES_DST}"
    fi

    echo
    echo "device links:"
    for dev in /dev/ttyimu /dev/ttyjoy; do
        if [[ -e "${dev}" ]]; then
            ls -l "${dev}"
        else
            echo "  missing: ${dev}"
        fi
    done
}

diagnose() {
    list_devices
    echo
    show_status
}

reload_udev() {
    udevadm control --reload-rules
    udevadm trigger
}

COMMAND="${1:-capture}"

case "${COMMAND}" in
    install)
        require_root
        capture_rules
        reload_udev
        chmod 0777 /dev/ttyimu 2>/dev/null || true
        show_status
        ;;
    capture)
        require_root
        capture_rules
        reload_udev
        chmod 0777 /dev/ttyimu 2>/dev/null || true
        show_status
        ;;
    list)
        list_devices
        ;;
    probe)
        require_root
        probe_imu_devices
        ;;
    joy-probe)
        require_root
        probe_joy_devices
        ;;
    diagnose)
        diagnose
        ;;
    status)
        show_status
        ;;
    remove)
        require_root
        rm -f "${RULES_DST}" "${LEGACY_RULES_DST}"
        reload_udev
        show_status
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
