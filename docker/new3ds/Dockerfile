# Reproducible New Nintendo 3DS build environment.
FROM devkitpro/devkitarm:latest

RUN dkp-pacman -Syu --noconfirm 3ds-dev 3ds-zlib \
    && apt-get update -qq \
    && apt-get install -y -qq git python3 curl bsdextrautils \
    && rm -rf /var/lib/apt/lists/* \
    && dkp-pacman -Scc --noconfirm

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV PATH="${DEVKITPRO}/devkitARM/bin:${DEVKITPRO}/tools/bin:${PATH}"
WORKDIR /work

ENTRYPOINT ["/bin/bash"]
