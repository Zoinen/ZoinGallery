from conan import ConanFile
from conan.tools.files import copy
import os

class ZoinGalleryConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "with_qt": [True, False],
    }

    requires = [
        "exiv2/0.28.3",
        "libtiff/4.7.0",
        "libraw/0.21.3",
        "libpng/1.6.45",
        "libwebp/1.6.0",
        "libheif/1.20.1",
    ]

    generators = "CMakeToolchain", "CMakeDeps"

    default_options = {
        "with_qt": True,
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
        if self.options.with_qt:
            self.requires("qt/6.11.1")

        # Override the version of libjpeg-turbo required by libraw explicitly
        self.requires("libjpeg-turbo/3.0.2", override=True)
        self.requires("jasper/4.2.0", override=True)

    def layout(self):
        self.cpp.build.libdirs = "lib" # write the .libs to the library folder under build
        self.cpp.build.bindirs = "bin" # write the .dll to the bin folder under build

    def generate(self):
        for dep in self.dependencies.values():
            if self.settings.compiler == "apple-clang":
                for libdir in dep.cpp_info.libdirs:
                    copy(self, "*.dylib", src=libdir, dst=os.path.join(self.cpp.build.libdirs, str(self.settings.build_type)), keep_path=False)
            elif self.settings.compiler == "gcc":
                for libdir in dep.cpp_info.libdirs:
                    copy(self, "*.so", src=libdir, dst=os.path.join(self.cpp.build.libdirs, str(self.settings.build_type)), keep_path=False)
            elif self.settings.compiler == "msvc":
                # copy(self, "*.lib", src=dep.cpp_info.libdirs[0], dst=os.path.join(self.cpp.build.libdirs, ""), keep_path=False)
                for bindir in dep.cpp_info.bindirs:
                    copy(self, "*.dll", src=bindir, dst=os.path.join(self.cpp.build.bindirs, str(self.settings.build_type)),keep_path=False)
