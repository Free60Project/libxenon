# LibXenon
[![GitHub pages - documentation](https://img.shields.io/github/workflow/status/Free60Project/libxenon/Documentation?label=documentation)](https://free60project.github.io/libxenon/)
[![GitHub Workflow - Docker](https://img.shields.io/github/workflow/status/Free60Project/libxenon/docker?label=docker)](https://github.com/Free60Project/libxenon/actions?query=workflow%3Adocker)
[![Docker pulls (Toolchain)](https://img.shields.io/docker/pulls/free60/toolchain)](https://hub.docker.com/r/free60/toolchain)
[![Docker pulls (LibXenon)](https://img.shields.io/docker/pulls/free60/libxenon)](https://hub.docker.com/r/free60/libxenon)


LibXenon provides a bare-metal homebrew library for the Xbox 360 gaming console.

## Prebuilt images

Check out (https://hub.docker.com/u/free60)

Example usage

```
host $ cd libxenon-homebrew-app/
host $ docker run -it -v $PWD:/app free60/libxenon:latest
docker $ cd /app
docker $ make
```

## Manual build

### Requirements

Dependencies for debian

- flex
- bison
- gcc-multilib
- libgmp3-dev
- libmpfr-dev
- libmpc-dev
- texinfo
- git-core
- build-essential
- wget
- file

### Environment variables

After installation of the toolchain, the following environment variables need to be populated.

`DEVKITXENON` is dependencing on your chosen installation prefix location.

(it's recommended to add the below to your bashrc)

```sh
export DEVKITXENON=/usr/local/xenon
PATH=$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin
```

### Prefix (only needed when DEVKITXENON not set)

If you want to choose your own prefix, prepend it to the `./build-xenon-toolchain` invocation.

e.g. `PREFIX=/home/username/xenon ./build-xenon-toolchain toolchain`

### Installing toolchain

```sh
cd toolchain
./build-xenon-toolchain toolchain # install toolchain
./build-xenon-toolchain libxenon # install libxenon
./build-xenon-toolchain libs # install libs (zlib, bzip, libpng, freetype, sdl, sdl_image, sdl_mixer, sdl_ttf)
```
