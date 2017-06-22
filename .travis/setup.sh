#!/bin/bash

if [[ $TRAVIS_OS_NAME == "osx" ]]; then
    brew install cmake
    brew install openmpi
    brew install gcc --without-multilib
    wget -O conda.sh https://repo.continuum.io/miniconda/Miniconda3-latest-MacOSX-x86_64.sh
else
    if [[ ${TASK} != "pylint" ]]; then
        sudo add-apt-repository ppa:george-edison55/cmake-3.x -y
        sudo apt-get update -q
        sudo apt-get install -y cmake
        sudo apt-get install -y libopenmpi-dev openmpi-bin build-essential
    fi
    wget -O conda.sh https://repo.continuum.io/miniconda/Miniconda3-latest-Linux-x86_64.sh
fi
