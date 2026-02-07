# LibXenon
[![GitHub Workflow - Docker](https://img.shields.io/github/actions/workflow/status/Free60Project/libxenon/docker.yml?label=docker)](https://github.com/Free60Project/libxenon/actions?query=workflow%3Adocker)
[![GitHub pages - documentation](https://img.shields.io/github/actions/workflow/status/Free60Project/libxenon/doxygen.yml?label=documentation)](https://free60project.github.io/libxenon/)
[![Docker pulls (LibXenon)](https://img.shields.io/docker/pulls/free60/libxenon?label=docker%20pulls%20%28libxenon%29)](https://hub.docker.com/r/free60/libxenon)
[![Docker pulls (Toolchain)](https://img.shields.io/docker/pulls/free60/toolchain?label=docker%20pulls%20%28toolchain%29)](https://hub.docker.com/r/free60/toolchain)

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

Dependencies for Linux distributions using the `dnf` or `apt` package managers will automatically be installed for you. These include:

- flex
- bison
- gcc-multilib (if on x86\_64)
- libgmp3-dev
- libmpfr-dev
- libmpc-dev
- texinfo
- git-core
- build-essential
- wget
- file

If you are not using a Linux system with either the `dnf` or `apt` package manager, ensure the above equivalents are installed before running the main driving script to setup LibXenon, which is named `toolchain/build-xenon-toolchain`. See below on how to use it.

### Prefix

By default the prefix is set to `/usr/local/xenon`. If you want to choose your own prefix, prepend it to the `./build-xenon-toolchain` invocation, i.e. `PREFIX="/home/username/xenon" ./build-xenon-toolchain toolchain`.

### Installing the toolchain

```
./build-xenon-toolchain toolchain
```

### Installing the libxenon library

```
./build-xenon-toolchain libxenon
```

### Installing auxiliary libs (libxenon, bin2s, zlib, libpng, bzip2, freetype, filesystems)

```
./build-xenon-toolchain libs
```

### Environment variables

After installation of the toolchain, the following environment variables need to be populated:

```
DEVKITXENON="/usr/local/xenon"
PATH="${PATH:+${PATH}:}"$DEVKITXENON"/bin:"$DEVKITXENON"/usr/bin"
```
`DEVKITXENON` depends on your chosen installation prefix location. The default value is `/usr/local/xenon` unless you changed it.

You may edit your ~/.bashrc to set these in every shell automatically. Alternatively, you may execute `./build-xenon-toolchain env-cmd` to install a command named `xenon-env`. When you run that command, it will set those variables in a new shell.
