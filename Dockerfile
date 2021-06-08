FROM ubuntu:latest

ENV TZ=Europe/Berlin
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt update && apt install -y \
  libgmp3-dev \
  libmpfr-dev \
  libmpc-dev \
  texinfo \
  git-core \
  build-essential \
  wget \
  file

WORKDIR /build
COPY . .

WORKDIR /build/toolchain
RUN echo "[+] Installing toolchain"
RUN ./build-xenon-toolchain toolchain || cat build.log

RUN echo "[+] Installing libxenon"
RUN ./build-xenon-toolchain libxenon || cat build.log

RUN echo "[+] Installing dependencies"
RUN ./build-xenon-toolchain libs || cat build.log

RUN echo "[+] Setting environment variables"
RUN echo 'export DEVKITXENON="/usr/local/xenon"' >> /root/.bashrc
RUN echo 'export PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin' >> /root/.bashrc