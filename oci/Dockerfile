ARG ARCH=aarch64
ARG REPO=docker.io/axisecp
ARG SDK_VERSION=12.11.0
ARG UBUNTU_VERSION=24.04
ARG NODE_MAJOR=22

FROM ${REPO}/acap-native-sdk:${SDK_VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

ARG NODE_MAJOR

RUN DEBIAN_FRONTEND=noninteractive \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake \
        curl \
        file \
        git \
        gnupg \
        ninja-build \
        pkgconf && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN install -d -m 0755 /etc/apt/keyrings && \
    curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | \
        gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg && \
    echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_${NODE_MAJOR}.x nodistro main" \
        > /etc/apt/sources.list.d/nodesource.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends nodejs && \
    npm install --global yarn && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
