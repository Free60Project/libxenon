# LibXenon

LibXenon provides a bare-metal homebrew library for the Xbox 360 gaming console.

## Requirements

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

## Environment variables

After installation of the toolchain, the following environment variables need to be populated.

`DEVKITXENON` is dependencing on your chosen installation prefix location.

```
DEVKITXENON="/usr/local/xenon"
PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin"
```

## Prefix

If you want to choose your own prefix, prepend it to the `./build-xenon-toolchain` invocation.

e.g. `PREFIX=/home/username/xenon ./build-xenon-toolchain toolchain`

## Installing toolchain

```
./build-xenon-toolchain toolchain
```

## Install libxenon library

```
./build-xenon-toolchain libxenon
```

## Install auxiliary libs

```
./build-xenon-toolchain libs
```