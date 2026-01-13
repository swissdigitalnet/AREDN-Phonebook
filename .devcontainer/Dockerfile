FROM mcr.microsoft.com/devcontainers/base:ubuntu

# Rename vscode user to dev (base image has vscode with UID 1000)
RUN usermod -l dev -d /home/dev -m vscode \
    && groupmod -n dev vscode \
    && sed -i 's/vscode/dev/g' /etc/sudoers.d/vscode || true \
    && mv /etc/sudoers.d/vscode /etc/sudoers.d/dev 2>/dev/null || true

# Install OpenWrt SDK build dependencies (matching GitHub Actions workflow)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libncurses5-dev \
    libncursesw5-dev \
    zlib1g-dev \
    gawk \
    git \
    gettext \
    libssl-dev \
    xsltproc \
    rsync \
    wget \
    unzip \
    python3 \
    python3-pip \
    file \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Set up OpenWrt SDK environment variables
ENV OPENWRT_SDK_VERSION=23.05.4
ENV OPENWRT_SDK_URL=https://downloads.openwrt.org/releases/23.05.4/targets/ath79/generic/openwrt-sdk-23.05.4-ath79-generic_gcc-12.3.0_musl.Linux-x86_64.tar.xz

# Download and extract OpenWrt SDK
RUN mkdir -p /opt/openwrt \
    && cd /opt/openwrt \
    && wget -q "$OPENWRT_SDK_URL" -O sdk.tar.xz \
    && tar -xf sdk.tar.xz \
    && mv openwrt-sdk-* sdk \
    && rm sdk.tar.xz \
    && chown -R dev:dev /opt/openwrt

USER dev
WORKDIR /workspaces/AREDN-Phonebook
