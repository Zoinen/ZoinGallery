# Building ZoinGallery on macOS

This project was built successfully on an Apple Silicon Mac using global Python
tools, Conan 2, ConanCenter Qt, and a locally built QWindowKit.

## Tools

Install the build tools globally:

```sh
pip3 install --break-system-packages conan cmake ninja
```

Verified versions from the working build:

- Conan 2.29.1
- CMake 4.3.4
- Ninja 1.13.0

## Conan Dependencies

The project uses Conan 2 generators in `build/`.

Qt was added through ConanCenter as `qt/6.11.1` with these options:

- `qt/*:shared=True`
- `qt/*:qtdeclarative=True`
- `qt/*:qtsvg=True`
- `qt/*:qtshadertools=True`
- `qt/*:with_pq=False`
- `qt/*:with_odbc=False`

The Postgres and ODBC Qt SQL plugins were disabled because they caused
unrelated optional plugin link failures. ZoinGallery does not need them.

The Conan install command used:

```sh
conan install . --build=missing \
  -s build_type=RelWithDebInfo \
  -s compiler.cppstd=20 \
  -s os.version=13.0 \
  -o '&:build_standalone=True' \
  -c 'tools.cmake.cmaketoolchain:extra_variables={"QT_NO_XCODE_MIN_VERSION_CHECK":"ON"}'
```

`QT_NO_XCODE_MIN_VERSION_CHECK=ON` was needed because the machine had Apple
Command Line Tools active rather than a full Xcode install, and Qt's configure
step otherwise could not determine an Xcode version.

## QWindowKit

QWindowKit was cloned with submodules into the build directory:

```sh
git clone --recursive --branch main https://github.com/stdware/qwindowkit.git build/qwindowkit-src
```

It was configured, built, and installed locally:

```sh
cmake -S build/qwindowkit-src -B build/qwindowkit-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$PWD/build" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/qwindowkit-install" \
  -DQWINDOWKIT_BUILD_QUICK=TRUE \
  -DQWINDOWKIT_BUILD_WIDGETS=FALSE \
  -DQWINDOWKIT_BUILD_EXAMPLES=FALSE \
  -DQWINDOWKIT_BUILD_DOCUMENTATIONS=FALSE

cmake --build build/qwindowkit-build --parallel
cmake --install build/qwindowkit-build
```

The install exports `QWindowKit::Quick`, which is the target used by
ZoinGallery.

## ZoinGallery Configure and Build

Configure the app with Conan and QWindowKit on `CMAKE_PREFIX_PATH`:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DUSE_QWK=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH="$PWD/build;$PWD/build/qwindowkit-install"
```

Build:

```sh
cmake --build build --parallel
```

The resulting app bundle is:

```text
build/bin/RelWithDebInfo/ZoinGallery.app
```

## macOS Notes

The project previously set `CMAKE_OSX_DEPLOYMENT_TARGET` to `10.15`. Qt 6.11.1
itself has a macOS 13.0 deployment ABI and overrides lower targets while it is
built. Keep `os.version=13.0` in the Conan host profile so Qt, image-codec
dependencies, QWindowKit, and ZoinGallery all describe the same real ABI.

## Smoke Test

The built app was launched directly from:

```text
build/bin/RelWithDebInfo/ZoinGallery.app/Contents/MacOS/ZoinGallery
```

It started and stayed running for 5 seconds, then was stopped manually by the
smoke-test command. No stderr output was produced.
