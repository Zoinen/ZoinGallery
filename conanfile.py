from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake
import os

class ZoinGalleryConan(ConanFile):
    name = "zoingallery"
    version = "0.1.0"
    # The distributable artifact contains a dynamic QML module plus a static
    # Core component. Model the aggregate as shared; component metadata below
    # describes the mixed target types precisely.
    package_type = "shared-library"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "*.cpp", "*.h", "*.mm", "*.in",
        "Decoders/*", "Runners/*", "TinyEXIF/*", "Exiftool/*",
        "exiv2/*", "dds_image/*", "qml/*", "ZGStyle/*", "resources/*",
        "tests/*",
        # Root globs are recursive in Conan. Never export an in-tree CMake or
        # Conan build (generated moc/rcc files made earlier revisions depend on
        # whatever happened to be present in the developer's build directory).
        "!build/*", "!build/**",
        # Keep developer/audit CMake trees such as build-standalone-* and
        # build-qwindowkit-* out as well.  The root source globs above are
        # recursive in Conan and otherwise export generated moc/rcc sources
        # and even installed third-party headers from those directories.
        "!build-*/*", "!build-*/**",
        "!cmake-build-*/*", "!cmake-build-*/**",
        "!out/*", "!out/**",
    )
    options = {
        "with_qt": [True, False],
        "with_exiv2": [True, False],
        "build_standalone": [True, False],
    }

    generators = "CMakeToolchain", "CMakeDeps"

    default_options = {
        "with_qt": True,
        "with_exiv2": False,
        # Preserve the repository's historical/default product: consuming or
        # creating the package directly also builds the standalone app. Hosts
        # embedding only the reusable module opt out explicitly.
        "build_standalone": True,
        "qt/*:shared": True,
        "qt/*:qtdeclarative": True,
        "qt/*:qtsvg": True,
        "qt/*:qtshadertools": True,
        "qt/*:with_pq": False,
        "qt/*:with_odbc": False,
        "libtiff/*:jpeg": "libjpeg-turbo",
        "libraw/*:shared": True,
        "libraw/*:with_jpeg": "libjpeg-turbo",
        "libwebp/*:shared": False,
        "jasper/*:with_libjpeg": "libjpeg-turbo"
    }

    def requirements(self):
        # ZoinGalleryCore is a static archive inside a package that also ships a
        # dynamic QML module. Explicit propagation keeps its link dependencies
        # visible to consumers even though the aggregate package is "shared".
        self.requires("libtiff/4.7.0", transitive_libs=True)
        self.requires("libraw/0.21.3", transitive_libs=True)
        self.requires("libpng/1.6.45", transitive_libs=True)
        self.requires("libwebp/1.6.0", transitive_libs=True)
        self.requires("libheif/1.20.1", transitive_libs=True)
        if self.options.with_qt:
            self.requires("qt/6.11.1", transitive_libs=True)
        if self.options.with_exiv2:
            self.requires("exiv2/0.28.3", transitive_libs=True)

        # Override the version of libjpeg-turbo required by libraw explicitly
        self.requires("libjpeg-turbo/3.0.2", transitive_libs=True)
        self.requires("jasper/4.2.0", override=True)

    def validate(self):
        if str(self.settings.os) != "Macos":
            return
        # Qt 6.11 itself has a macOS 13 deployment ABI. A nominal 10.15
        # profile is silently raised by Qt's configure step and leaves Conan
        # metadata disagreeing with the packaged dylibs.
        expected = "13.0"
        actual = str(self.settings.get_safe("os.version"))
        if actual != expected:
            raise ConanInvalidConfiguration(
                f"ZoinGallery build_standalone={self.options.build_standalone} "
                f"requires macOS deployment target {expected}; profile has {actual}. "
                f"Pass -s:h os.version={expected} so ZoinGallery and all dependencies "
                "share one deployment ABI."
            )

    def layout(self):
        # With no explicit Conan output folder, generators and CMake output live
        # in <source>/build.  That same predictable tree is what editable-mode
        # consumers resolve.
        self.folders.source = "."
        self.folders.build = "build"
        self.folders.generators = "build"
        self.cpp.source.includedirs = ["include"]
        self.cpp.build.libdirs = ["."]
        self.cpp.build.bindirs = ["."]
        self.cpp.build.builddirs = [os.path.join("cmake", "ZoinGallery")]
        self.cpp.build.resdirs = ["."]
        for component_name in ("Core", "Qml"):
            component = self.cpp.build.components[component_name]
            component.libdirs = ["."]
            component.bindirs = ["."]
            component.builddirs = [os.path.join("cmake", "ZoinGallery")]
        self.cpp.build.components["Qml"].resdirs = ["."]

    def build(self):
        variables = {
            "ZOIN_BUILD_STANDALONE": bool(self.options.build_standalone),
            "BUILD_TESTING": False,
            "USE_QWK": False,
            "USE_EXIV2": bool(self.options.with_exiv2),
        }
        if str(self.settings.os) == "Macos" and not bool(self.options.build_standalone):
            variables["CMAKE_OSX_DEPLOYMENT_TARGET"] = "13.0"

        cmake = CMake(self)
        cmake.configure(variables=variables)
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "ZoinGallery")
        # Use the CMake package exported by this project. Besides retaining the
        # Qt QML-module metadata needed by deployment tools, it represents the
        # mixed static-Core/shared-QML package accurately (CMakeDeps otherwise
        # applies the aggregate package type to every component).
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = [os.path.join("lib", "cmake", "ZoinGallery")]
        self.cpp_info.resdirs = [os.path.join("lib", "qml")]

        core = self.cpp_info.components["Core"]
        core.type = "static-library"
        core.libs = ["ZoinGalleryCore"]
        core.set_property("cmake_target_name", "ZoinGallery::Core")
        core.includedirs = ["include"]
        core.builddirs = [os.path.join("lib", "cmake", "ZoinGallery")]
        core.requires = [
            "qt::qtCore",
            "qt::qtQuick",
            "qt::qtConcurrent",
            "qt::qtSvg",
            "qt::qtQuickControls2",
            "libtiff::libtiff",
            "libraw::libraw",
            "libpng::libpng",
            "libjpeg-turbo::jpeg",
            "libjpeg-turbo::turbojpeg",
            "libheif::libheif",
            "libwebp::webp",
            "libwebp::webpdemux",
        ]
        if self.options.with_exiv2:
            core.requires.append("exiv2::exiv2lib")

        qml = self.cpp_info.components["Qml"]
        qml.type = "shared-library"
        qml.libs = ["ZoinGalleryQml"]
        qml.set_property("cmake_target_name", "ZoinGallery::Qml")
        qml.builddirs = [os.path.join("lib", "cmake", "ZoinGallery")]
        qml.requires = [
            "Core",
            "qt::qtQml",
            "qt::qtQuick",
            "qt::qtQuickControls2",
        ]
        qml.resdirs = [os.path.join("lib", "qml")]
