# N64 reference renderer for visual parity checks: native arm64 mupen64plus 2.5.9. Debian bookworm ships no
# arm64 build of the mupen64plus core or plugins (only the arch-all -all meta packages and mupen64plus-data),
# so the core, console front end and plugins are built from the 2.5.9 tags. Runs under Xvfb with software GL.
# The maintainer's ROM is bind-mounted at run time, never copied into the image.
FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates build-essential git pkg-config libsdl2-dev libmupen64plus-dev \
        libsamplerate0-dev libspeexdsp-dev libfreetype-dev libpng-dev zlib1g-dev libgl-dev libglu1-mesa-dev \
        libboost-filesystem-dev libboost-system-dev \
        mupen64plus-data xvfb x11-utils imagemagick libgl1-mesa-dri \
    && rm -rf /var/lib/apt/lists/*
# CFLAGS=-fcommon: 2.5.9 defines globals in headers (idec.h), which gcc 10+ -fno-common rejects at link time.
RUN set -eux; \
    export CFLAGS=-fcommon; \
    mkdir -p /src && cd /src; \
    git clone -q --depth 1 --branch 2.5.9 https://github.com/mupen64plus/mupen64plus-core core; \
    make -C core/projects/unix all -j"$(nproc)"; \
    make -C core/projects/unix install PREFIX=/usr/local; \
    git clone -q --depth 1 --branch 2.5.9 https://github.com/mupen64plus/mupen64plus-ui-console ui; \
    make -C ui/projects/unix all -j"$(nproc)" APIDIR=/usr/include/mupen64plus \
        COREDIR=/usr/local/lib/ PLUGINDIR=/usr/local/lib/mupen64plus SHAREDIR=/usr/local/share/mupen64plus; \
    install -m 0755 ui/projects/unix/mupen64plus /usr/local/bin/mupen64plus; \
    rm -rf /src/core /src/ui
# Plugins. The 2.5.9 Makefiles of glide64mk2 and rice reject HOST_CPU=aarch64; rice is built as its generic
# NO_ASM ARM path (HOST_CPU=arm64), which only changes CPU-specific code paths, not the GL readback.
# glide64mk2 loads Glide64mk2.ini from the shared data dir, so that file is installed alongside it.
RUN set -eux; \
    export CFLAGS=-fcommon; \
    cd /src; \
    install -d /usr/local/lib/mupen64plus; \
    for p in video-glide64mk2 video-rice rsp-hle audio-sdl input-sdl; do \
        hostcpu=$(uname -m); \
        if [ "$p" = video-rice ]; then hostcpu=arm64; fi; \
        git clone -q --depth 1 --branch 2.5.9 https://github.com/mupen64plus/mupen64plus-$p $p; \
        make -C $p/projects/unix all -j"$(nproc)" APIDIR=/usr/include/mupen64plus HOST_CPU=$hostcpu; \
        install -m 0644 $p/projects/unix/mupen64plus-*.so /usr/local/lib/mupen64plus/; \
        if [ -f $p/data/Glide64mk2.ini ]; then install -m 0644 $p/data/Glide64mk2.ini /usr/local/share/mupen64plus/; fi; \
        rm -rf $p; \
    done
ENV SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3
WORKDIR /work
