FROM ubuntu:21.04

ENV TZ=Europe/Berlin
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt update && apt install -y \
    flex bison gcc-multilib libgmp3-dev libmpfr-dev libmpc-dev \
    texinfo git-core build-essential wget file && \
  apt -y clean autoclean autoremove && \
  rm -rf /var/lib/{apt,dpkg,cache,log}/

WORKDIR /build
COPY toolchain ./toolchain

WORKDIR /root
RUN echo "[+] Installing toolchain"
RUN (cd /build/toolchain && ./build-xenon-toolchain toolchain && cd / && rm -rf /build) || (cat build.log; exit 1)

RUN echo "[+] Setting environment variables"
ENV DEVKITXENON=/usr/local/xenon
ENV PATH=$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin
