# wings64-dc build container.
#   toolchain : sh-elf GCC (dc-chain profile from pins.env), m4-single-only multilib. ~1 h.
#   dev       : KallistiOS built with -m4-single-only, mkdcdisc, python3.
# The repo is bind-mounted at /work. The ROM never enters the image.
# Build: scripts/ctr.sh build   (reads docker/pins.env into --build-arg)

FROM debian:bookworm AS base
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential git ca-certificates curl wget bzip2 xz-utils patch texinfo flex bison \
        libgmp-dev libmpfr-dev libmpc-dev libelf-dev libjpeg-dev libpng-dev \
        python3 python3-venv pkg-config meson ninja-build libisofs-dev \
    && rm -rf /var/lib/apt/lists/*
COPY fetch-src.sh /usr/local/bin/fetch-src.sh

FROM base AS toolchain
ARG KOS_URL
ARG KOS_REV
ARG TOOLCHAIN_PROFILE
ARG MAKEJOBS=6
RUN fetch-src.sh "$KOS_URL" "$KOS_REV" /opt/kos-src \
 && cd /opt/kos-src/utils/dc-chain \
 && make build toolchain_profile=$TOOLCHAIN_PROFILE makejobs=$MAKEJOBS verbose=0 \
        precision_modes=m4-single,m4-single-only default_precision=m4-single \
        enable_objc=0 enable_objcpp=0 \
 && rm -rf /opt/kos-src

FROM toolchain AS dev
ARG KOS_URL
ARG KOS_REV
ARG KOS_SH4_PRECISION
ARG MKDCDISC_URL
ARG MKDCDISC_REV
ARG MAKEJOBS=6
# KallistiOS at the pin, built with the port's FP ABI. No ARM toolchain: KOS uses its prebuilt stream.drv.
RUN fetch-src.sh "$KOS_URL" "$KOS_REV" /opt/toolchains/dc/kos \
 && cd /opt/toolchains/dc/kos \
 && sed -e "s|^export KOS_SH4_PRECISION=.*|export KOS_SH4_PRECISION=\"$KOS_SH4_PRECISION\"|" \
        doc/environ.sh.sample > environ.sh \
 && grep -q "KOS_SH4_PRECISION=\"$KOS_SH4_PRECISION\"" environ.sh \
 && bash -c '. ./environ.sh && make -j$MAKEJOBS && make -C utils/bin2c && make -C utils/scramble && make -C utils/makeip' \
 && echo "$KOS_REV" > /opt/toolchains/dc/kos/.wings64-rev
RUN fetch-src.sh "$MKDCDISC_URL" "$MKDCDISC_REV" /opt/mkdcdisc-src \
 && cd /opt/mkdcdisc-src && meson setup builddir --buildtype=release && meson compile -C builddir \
 && install -m 755 builddir/mkdcdisc /usr/local/bin/mkdcdisc \
 && rm -rf /opt/mkdcdisc-src
ENV KOS_BASE=/opt/toolchains/dc/kos
WORKDIR /work
