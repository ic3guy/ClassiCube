FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc make libc6-dev \
      libx11-dev libxi-dev libegl1-mesa-dev libgles2-mesa-dev \
      libgl1-mesa-dri libegl1 libgles2 libglx-mesa0 \
      xvfb x11-apps imagemagick xdotool ca-certificates curl unzip \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
