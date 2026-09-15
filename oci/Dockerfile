ARG ARCH=aarch64
ARG REPO=docker.io/axisecp
ARG SDK_VERSION=12.11.0
ARG UBUNTU_VERSION=24.04

FROM ${REPO}/acap-native-sdk:${SDK_VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}

RUN DEBIAN_FRONTEND=noninteractive \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake \
        file \
        git \
        ninja-build \
        pkgconf && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
