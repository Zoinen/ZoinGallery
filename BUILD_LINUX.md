# Building ZoinGallery on Linux

Tested on Ubuntu 24.04 (including WSL2).

## 1. Install system dependencies

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    dos2unix \
    libtiff-dev \
    libraw-dev \
    libjpeg-turbo8-dev \
    libpng-dev
```

## 2. Install Qt 6.9

Qt is not available at version 6.9 from apt. Use
[aqtinstall](https://github.com/miurahr/aqtinstall) to install it.

```bash
pip install aqtinstall --break-system-packages
aqt install-qt linux desktop 6.9.3 linux_gcc_64 \
    --modules qtquickcontrols2 qtshadertools \
    --outputdir ~/Qt
```

> **WSL2 note:** If HTTPS is blocked in your WSL2 environment, run aqt on
> Windows instead:
> ```powershell
> pip install aqtinstall
> aqt install-qt linux desktop 6.9.3 linux_gcc_64 --modules qtquickcontrols2 qtshadertools --outputdir C:\Qt
> ```
> The Linux Qt build installed this way is accessible from WSL2 at
> `/mnt/c/Qt/6.9.3/gcc_64`.

## 3. Create the build directory and stub cmake files

The project uses `conan_toolchain.cmake` by convention (CMakeLists.txt checks
for it). On Linux with apt packages, create minimal stubs:

```bash
mkdir -p build
```

**`build/conan_toolchain.cmake`:**
```cmake
set(CMAKE_CXX_STANDARD 20 CACHE STRING "")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
```

**`build/librawConfig.cmake`** — lets CMake find libraw via `find_package(libraw REQUIRED)`:
```cmake
if(TARGET libraw::libraw)
  return()
endif()

find_path(LIBRAW_INCLUDE_DIR NAMES libraw/libraw.h
  PATHS /usr/include /usr/local/include
)
find_library(LIBRAW_LIBRARY NAMES raw
  PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib
)

add_library(libraw::libraw SHARED IMPORTED)
set_target_properties(libraw::libraw PROPERTIES
  IMPORTED_LOCATION "${LIBRAW_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBRAW_INCLUDE_DIR}"
)

set(libraw_FOUND TRUE)
```

**`build/libjpeg-turboConfig.cmake`** — same for libjpeg-turbo:
```cmake
if(TARGET libjpeg-turbo::libjpeg-turbo)
  return()
endif()

find_path(LIBJPEGTURBO_INCLUDE_DIR NAMES turbojpeg.h jpeglib.h
  PATHS /usr/include /usr/local/include
)
find_library(LIBJPEGTURBO_LIBRARY NAMES turbojpeg
  PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib
)
find_library(LIBJPEG_LIBRARY NAMES jpeg
  PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib
)

add_library(libjpeg-turbo::libjpeg-turbo SHARED IMPORTED)
set_target_properties(libjpeg-turbo::libjpeg-turbo PROPERTIES
  IMPORTED_LOCATION "${LIBJPEGTURBO_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBJPEGTURBO_INCLUDE_DIR}"
)
if(LIBJPEG_LIBRARY)
  set_target_properties(libjpeg-turbo::libjpeg-turbo PROPERTIES
    INTERFACE_LINK_LIBRARIES "${LIBJPEG_LIBRARY}"
  )
endif()

set(libjpeg-turbo_FOUND TRUE)
```

## 4. Configure and build

Adjust the Qt path to wherever you installed it.

```bash
cmake -B build -S . \
    -G Ninja \
    -DCMAKE_PREFIX_PATH=/mnt/c/Qt/6.9.3/gcc_64 \
    -DUSE_QWK=OFF \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build --parallel
```

The binary is written to `build/bin/ZoinGallery`.

## 5. Run

Qt libraries are not on the system path, so point `LD_LIBRARY_PATH` at them
and pass the QML import path:

```bash
QSG_RHI_BACKEND=opengl \
LD_LIBRARY_PATH=/mnt/c/Qt/6.9.3/gcc_64/lib:$LD_LIBRARY_PATH \
./build/bin/ZoinGallery \
    -I /mnt/c/Qt/6.9.3/gcc_64/qml
```

`QSG_RHI_BACKEND=opengl` is required on WSL2/Mesa — Vulkan is not available
and D3D12 is Windows-only.
