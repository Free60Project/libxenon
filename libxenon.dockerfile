FROM free60/toolchain:latest

RUN apt update && apt install nano && \
  apt -y clean autoclean autoremove && \
  rm -rf /var/lib/{apt,dpkg,cache,log}/

WORKDIR /build
COPY . .

WORKDIR /build/toolchain
RUN echo "[+] Installing libxenon"
RUN ./build-xenon-toolchain libxenon || (cat build.log; exit 1)

RUN echo "[+] Installing dependencies"
RUN ./build-xenon-toolchain libs || (cat build.log; exit 1)