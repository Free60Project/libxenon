FROM ubuntu:latest

ENV TZ=Europe/Berlin
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt update && apt install -y \
  flex \
  bison \
  gcc-multilib \
  libgmp3-dev \
  libmpfr-dev \
  libmpc-dev \
  texinfo \
  git-core \
  build-essential \
  wget \
  file

WORKDIR /build
COPY toolchain ./toolchain

WORKDIR /build/toolchain
RUN echo "[+] Installing toolchain"
RUN ./build-xenon-toolchain toolchain || (cat build.log; exit 1)

RUN echo "[+] Setting environment variables"
RUN echo 'export DEVKITXENON="/usr/local/xenon"' >> /root/.bashrc
RUN echo 'export PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin' >> /root/.bashrc