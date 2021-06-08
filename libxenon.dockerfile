FROM free60/toolchain:latest

WORKDIR /build

COPY libxenon ./libxenon
COPY devkitxenon ./devkitxenon

WORKDIR /build/toolchain
RUN echo "[+] Installing libxenon"
RUN ./build-xenon-toolchain libxenon || (cat build.log; exit 1)

RUN echo "[+] Installing dependencies"
RUN ./build-xenon-toolchain libs || (cat build.log; exit 1)