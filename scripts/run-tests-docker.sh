#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
qt_dir="${QT_ROOT_DIR:-$HOME/Qt/6.12.0/gcc_64}"
work_dir="$project_dir/cmake-build-docker"

if [[ ${1:-} == --help ]]; then
    echo "Usage: $0 [CTest options, e.g. -R Encoding_LinuxMedia]"
    echo "Requires local Docker and the Linux Qt 6.12.0 SDK (QT_ROOT_DIR)."
    echo "Defaults: SNAPTRAY_DOCKER_CPUS=4 SNAPTRAY_DOCKER_MEMORY=8g CMAKE_BUILD_PARALLEL_LEVEL=4"
    echo "Host reports: $work_dir/results/<run>/"
    exit 0
fi
if [[ ! -f "$qt_dir/lib/libQt6Core.so.6.12.0" || ! -f "$qt_dir/plugins/imageformats/libqwebp.so" ]]; then
    echo "Qt SDK at '$qt_dir' is incomplete or incompatible. Set QT_ROOT_DIR to the Linux Qt 6.12.0 gcc_64 SDK with qtimageformats." >&2
    exit 1
fi
qt_dir="$(cd "$qt_dir" && pwd)"
mkdir -p "$work_dir/build" "$work_dir/results"
exec 9>"$work_dir/build.lock"
flock -n 9 || { echo "Another Docker test run is using $work_dir." >&2; exit 1; }
results_dir="$(mktemp -d "$work_dir/results/$(date +%Y%m%d-%H%M%S)-XXXXXX")"
echo "Host test results: $results_dir"

image="snaptray-linux-tests:ubuntu22.04-$(id -u)"
docker build --build-arg "TEST_UID=$(id -u)" --tag "$image" \
    "$project_dir/scripts/docker-tests" 2>&1 | tee "$results_dir/image-build.log"

docker run --rm --init -i --user "$(id -u):$(id -g)" \
    --cpus "${SNAPTRAY_DOCKER_CPUS:-4}" --memory "${SNAPTRAY_DOCKER_MEMORY:-8g}" \
    --shm-size 256m \
    --mount "type=bind,src=$project_dir,dst=/src,readonly" \
    --mount "type=bind,src=$qt_dir,dst=/opt/qt,readonly" \
    --mount "type=bind,src=$work_dir/build,dst=/build" \
    --mount "type=bind,src=$results_dir,dst=/results" \
    -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-4}" \
    -e XDG_SESSION_TYPE=x11 -e SNAPTRAY_TEST_X11_CLIPBOARD=1 \
    "$image" bash -se -- "$@" <<'BUILD_AND_TEST' 2>&1 | tee "$results_dir/run.log"
export XDG_RUNTIME_DIR="$(mktemp -d)"
cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/qt
cmake --build /build

# Keep only evidence produced by this invocation of CTest.
test_start="$(mktemp)"
status=0
dbus-run-session -- xvfb-run -a -s '-screen 0 1920x1080x24' \
    ctest --test-dir /build --output-on-failure --no-tests=error \
    --output-junit /results/test-results.xml "$@" || status=$?
for report in /build/Testing/Temporary/LastTest.log /build/Testing/Temporary/LastTestsFailed.log \
              /build/boundary-snap-metrics.json; do
    if [[ "$report" -nt "$test_start" ]]; then
        cp "$report" /results/
    fi
done
exit "$status"
BUILD_AND_TEST
