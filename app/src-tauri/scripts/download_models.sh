#!/bin/bash
# download_models.sh — Downloads Biopass AI models to the user's data directory.
# This script is invoked by package installer hooks (RPM preinstall / Debian postinst).

set -euo pipefail

# Overwritten at package/bundle time (see Makefile build-app) to pin this to
# the exact release tag being built, so RC/beta bundles fetch their own
# models instead of whatever the latest stable release happens to be.
BASE_URL="https://github.com/TickLabVN/biopass/releases/download/<tag>"

MODELS=(
    "${BASE_URL}/yolov8n-face.onnx"
    "${BASE_URL}/edgeface_s_gamma_05.onnx"
    "${BASE_URL}/edgeface_xs_gamma_06.onnx"
    "${BASE_URL}/mobilenetv3_antispoof.onnx"
    "${BASE_URL}/minifas_v2.onnx"
)

LEGACY_MODELS=(
    "yolov11n-face.torchscript"
    "edgeface_s_gamma_05_ts.pt"
    "mobilenetv3_antispoof_ts.pt"
)

# Determine the data dir. If running as root (e.g. system-wide install),
# use SUDO_USER's home. Without SUDO_USER (PackageKit/Discover, pacman, dnf
# run from a root shell) there is no user to install for; the app downloads
# missing models into the user's home on first launch instead.
if [ "$(id -u)" -eq 0 ] && [ "${SUDO_USER:-root}" = "root" ]; then
    echo "Biopass: No invoking user found; models will be downloaded when Biopass is first opened."
    exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    APP_DATA_DIR="${USER_HOME}/.local/share/com.ticklab.biopass"
    DATA_DIR="${APP_DATA_DIR}/models"
    CONFIG_DIR="${USER_HOME}/.config/com.ticklab.biopass"
else
    APP_DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/com.ticklab.biopass"
    DATA_DIR="${APP_DATA_DIR}/models"
    CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/com.ticklab.biopass"
fi

CONFIG_FILE="${CONFIG_DIR}/config.yaml"

escape_sed_replacement() {
    printf '%s' "$1" | sed -e 's/[\/&]/\\&/g'
}

migrate_config_models() {
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "Biopass: No existing config found at $CONFIG_FILE, skipping config migration."
        return
    fi

    echo "Biopass: Migrating model paths in $CONFIG_FILE"
    local escaped_data_dir
    escaped_data_dir="$(escape_sed_replacement "$DATA_DIR")"

    # 1) Rename legacy model filenames to current ONNX filenames everywhere.
    sed -i \
        -e 's/yolov11n-face\.torchscript/yolov8n-face.onnx/g' \
        -e 's/edgeface_s_gamma_05_ts\.pt/edgeface_s_gamma_05.onnx/g' \
        -e 's/mobilenetv3_antispoof_ts\.pt/mobilenetv3_antispoof.onnx/g' \
        "$CONFIG_FILE"

    # 2) Force face model model/path entries to use the new files from ~/.local/share/.../models.
    sed -E -i \
        -e "s#^([[:space:]]*(model|path):[[:space:]]*)['\"]?[^'\"[:space:]]*yolov8n-face\\.onnx['\"]?[[:space:]]*\$#\\1${escaped_data_dir}/yolov8n-face.onnx#g" \
        -e "s#^([[:space:]]*(model|path):[[:space:]]*)['\"]?[^'\"[:space:]]*edgeface_s_gamma_05\\.onnx['\"]?[[:space:]]*\$#\\1${escaped_data_dir}/edgeface_s_gamma_05.onnx#g" \
        -e "s#^([[:space:]]*(model|path):[[:space:]]*)['\"]?[^'\"[:space:]]*mobilenetv3_antispoof\\.onnx['\"]?[[:space:]]*\$#\\1${escaped_data_dir}/mobilenetv3_antispoof.onnx#g" \
        "$CONFIG_FILE"
}

remove_legacy_models() {
    local removed=0
    for filename in "${LEGACY_MODELS[@]}"; do
        local old_model_path="${DATA_DIR}/${filename}"
        if [ -f "$old_model_path" ]; then
            echo "Biopass: Removing legacy model $filename"
            rm -f "$old_model_path"
            removed=1
        fi
    done

    if [ "$removed" -eq 0 ]; then
        echo "Biopass: No legacy models to remove."
    fi
}

migrate_config_models
remove_legacy_models

echo "Biopass: Ensuring model directory exists at $DATA_DIR"
mkdir -p "$DATA_DIR"

for entry in "${MODELS[@]}"; do
    url="$entry"
    filename="$(basename "$url")"
    dest="$DATA_DIR/$filename"

    if [ -f "$dest" ]; then
        echo "Biopass: Model already present, skipping: $filename"
    else
        echo "Biopass: Downloading $filename ..."
        if curl -fL --retry 3 --retry-delay 2 -C - -o "$dest" "$url"; then
            echo "Biopass: Downloaded $filename"
        else
            echo "Biopass: WARNING — Failed to download $filename. The app may not function correctly until models are available." >&2
            rm -f "$dest"  # Remove partial file
        fi
    fi
done

echo "Biopass: Model download complete."

# Fix ownership back to the actual user if running under sudo
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
    chown -R "$SUDO_USER:$SUDO_USER" "$APP_DATA_DIR"
    if [ -d "$CONFIG_DIR" ]; then
        chown -R "$SUDO_USER:$SUDO_USER" "$CONFIG_DIR"
    fi
fi
