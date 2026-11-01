# Builds auth/ (C++ PAM module + face/fingerprint libs) and app/ (Tauri).
# Prerequisites:
#   auth: cmake, ninja/make, libpam0g-dev, libcli11-dev, libturbojpeg0-dev.
#         libcamera is built from source (auth/BundleLibcamera.cmake) and needs
#         meson, ninja-build, python3-jinja2, python3-yaml, python3-ply,
#         libudev-dev, libdrm-dev, libegl-dev, libgles-dev.
#   app:  bun, rustup/cargo, tauri-cli v2, webkit2gtk, libssl-dev.
# Full package lists: CONTRIBUTING.md, "Install Dependencies".

.PHONY: all build build-auth build-app test-auth \
        package package-app \
        clean clean-auth clean-app help

BUILD_TYPE   ?= Release
AUTH_BUILD   := auth/build
# Extra CMake flags, e.g. CMAKE_ARGS=-DBUILD_TESTS=ON
CMAKE_ARGS   ?=
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
	      -DPROJECT_VERSION=$(VERSION) \
	      $(CMAKE_ARGS)
	@echo "==> [auth] Building…"
	cmake --build $(AUTH_BUILD) --config $(BUILD_TYPE) --parallel

# Builds auth with BUILD_TESTS=ON and runs the unit tests registered with ctest.
test-auth:
	$(MAKE) build-auth CMAKE_ARGS="$(CMAKE_ARGS) -DBUILD_TESTS=ON"
	ctest --test-dir $(AUTH_BUILD) --output-on-failure

clean-auth:
	@echo "==> [auth] Cleaning build directory…"
	rm -rf $(AUTH_BUILD)

# Tauri bundles the auth .so files, so build them first.
# Cargo.toml carries the app version (CARGO_PKG_VERSION, bundle names), so
# VERSION is stamped in for the build and Cargo.toml/Cargo.lock are put back
# after, even on failure, so a build never leaves tracked files modified.
build-app: build-auth
	@echo "==> [app] Installing JS dependencies…"
	cd $(APP_DIR) && bun install --frozen-lockfile
	@echo "==> [app] Building Tauri application (version $(VERSION))…"
	@set -e; \
	  src="$(CURDIR)/$(APP_DIR)/src-tauri"; bak="$$src/target/version-stamp"; \
	  mkdir -p "$$bak"; cp "$$src/Cargo.toml" "$$src/Cargo.lock" "$$bak/"; \
	  trap 'cp "$$bak/Cargo.toml" "$$bak/Cargo.lock" "$$src/"' EXIT; \
	  trap 'exit 130' INT TERM; \
	  sed -i -E 's/^version = "[^"]*"/version = "$(VERSION)"/' "$$src/Cargo.toml"; \
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
	@echo "    test-auth      Build auth with tests and run them (ctest)"
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
