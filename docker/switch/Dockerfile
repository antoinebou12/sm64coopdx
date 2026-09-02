# Reproducible Nintendo Switch / Horizon build environment.
# Uses the same devkitPro image family as GitHub Actions.
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

COPY scripts/ci/switch-portability.sh /usr/local/bin/switch-portability.sh
RUN chmod +x /usr/local/bin/switch-portability.sh

ENTRYPOINT ["/bin/bash"]
