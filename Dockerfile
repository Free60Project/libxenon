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
RUN ./build-xenon-toolchain toolchain || cat build.log
