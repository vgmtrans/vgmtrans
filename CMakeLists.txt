####
# vgmtrans
# cmake settings
####
cmake_minimum_required(VERSION 3.5.0)
project(vgmtrans)

############################################
# Compilers and platform-specific settings
####

# We use C++14...
set(CMAKE_CXX_STANDARD 14)

# ...so check for a compiler that implements it
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
  CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
  message(FATAL_ERROR "GCC >= 5.0 required (we've found \
                        ${CMAKE_CXX_COMPILER_VERSION})")

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
  CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.4)
  message(FATAL_ERROR "Clang >= 3.4 required (we've found \
                        ${CMAKE_CXX_COMPILER_VERSION})")
endif()

# Set platform-specific flags
if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/permissive-)
  add_compile_options(/Zc:inline)
  add_compile_options(/Zc:throwingNew)
  add_compile_options(/Zc:strictStrings-)
  add_compile_options(/volatile:iso)
  add_compile_options(/MP)
  
  add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
  add_definitions(-DVGMTRANS_FOR_QT)

  string(APPEND CMAKE_EXE_LINKER_FLAGS " /NXCOMPAT")
else()
  set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -Wtype-limits \
  -Wsign-compare -Wuninitialized")
  set(CMAKE_CXX_FLAGS_RELEASE "-w -Os -fomit-frame-pointer")

  # Stripping on OS X is problematic...
  if(NOT APPLE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -s")
  endif()
endif()

# Enforce 'Release' unless specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type (Release/Debug)" FORCE)
endif()

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "x86_64")
  set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-apple-darwin11")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9")
  set(GUI_TYPE MACOSX_BUNDLE)
endif()

if(WIN32)
  if(MSYS OR MINGW)
    set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-w64-mingw32")
    string(APPEND CMAKE_EXE_LINKER_FLAGS "-static")
  endif()
  set(GUI_TYPE WIN32)
endif()

# IDE support
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
    ${CMAKE_SOURCE_DIR}/cmake)

##################################
# Dependency management
####

file(GLOB_RECURSE MAIN_FILES
  "${PROJECT_SOURCE_DIR}/src/main/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/main/*.h")
file(GLOB UI_FILES
  "${PROJECT_SOURCE_DIR}/src/ui/qt/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/ui/qt/*.h")

set(INCLUDE_DIRS
  ${PROJECT_SOURCE_DIR}/src/main
  ${PROJECT_SOURCE_DIR}/src/main/formats
  ${PROJECT_SOURCE_DIR}/src/main/loaders
  ${PROJECT_SOURCE_DIR}/src/ui/qt
)

source_group("ui" REGULAR_EXPRESSION .*src/ui/qt/.*)
source_group("main\\formats" REGULAR_EXPRESSION .*src/main/formats/.*)
source_group("main\\loaders" REGULAR_EXPRESSION .*src/main/loaders/.*)
source_group("main" REGULAR_EXPRESSION .*src/main/.*)

include_directories(${INCLUDE_DIRS})

find_package(Qt5 REQUIRED COMPONENTS Gui Widgets)
set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES "")
message(STATUS "Will use system Qt ${Qt5Core_VERSION}")

set(CMAKE_AUTOMOC ON)
set(RESOURCE_PATH
  "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/resources.qrc"
  "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/qdarkstyle/style.qrc"
)
qt5_add_resources(UI_RESOURCES ${RESOURCE_PATH})

find_package(ZLIB)
if(ZLIB_FOUND)
  message(STATUS "Will use system zlib ${ZLIB_VERSION_STRING}")
  find_package(MINIZIP)
  if(MINIZIP_FOUND)
    message(STATUS "Will use system minizip")
  else()
    message(STATUS "Will use static minizip")
	add_subdirectory(lib/zlib/minizip)
  endif()
else()
  message(STATUS "Using static zlib and minizip")
  add_subdirectory(lib/zlib)
endif()

message(STATUS "Using static TinyXML")
add_subdirectory(lib/tinyxml)

add_definitions(-DTIXML_USE_STL)

##################################
# Targets
####

add_executable(
  vgmtrans ${GUI_TYPE} ${MAIN_FILES} ${UI_FILES} ${UI_RESOURCES})

target_compile_definitions(vgmtrans
PRIVATE
  -DQT_USE_QSTRINGBUILDER
)

target_include_directories(vgmtrans
PUBLIC
  ${INCLUDE_DIRS}
PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${Qt5Gui_PRIVATE_INCLUDE_DIRS}
)

target_link_libraries(vgmtrans
PRIVATE
  Qt5::Widgets
  minizip
  ZLIB::ZLIB
  tinyxml_static
)

# We can't know for sure if the system minizip
# was compiled with BZip2 support
if(MINIZIP_FOUND)
  find_package(BZip2)
  if(BZIP2_FOUND)
    message(STATUS "Assuming system minizip needs Bzip2")
    target_link_libraries(vgmtrans
      PRIVATE
      BZip2::BZip2
    )  
  endif()
endif()