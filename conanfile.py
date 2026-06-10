from conan import ConanFile
from conan.tools.files import copy
import os

class ZoinGalleryConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    
    requires = [
        "exiv2/0.28.3",
        "libtiff/4.7.0",
        "libraw/0.21.3",
        "libpng/1.6.45",
        "libheif/1.20.1",
    ]

    generators = "CMakeToolchain", "CMakeDeps"

    default_options = {
        "libtiff/*:jpeg": "libjpeg-turbo",
        "libraw/*:shared": True,
        "libraw/*:with_jpeg": "libjpeg-turbo",
        "jasper/*:with_libjpeg": "libjpeg-turbo"
    }

    def requirements(self):
        # Override the version of libjpeg-turbo required by libraw explicitly
        self.requires("libjpeg-turbo/3.0.2", override=True)
        self.requires("jasper/4.2.0", override=True)

    def layout(self):
        self.cpp.build.libdirs = "lib" # write the .libs to the library folder under build
        self.cpp.build.bindirs = "bin" # write the .dll to the bin folder under build

    def generate(self):
        for dep in self.dependencies.values():
            if self.settings.compiler == "apple-clang":
                copy(self, "*.dylib", src=dep.cpp_info.libdirs[0], dst=os.path.join(self.cpp.build.libdirs, str(self.settings.build_type)), keep_path=False)
            elif self.settings.compiler == "gcc":
                copy(self, "*.so", src=dep.cpp_info.libdirs[0], dst=os.path.join(self.cpp.build.libdirs, str(self.settings.build_type)), keep_path=False)
            elif self.settings.compiler == "msvc":
                # copy(self, "*.lib", src=dep.cpp_info.libdirs[0], dst=os.path.join(self.cpp.build.libdirs, ""), keep_path=False)  
                copy(self, "*.dll", src=dep.cpp_info.bindirs[0], dst=os.path.join(self.cpp.build.bindirs, str(self.settings.build_type)),keep_path=False)
