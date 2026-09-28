##################### Overridable arguments ####################################
ARG ARCH=aarch64
ARG REPO=docker.io/axisecp
ARG SDK_VERSION=12.11.0
ARG UBUNTU_VERSION=24.04

FROM ${REPO}/acap-native-sdk:${SDK_VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

ARG NODE_MAJOR=22
ARG YARN_VERSION=1.22.22

##################### General setup ############################################

# Install build tools and package setup dependencies:
RUN DEBIAN_FRONTEND=noninteractive \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        curl \
        file \
        git \
        gnupg \
        ninja-build \
        pkgconf && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Install Node.js and Yarn:
RUN install -d -m 0755 /etc/apt/keyrings && \
    curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | \
        gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg && \
    echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg]" \
        "https://deb.nodesource.com/node_${NODE_MAJOR}.x nodistro main" \
        > /etc/apt/sources.list.d/nodesource.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends nodejs && \
    npm install --global yarn@${YARN_VERSION} && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
