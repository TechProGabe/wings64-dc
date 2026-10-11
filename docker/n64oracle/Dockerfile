# N64 reference renderer for visual parity checks (mupen64plus + GLideN64/glide64mk2 under Xvfb).
# Runs the maintainer's own ROM (bind-mounted, never in the image) and captures frames with --testshots.
FROM --platform=linux/amd64 ubuntu:22.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        mupen64plus-ui-console mupen64plus-video-all mupen64plus-rsp-all \
        mupen64plus-input-all mupen64plus-audio-all xvfb x11-utils imagemagick libgl1-mesa-dri \
    && rm -rf /var/lib/apt/lists/*
ENV SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /work
