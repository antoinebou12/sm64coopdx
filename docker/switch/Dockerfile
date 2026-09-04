# Reproducible Nintendo Switch / Horizon build environment.
FROM devkitpro/devkita64:latest

RUN dkp-pacman -Syu --noconfirm \
        switch-sdl2 \
        switch-mesa \
        switch-curl \
        switch-zlib \
    && apt-get update -qq \
    && apt-get install -y -qq git python3 bsdextrautils \
    && rm -rf /var/lib/apt/lists/* \
    && dkp-pacman -Scc --noconfirm

ENV DEVKITPRO=/opt/devkitpro
ENV PATH="${DEVKITPRO}/devkitA64/bin:${DEVKITPRO}/tools/bin:${PATH}"
WORKDIR /work

ENTRYPOINT ["/bin/bash"]
