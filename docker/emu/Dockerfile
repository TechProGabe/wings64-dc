# wings64-dc emulator image: Flycast built from source, run headless under Xvfb.
# Build: scripts/ctr.sh emu-build. Run: scripts/emu_smoke.sh (serial log + screenshots).
FROM debian:bookworm AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates pkg-config python3 \
        libsdl2-dev libgl-dev libegl-dev libx11-dev libudev-dev libzip-dev libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
ARG FLYCAST_URL
ARG FLYCAST_TAG
ARG FLYCAST_REV
RUN git clone -q --depth 1 --branch "$FLYCAST_TAG" --recurse-submodules --shallow-submodules "$FLYCAST_URL" /src \
 && test "$(git -C /src rev-parse HEAD)" = "$FLYCAST_REV" \
 && cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DUSE_VULKAN=OFF -DUSE_ALSA=OFF -DUSE_LIBAO=OFF -DUSE_PULSEAUDIO=OFF -DUSE_BREAKPAD=OFF -DUSE_LUA=OFF \
 && cmake --build /build -j6 \
 && install -m 755 /build/flycast /usr/local/bin/flycast

FROM debian:bookworm
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libsdl2-2.0-0 libgl1 libegl1 libgl1-mesa-dri libglx-mesa0 libzip4 libcurl4 libudev1 libgomp1 \
        xvfb x11-apps x11-utils imagemagick procps \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /usr/local/bin/flycast /usr/local/bin/flycast
ENV SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /work
