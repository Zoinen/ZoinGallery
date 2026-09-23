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

External masonry catalogs commit newly read image dimensions by complete visual
row, including the final partial row. Future row boundaries are calculated with
the real dimensions and one resolved lookahead entry, not placeholder widths.
Completion order does not matter, and an
unreadable image settles with placeholder geometry after retries are exhausted.
The optional `metadataSettled` role passes through `GalleryCatalogModel`; older
catalogs without that role retain their existing behavior. Cached dimension
batches still apply atomically without waiting for uncached rows. Opt-in
`F4_MEDIA_TIMING_TRACE` includes `qt.gallery.masonry.row_metadata_commit` events.

In natural-size masonry, a decoded or cached thumbnail becomes visible only
after its row geometry is committed. The publication gate also covers reused
delegates and cached folder reentry, including settled metadata failures whose
geometry stays square. Grid, Icons, Details, Columns, and the fixed-geometry
sparse-catalog placeholder view continue publishing thumbnails immediately.

For visual diagnostics, launch with `F4_GALLERY_ROW_DELAY_MS=1000` to apply at
most one ready masonry row per second, including cached metadata on reentry.
This uses nonblocking timers; leave the variable unset for normal performance.

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

### Host-managed viewer presentation

An embedding host can keep one `GalleryViewer` and its session mounted while
changing its rectangle by setting `managedPresentation: true`. This bypasses
standalone opening/closing resets and emits `presentationCloseRequested` for
the host to handle. Animate position and dimensions, rather than scaling a
wrapper containing viewer controls. Fit mode follows the viewport; custom
zoom keeps its absolute level and bounded image center.

`previewEntryId` temporarily presents a catalog entry without changing the
session cursor. Clear it to resume cursor-following presentation. Navigation
still uses the session's normal authority/acknowledgement flow. An optional
`hostKeyHandler(event)` can consume embedding-specific shortcuts before the
normal viewer shortcuts run. These options do not change standalone defaults.

### Viewer resampling

The image viewer reduces images with a scale-aware BC cubic filter
(`B=-0.4`, `C=0.8`).
`ViewerResampler` prepares CPU fit frames with normalized separable weights;
`ViewerResample.qml` uses the same kernel in a GPU shader. Both reduce through
floor-rounded half-size levels on an exact 2:1 sampling grid before filtering
the remaining scale. A one-pixel axis stays one pixel. The GPU
keeps generated levels for the current source during pan and zoom, and selects
the smallest level that still covers the physical output size. Sampling is
based on physical pixel derivatives, including the window DPR. The native
`ViewerResampleEffect` material supplies the actual render-target viewport to
the vertex shader. At rest, that shader aligns the projected image's center and
pixel count, including quarter turns, so fractional-DPR window-size rounding
cannot add a second interpolation. Oversized axes preserve the viewer's pan
origin when correcting that rounding, including leading/trailing clipping and
large negative offsets. If the selected texture matches the output
pixel count, it fetches exact texels; this also covers half/quarter pyramid
levels and prepared fit frames. During pan, zoom and transitions the geometry
and sampling stay continuous. Magnification uses a separate interpolating
Catmull-Rom cubic (`B=0`, `C=0.5`) over 4x4 source texels. Its negative weights
retain fine-detail contrast and can produce ringing around strong edges.
Each texel is decoded to linear light before filtering, with premultiplied
alpha preserved and the result bounded before encoding. Hardware bilinear
sampling is not used to combine encoded texels for magnification. There is no
automatic unsharp mask.

Viewer requests ask decoders for native pixels before preparing a fit frame,
avoiding scaled-decode shortcuts with a different kernel. Embedded previews
remain a fallback when the full image cannot be decoded. Gallery
thumbnails retain their existing decode and rendering paths. Fit-derived cache
keys include the filter version and use lossless WebP to preserve filtered
pixels and dither. Thumbnail cache compression is unchanged. Returning from
native zoom to fit selects a
ready fit frame consistently; transient crops and navigation neighbors use the
same viewer shader. Entering native zoom schedules the selected full-size frame
immediately; the fit-first dwell remains in place for neighboring native
prefetch, so a blocked neighbor cannot leave the current 100% image stretched
from its fit preview.

Filtering decodes the sRGB transfer function before combining samples and
encodes it again after each stage. Intermediate levels use RGBA8 with ordered
dither. Existing ICC profile conversion and tagging are unchanged. Negative
cubic lobes are bounded before compositing. The kernel balances sharpness and
suppression of aliasing; its finite support does not remove every above-Nyquist
pattern.

Numerical tests cover constant colors, transparency, periodic patterns, native
decode, and large reductions. QML state tests cover source selection, pyramid
reuse, interaction continuity, and settled physical-pixel geometry at 175%
scale. Offscreen GPU texture readbacks verify exact native texels, fractional
motion sampling, linear-light averaging, transparency, and CPU/GPU agreement at
23%, 25%, odd source dimensions, and 175% scale. Full-viewer regressions use a
3840-by-2076 framebuffer at 175% scale, whose rounded logical extent does not
map exactly to its physical size. They verify every image pixel, the boundary,
quarter turns, and the return from continuous motion to exact sampling.
Magnification tests check analytic fine-line contrast at 200%, an independent
cubic Hermite reference at fractional zoom levels, transparent edges, single-row
and single-column images, and magnified motion and clipping at 175% display scale.
These checks compare pixel
values without screenshots or human visual inspection; interactive image
quality and GPU performance still require a separate visual check.

### Collection layouts

`MasonryLayout` is the compatibility name of the module's virtualized
collection renderer. `GalleryPanel.presentationMode` selects one of five
strategies without replacing the model or session: `masonry`, column-major
`columns`, one-row `details`, equal-cell `grid`, and Explorer-style `icons`.
All strategies expose the same cursor, selection, hit-testing, scroll and
viewer-transition API and reuse source-aspect thumbnail tiers from the same
runtime cache.

`GallerySession.thumbnailsEnabled` controls panel-preview work independently
per session (default `true`). Disabling it hides cached image/video/folder
previews, rejects or cancels panel-owned metadata, probes, decodes and folder
preview demand, and makes image dimensions irrelevant to panel geometry.
Caches and folder snapshots are retained for reuse when re-enabled. Requests
owned by a viewer or another session are not canceled by this policy; full
image viewing is unaffected.

Delegates are created only for visible rows plus bounded overscan. Pixel
decoding follows that same window (metadata remains catalog-wide for justified
masonry aspect ratios), and the shared runtime LRU defaults to 256 MiB. Build
with `-DZOIN_BUILD_BENCHMARKS=ON` to produce
`ZoinGalleryLayoutInteractionBenchmark`; its `--strict` mode exercises every
strategy with a synthetic 10,000-entry catalog and can save visual QA frames
with `--screenshot-dir`.
