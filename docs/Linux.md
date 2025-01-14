# Building & Running on Linux <!-- omit in toc -->

These instructions cover building 32blit on Linux.

- [Prerequisites](#prerequisites)
  - [Building & Running on 32Blit](#building--running-on-32blit)
  - [Building & Running Locally](#building--running-locally)
    - [Build Everything](#build-everything)

# Prerequisites

First install the required tools:

```
sudo apt install git gcc g++ gcc-arm-none-eabi cmake make python3 python3-pip libsdl2-dev libsdl2-image-dev unzip

pip3 install 32blit
```

Optionally, for building the firmware as a .DFU file (usually not needed on Linux):

```
pip3 install construct bitstring
```

## Building & Running on 32Blit

If you want to run code on 32Blit, you should now refer to [Building & Running On 32Blit](32blit.md).

## Building & Running Locally

Set up the 32Blit Makefile from the root of the repository with the following commands:

```shell
mkdir build
cd build
cmake ..
```

Now to make any example, type:

```shell
make example-name
```

For example:

```shell
make raycaster
```

This will produce `examples/raycaster/raycaster` which you should run with:

```shell
./examples/raycaster/raycaster
```

### Build Everything

Alternatively you can build everything by just typing:

```shell
make
```

When the build completes you should be able to run any example.
