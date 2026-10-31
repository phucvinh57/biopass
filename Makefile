# Builds auth/ (C++ PAM module + face/fingerprint libs) and app/ (Tauri).
# Prerequisites:
#   auth: cmake, ninja/make, libpam0g-dev, libcli11-dev, libturbojpeg0-dev.
#         libcamera is built from source (auth/BundleLibcamera.cmake) and needs
#         meson, ninja-build, python3-jinja2, python3-yaml, python3-ply,
#         libudev-dev, libdrm-dev, libegl-dev, libgles-dev.
#   app:  bun, rustup/cargo, tauri-cli v2, webkit2gtk, libssl-dev.

.PHONY: all build build-auth build-app \
        package package-app \
        clean clean-auth clean-app \
        install-deps

BUILD_TYPE   ?= Release
AUTH_BUILD   := auth/build
APP_DIR      := app
VERSION      ?= $(shell git describe --tags --always 2>/dev/null || echo "0.1.0")

all: build

build: build-auth build-app

package: package-app

build-auth:
	@echo "==> [auth] Configuring CMake (BUILD_TYPE=$(BUILD_TYPE))…"
	cmake -S auth -B $(AUTH_BUILD) \
	      -Wno-deprecated \
	      -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	      -DCMAKE_WARN_DEPRECATED=OFF \
	      -DCMAKE_ERROR_DEPRECATED=OFF \
	      -DPROJECT_VERSION=$(VERSION)
	@echo "==> [auth] Building…"
	cmake --build $(AUTH_BUILD) --config $(BUILD_TYPE) --parallel


clean-auth:
	@echo "==> [auth] Cleaning build directory…"
	rm -rf $(AUTH_BUILD)

# Tauri bundles the auth .so files, so build them first.
build-app: build-auth
	@echo "==> [app] Installing JS dependencies…"
	cd $(APP_DIR) && bun install --frozen-lockfile
	@echo "==> [app] Pinning model downloader to release $(VERSION)…"
	sed -i -E 's#^const DEFAULT_MODELS_URL: &str = "https://github.com/phucvinh57/biopass/releases/(latest/download|download/[^"]+)";#const DEFAULT_MODELS_URL: \&str = "https://github.com/phucvinh57/biopass/releases/download/$(VERSION)";#' \
	    $(APP_DIR)/src-tauri/src/models.rs
	@echo "==> [app] Pinning app version to $(VERSION)…"
	sed -i -E 's/^version = "[^"]*"/version = "$(VERSION)"/' \
	    $(APP_DIR)/src-tauri/Cargo.toml
	@echo "==> [app] Building Tauri application…"
	cd $(APP_DIR) && bun run tauri build

package-app: build-app
	@echo "==> [app] Tauri packages are in app/src-tauri/target/release/bundle/"
	@ls app/src-tauri/target/release/bundle/deb/*.deb \
	     app/src-tauri/target/release/bundle/rpm/*.rpm 2>/dev/null || true

clean-app:
	@echo "==> [app] Cleaning build artifacts…"
	cd $(APP_DIR) && cargo clean --manifest-path src-tauri/Cargo.toml 2>/dev/null || true
	rm -rf $(APP_DIR)/dist

clean: clean-auth clean-app

help:
	@echo ""
	@echo "  Usage: make [target] [VAR=value …]"
	@echo ""
	@echo "  Targets:"
	@echo "    build          Build both auth and app (default)"
	@echo "    build-auth     Configure + build the C++ PAM module only"
	@echo "    build-app      Install JS deps + build the Tauri app only"
	@echo "    package        Build + package everything into combined Linux bundles"
	@echo "    package-app    Show Tauri combined bundle output paths"
	@echo "    clean          Remove all build output"
	@echo "    clean-auth     Remove auth/build/"
	@echo "    clean-app      Remove app build artifacts"
	@echo ""
	@echo "  Variables:"
	@echo "    BUILD_TYPE     CMake build type (default: Release)"
	@echo "    VERSION        Package version (default: git tag or 0.1.0)"
	@echo ""
