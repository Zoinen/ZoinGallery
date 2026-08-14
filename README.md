# ZoinGallery

[![CI](https://github.com/Zoinen/ZoinGallery/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Zoinen/ZoinGallery/actions/workflows/ci.yml?query=branch%3Amaster)

ZoinGallery is a Qt-based desktop image gallery.

## Latest CI Builds

<!-- CI_ARTIFACTS_START -->
| Platform | Version | Built | Download |
| --- | --- | --- | --- |
| <img src=".github/readme/platforms/windows.svg" width="22" alt="Windows logo"> Windows | Windows 10 or later (x64) | 2026-07-19 00:26 UTC | [Download Windows x64 (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8436009263) |
| <img src=".github/readme/platforms/windows.svg" width="22" alt="Windows logo"> Windows | Windows 11 or later (ARM64) | 2026-07-19 00:28 UTC | [Download Windows ARM64 (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8436022877) |
| <img src=".github/readme/platforms/macos.svg" width="22" alt="macOS logo"> macOS | macOS 13 or later | 2026-07-19 00:25 UTC | [Download DMG (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8435998906) |
| <img src=".github/readme/platforms/linux.svg" width="22" alt="Linux logo"> Linux | AppImage x86_64 | 2026-07-19 00:26 UTC | [Download AppImage (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8436005385) |
| <img src=".github/readme/platforms/linux.svg" width="22" alt="Linux logo"> Linux | Flatpak x86_64 | 2026-07-19 00:29 UTC | [Download Flatpak (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8436028480) |
| <img src=".github/readme/platforms/linux.svg" width="22" alt="Linux logo"> Linux | Ubuntu 24.04 | 2026-07-29 20:53 UTC | [Download Ubuntu (build 64)](https://github.com/Zoinen/ZoinGallery/actions/runs/30489832147/artifacts/8739361969) |
| <img src=".github/readme/platforms/linux.svg" width="22" alt="Linux logo"> Linux | Debian 12 | 2026-07-19 00:35 UTC | [Download Debian (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8436084521) |
| <img src=".github/readme/platforms/linux.svg" width="22" alt="Linux logo"> Linux | Fedora latest | 2026-07-19 00:35 UTC | [Download Fedora (build 56)](https://github.com/Zoinen/ZoinGallery/actions/runs/29666768385/artifacts/8436081867) |
| <img src=".github/readme/platforms/linux.svg" width="22" alt="Linux logo"> Linux | Arch rolling | 2026-07-26 22:43 UTC | [Download Arch (build 61)](https://github.com/Zoinen/ZoinGallery/actions/runs/30223157527/artifacts/8637964953) |

_These are unsigned CI validation builds. GitHub may require sign-in to download artifacts._
<!-- CI_ARTIFACTS_END -->

The CI builds are unsigned validation artifacts, not release packages. GitHub may require sign-in to download artifacts, and artifacts expire according to the repository retention policy.

## Building

- [Linux build notes](BUILD_LINUX.md)
- [macOS build notes](BUILD_MACOS.md)

## Reusable QML module

The build also exports a static `ZoinGallery::Core` library and the dynamic
`ZoinGallery 1.0` QML module. `GalleryRuntime::install()` installs the
engine-scoped providers and bounded decode scheduler; independent
`GallerySession` objects then back the windowless `GalleryPanel` and
`GalleryViewer` components. External sessions accept an authoritative catalog
from a host such as f4 and never scan or mutate the filesystem themselves.

To build the embeddable package without the standalone shell:

```sh
conan create . --build=missing \
  -s build_type=RelWithDebInfo -s compiler.cppstd=20 \
  -o '&:build_standalone=False'
```

For the f4-consumed macOS package, also pass `-s os.version=13.0`; this applies
the host's deployment minimum to Qt and every codec dependency, not only to the
ZoinGallery targets.

`ZOIN_BUILD_STANDALONE` remains `ON` for normal repository builds. The package
installs headers, CMake exports, the QML plugin/import tree, compiled shaders,
assets, and codec dependencies as `zoingallery/0.1.0`.

### Collection layouts

`MasonryLayout` is the compatibility name of the module's virtualized
collection renderer. `GalleryPanel.presentationMode` selects one of five
strategies without replacing the model or session: `masonry`, column-major
`columns`, one-row `details`, equal-cell `grid`, and Explorer-style `icons`.
All strategies expose the same cursor, selection, hit-testing, scroll and
viewer-transition API and reuse source-aspect thumbnail tiers from the same
runtime cache.

Delegates are created only for visible rows plus bounded overscan. Pixel
decoding follows that same window (metadata remains catalog-wide for justified
masonry aspect ratios), and the shared runtime LRU defaults to 256 MiB. Build
with `-DZOIN_BUILD_BENCHMARKS=ON` to produce
`ZoinGalleryLayoutInteractionBenchmark`; its `--strict` mode exercises every
strategy with a synthetic 10,000-entry catalog and can save visual QA frames
with `--screenshot-dir`.
