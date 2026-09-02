# Reproducible New Nintendo 3DS build environment.
# Uses the same devkitPro image family as GitHub Actions.
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

COPY scripts/ci/new3ds-shell.sh /usr/local/bin/new3ds-shell.sh
COPY scripts/ci/new3ds-integration.sh /usr/local/bin/new3ds-integration.sh
RUN chmod +x /usr/local/bin/new3ds-shell.sh /usr/local/bin/new3ds-integration.sh

ENTRYPOINT ["/bin/bash"]
