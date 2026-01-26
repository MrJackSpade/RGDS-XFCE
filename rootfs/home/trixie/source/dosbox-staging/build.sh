export CFLAGS="-mcpu=cortex-a55 -mtune=cortex-a55 -O3 -pipe -fomit-frame-pointer"
export CXXFLAGS="${CFLAGS}"
export LDFLAGS="-Wl,-O1 -Wl,--as-needed"

cmake --preset=release-linux \
  -DCMAKE_C_FLAGS="${CFLAGS}" \
  -DCMAKE_CXX_FLAGS="${CXXFLAGS}" \
  -DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS}" \
  -DBUILD_TESTING=OFF \
  -DOPT_TESTS=OFF \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON

cmake --build --preset=release-linux -j4
