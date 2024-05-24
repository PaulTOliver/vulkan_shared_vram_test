#!/bin/bash

# Installs dependencies needed to build and run a basic Vulkan app. Based on
# instructions found at:
# https://vulkan-tutorial.com/Development_environment#page_Linux

set -euo pipefail

echo "Setting up development dependencies:"
sudo apt update
sudo apt install -y \
    libglfw3-dev \
    libglm-dev \
    libvulkan-dev \
    libxi-dev \
    libxxf86vm-dev \
    spirv-tools \
    vulkan-tools \
    vulkan-validationlayers-dev

echo "Pulling GLSLC binary:"
tmp_dir=/tmp/glslc-temp
rm -fr ${tmp_dir}
mkdir -vp ${tmp_dir}
pushd ${tmp_dir}
uri=$(curl -s https://storage.googleapis.com/shaderc/badges/build_link_linux_gcc_release.html | awk -F'url=' '{print $2}' | cut -d\" -f1)
wget ${uri}
tar -xvf install.tgz

install_dir="/usr/local/bin"
echo "Installing GLSLC binary to ${install_dir}:"
sudo cp -v install/bin/glslc ${install_dir}/glslc

echo "Done!"
