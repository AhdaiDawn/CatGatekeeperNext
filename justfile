set shell := ["bash", "-uc"]

build_dir := "build"

# Show available commands.
default:
    @just --list

# Configure the CMake build directory.
configure build_type="Debug":
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE="{{build_type}}"

# Configure and build the project.
build build_type="Debug":
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE="{{build_type}}"
    cmake --build {{build_dir}}

# Build with a release configuration.
release:
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE=Release
    cmake --build {{build_dir}}

# Build a portable release directory and tar.gz archive under dist/.
package build_type="Release":
    ./tools/package-portable.sh --build-type "{{build_type}}"

# Install from the current build directory.
install prefix="":
    if [ -n "{{prefix}}" ]; then cmake --install {{build_dir}} --prefix "{{prefix}}"; else cmake --install {{build_dir}}; fi

# Generate processed runtime assets.
assets:
    ./tools/preprocess-assets.sh

# Run basic local verification.
check:
    bash -n tools/preprocess-assets.sh
    bash -n tools/package-portable.sh
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE=Debug
    cmake --build {{build_dir}}

# Run the overlay directly for visual testing.
overlay seconds="20":
    QT_QPA_PLATFORM=wayland {{build_dir}}/cat-gatekeeper-overlay --sleep-seconds "{{seconds}}" --screen primary

# Run the daemon manually.
daemon:
    {{build_dir}}/cat-gatekeeperd

# Show daemon status.
status:
    {{build_dir}}/cat-gatekeeperctl status

# Trigger one reminder immediately.
trigger:
    {{build_dir}}/cat-gatekeeperctl trigger

# Close the current reminder overlay.
dismiss:
    {{build_dir}}/cat-gatekeeperctl dismiss

# Exit the daemon.
quit:
    {{build_dir}}/cat-gatekeeperctl quit

# Remove the CMake build directory.
clean:
    rm -rf {{build_dir}}
