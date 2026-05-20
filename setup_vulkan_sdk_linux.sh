#!/bin/bash

EXPECTED_SHA256_CHECKSUM="b65f068ab36263559da49d7cacd7e7b9df23824ca8b68ccc522a2b06f5725df2"
VULKAN_SDK_VERSION="1.4.350.0"
VULKAN_SDK_TARBALL_DOWNLOAD_URL="https://sdk.lunarg.com/sdk/download/${VULKAN_SDK_VERSION}/linux/vulkansdk-linux-x86_64-${VULKAN_SDK_VERSION}.tar.xz"
VULKAN_SDK_TARBALL=$(realpath vulkansdk-linux-x86_64-${VULKAN_SDK_VERSION}.tar.xz)

wget -nc -O $VULKAN_SDK_TARBALL $VULKAN_SDK_TARBALL_DOWNLOAD_URL
COMPUTED_SHA256_CHECKSUM=$(sha256sum "$VULKAN_SDK_TARBALL" | awk '{print $1}')

if [[ "$COMPUTED_SHA256_CHECKSUM" != "$EXPECTED_SHA256_CHECKSUM" ]]; then
    echo "FAILED: Checksum mismatch for $VULKAN_SDK_TARBALL."
    echo "  Expected: $EXPECTED_SHA256_CHECKSUM"
    echo "  Computed: $COMPUTED_SHA256_CHECKSUM"
    exit 1
fi

sudo apt-get install libxcb-xinput0 libxcb-xinerama0 libxcb-cursor-dev -y

pushd ~
rm -rf .vulkan
mkdir .vulkan
pushd .vulkan

tar xf $VULKAN_SDK_TARBALL
VULKAN_RUNTIME_ENV_FILE=$(realpath $VULKAN_SDK_VERSION/setup-env.sh)

source $VULKAN_RUNTIME_ENV_FILE
echo "" >> ~/.bashrc
echo "source $VULKAN_RUNTIME_ENV_FILE" >> ~/.bashrc

popd
popd

vulkaninfo

echo "Vulkan SDK installed!"
echo "Open a new SHELL to load all environment variables."
echo "For uninstalling Vulkan SDK, run 'rm -rf ~/.vulkan' and then remove the line 'source $VULKAN_RUNTIME_ENV_FILE' from '~/.bashrc' file."

