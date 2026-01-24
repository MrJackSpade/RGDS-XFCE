cd ~/source/melonDS
rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-march=armv8.2-a -mcpu=cortex-a55 -mtune=cortex-a55 -O3 -pipe" -DCMAKE_C_FLAGS="-march=armv8.2-a -mcpu=cortex-a55 -mtune=cortex-a55 -O3 -pipe" ..
make -j$(nproc)