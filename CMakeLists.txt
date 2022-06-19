cmake_minimum_required(VERSION 3.13)
project(ZoinGallery LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(_HAS_AUTO_PTR_ETC=1)

# conan
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
  message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
  file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.16.1/conan.cmake"
                "${CMAKE_BINARY_DIR}/conan.cmake"
                EXPECTED_HASH SHA256=396e16d0f5eabdc6a14afddbcfff62a54a7ee75c6da23f32f7a31bc85db23484
                TLS_VERIFY ON)
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_cmake_configure(REQUIRES exiv2/0.27.4 libtiff/4.3.0
                      GENERATORS cmake
                      IMPORTS "bin, *.dll -> ./bin"
                      IMPORTS "bin, *.pdb -> ./bin"
                      OPTIONS
                      libtiff:jpeg=libjpeg-turbo
                      )


conan_cmake_autodetect(settings)

conan_cmake_install(PATH_OR_REFERENCE .
                    BUILD missing
                    REMOTE conancenter
                    SETTINGS ${settings})

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(NO_OUTPUT_DIRS)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Quick Concurrent Svg REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Quick Concurrent Svg REQUIRED)

set(PROJECT_SOURCES
    main.cpp
    ThumbnailLoader.cpp
    FileListModel.cpp
    ImageFile.h
    QmlImageProvider.cpp
    ViewerController.cpp
    ThreadedThumbnailGenerator.cpp
    Unsharp/Unsharp.cpp
    Unsharp/Utilities.h
    QmlResourcesProvider.cpp
    MasonryLayout.cpp
    exiv2/Preview.cpp
    qml.qrc
    ThreadSafeQueue.h
    ThreadSafeQueue.cpp
)

if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo OR CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DQT_QML_DEBUG ")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    configure_file(ZoinGallery.rc.cmake ${CMAKE_CURRENT_BINARY_DIR}/ZoinGallery.rc @ONLY)
    set(RESOURCE_FILES ${CMAKE_CURRENT_BINARY_DIR}/ZoinGallery.rc)
endif()

add_executable(ZoinGallery WIN32 ${PROJECT_SOURCES} ${RESOURCE_FILES})
target_link_libraries(ZoinGallery PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Svg ${CONAN_LIBS})
