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

# SSH quiet config
RUN mkdir -p /home/dev/.ssh \
    && printf 'Host *\n    StrictHostKeyChecking accept-new\n    LogLevel ERROR\n' > /home/dev/.ssh/config \
    && chmod 700 /home/dev/.ssh \
    && chmod 600 /home/dev/.ssh/config \
    && chown -R dev:dev /home/dev/.ssh

USER dev
WORKDIR /workspaces/AREDN-Phonebook

# tmux — terminal multiplexer for SSH sessions
USER root
RUN apt-get update && apt-get install -y --no-install-recommends tmux \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
USER dev

# Per-user npm prefix on PATH so claude (installed into ~/.npm-global/bin by
# postCreate) and the superclaude alias resolve. MUST come before the tmux guard
# below: the guard execs tmux, so bashrc lines after it never run on SSH login.
RUN echo 'export PATH="$HOME/.npm-global/bin:$PATH"' >> /home/dev/.bashrc

# Force UTF-8 locale so tmux renders Unicode block glyphs (claude's splash logo)
# correctly. Must also come before the tmux guard.
RUN echo 'export LANG=C.UTF-8' >> /home/dev/.bashrc

# Persistent claude session: interactive SSH logins auto-attach a tmux session
# "main" running claude, so it survives disconnects (reconnect = continue).
RUN printf '%s\n' \
    '' \
    '# --- Persistent claude session (auto-attach tmux on interactive SSH) ---' \
    'if [ -n "$SSH_CONNECTION" ] && [ -z "$TMUX" ] && [ "$TERM_PROGRAM" != "vscode" ] && [ ! -f "$HOME/.no-tmux" ] && command -v tmux >/dev/null 2>&1; then' \
    '    case "$-" in' \
    '        *i*) exec tmux new-session -A -s main -c /workspaces/AREDN-Phonebook "claude --dangerously-skip-permissions; exec bash" ;;' \
    '    esac' \
    'fi' \
    >> /home/dev/.bashrc
