FROM free60/toolchain:latest

RUN apt update && apt install nano && \
  apt -y clean autoclean autoremove && \
  rm -rf /var/lib/{apt,dpkg,cache,log}/

WORKDIR /build
COPY . .

WORKDIR /build/toolchain
ARG FREE60_DOCKER_BUILD=true
ENV FREE60_DOCKER_BUILD=$FREE60_DOCKER_BUILD

RUN echo "[+] Installing libxenon"
RUN ./build-xenon-toolchain libxenon || (cat build.log; exit 1)

RUN echo "[+] Installing dependencies"
RUN ./build-xenon-toolchain libs || (cat build.log; exit 1)

CMD ["/bin/bash", "-l"]
