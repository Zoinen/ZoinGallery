cmake_minimum_required(VERSION 3.13)

set (CMAKE_OSX_DEPLOYMENT_TARGET, "10.13")

project(ZoinGallery LANGUAGES C CXX)

option(USE_QWK "Use QWindowKit" ON)

if (USE_QWK)
    add_compile_definitions(__USE_QWK)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Bringing back removed std::auto_ptr for msvc and clang
add_compile_definitions(_HAS_AUTO_PTR_ETC=1)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES -D_LIBCPP_ENABLE_CXX14_REMOVED_FEATURES")


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

find_package(QT NAMES Qt6)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Quick Gui Qml Concurrent Svg QuickControls2 REQUIRED)
find_package(Qt6 COMPONENTS ShaderTools)

if (USE_QWK)
    find_package(QWindowKit REQUIRED)
endif()

set(PROJECT_SOURCES
    main.cpp
    ThumbnailLoader.h
    ThumbnailLoader.cpp
    FileListModel.h
    FileListModel.cpp
    ImageFile.h
    ImageFile.cpp
    QmlImageProvider.h
    QmlImageProvider.cpp
    ViewerController.h
    ViewerController.cpp
    QmlResourcesProvider.h
    QmlResourcesProvider.cpp
    MasonryLayout.h
    MasonryLayout.cpp
    exiv2/Preview.h
    exiv2/Preview.cpp
    MasonryLayoutQuickSearch.h
    MasonryLayoutQuickSearch.cpp
    MainWindow.h
    MainWindow.cpp
    ImageModel.h
    ImageModel.cpp
    DummyQWK.h
    SvgCursor.h
    SvgCursor.cpp
    PersistentCache.h
    PersistentCache.cpp
    DecodeManager.h
    DecodeManager.cpp
    Runners/ImageInfoReadRunner.h
    Runners/ImageInfoReadRunner.cpp
    Runners/ImageReadRunner.h
    Runners/ImageReadRunner.cpp
    Runners/FolderListReadRunner.h
    Runners/FolderListReadRunner.cpp
    Runners/ImageDecodeRunner.h
    Runners/ImageDecodeRunner.cpp
    Runners/PersistentImageCacheRunners.h
    Runners/PersistentImageCacheRunners.cpp
    Runners/PersistentFolderCacheRunners.h
    Runners/PersistentFolderCacheRunners.cpp
)

#if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo OR CMAKE_BUILD_TYPE MATCHES Debug)
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DQT_QML_DEBUG ")
#endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    configure_file(ZoinGallery.rc.cmake ${CMAKE_CURRENT_BINARY_DIR}/ZoinGallery.rc @ONLY)
    set(RESOURCE_FILES ${CMAKE_CURRENT_BINARY_DIR}/ZoinGallery.rc)
endif()

if(APPLE)
    set(ICON_FILENAME ZoinGallery.icns)
    set(ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resources/${ICON_FILENAME})
    set(RESOURCE_FILES ${ICON_PATH})
endif()

add_executable(ZoinGallery WIN32 MACOSX_BUNDLE ${PROJECT_SOURCES} ${RESOURCE_FILES})

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_source_files_properties(qml/Style.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_shaders(ZoinGallery
    BATCHABLE
    NOGLSL
    PREFIX
        "/"
    FILES
        "resources/shader.frag"
        "resources/sphere.frag"
)

qt6_add_qml_module(ZoinGallery
    URI /
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        qml/main.qml
        qml/MasonryMode.qml
        qml/PathControl.qml
        qml/BrickDelegate.qml
        qml/ViewerMode.qml
        qml/RoundCorners.qml
        qml/FlickableZoomable.qml
        qml/SphericViewer.qml
        ZGStyle/Style.qml
        ZGStyle/ScrollBar.qml
        ZGStyle/Slider.qml
        ZGStyle/Button.qml
        ZGStyle/TextField.qml
        ZGStyle/ToolTip.qml
        ZGStyle/Menu.qml
        ZGStyle/MenuItem.qml
        ZGStyle/MenuSeparator.qml
        ZGStyle/TabBar.qml
        ZGStyle/TabButton.qml
    RESOURCES
        ZGStyle/qmldir
        resources/ThumbnailsShadow.png
        resources/ThumbnailsShadow@2x.png
        resources/FileIcon.svg
        resources/FolderIcon.svg
        resources/ImageIcon.svg
        resources/ScrollMode.svg
        resources/ScrollModeDown.svg
        resources/ScrollModeUp.svg
        resources/Logo.svg
        resources/DriveIcon.svg
        resources/Up.svg
        resources/Back.svg
        resources/Forward.svg
        resources/SearchBack.svg
        resources/SearchClose.svg
        resources/SearchNext.svg
        resources/PathSeparator.svg
        resources/ListView.svg
        resources/GridView.svg
        resources/WindowClose.svg
        resources/WindowMaximize.svg
        resources/WindowMinimize.svg
        resources/WindowRestore.svg
        resources/ExifCamera.svg
        resources/ExifDateTime.svg
        resources/ExifImage.svg
        resources/ExifLocation.svg
        resources/ExifShooting.svg
        resources/RecursiveView.svg
        resources/SphereScroll.svg
        resources/SphereScrollIdle.svg
        resources/Sphere.svg
    )

if (USE_QWK)
    target_link_libraries(ZoinGallery PUBLIC QWindowKit::Quick)
endif()

target_link_libraries(ZoinGallery PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Svg Qt${QT_VERSION_MAJOR}::QuickControls2 ${CONAN_LIBS})

if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE ${ICON_FILENAME})
    set_source_files_properties(${ICON_PATH} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(ZoinGallery
        PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist")

    # configure RPATH to find Qt
    find_package(Qt6 ${QT_REQUIRED_VERSION} COMPONENTS Core)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,-rpath,${_qt_bin_dir}/../lib/,-rpath,@loader_path/../Frameworks,-rpath,@executable_path/../,-rpath,@executable_path/../Frameworks"
        )
endif()
