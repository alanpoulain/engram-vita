cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(engram_installer C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Engram Installer")
set(VITA_TITLEID "ENGR00001")
set(VITA_VERSION "01.40")

# The engram.suprx the VPK carries: Engram's own build, next door.
set(ENGRAM_SUPRX "${CMAKE_CURRENT_SOURCE_DIR}/../build/engram.suprx" CACHE FILEPATH "engram.suprx to bundle")
if(NOT EXISTS "${ENGRAM_SUPRX}")
  message(FATAL_ERROR "No engram.suprx at ${ENGRAM_SUPRX}: build Engram first, or pass -DENGRAM_SUPRX=<path>")
endif()

# The Engram version this installer carries: what "Check for a new version"
# compares the latest release on GitHub with.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../engram.h" ENGRAM_VERSION_LINE REGEX "#define ENGRAM_VERSION ")
string(REGEX REPLACE ".*\"([0-9.]+)\".*" "\\1" ENGRAM_VERSION "${ENGRAM_VERSION_LINE}")
if(NOT ENGRAM_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
  message(FATAL_ERROR "No ENGRAM_VERSION in ../engram.h")
endif()

# For trying the update on a console before a release is the latest one, and
# never for a release: -DUPDATE_TEST_TAG=1.6 -DUPDATE_TEST_HAVE=1.5 makes an
# installer that says it carries Engram 1.5 and asks GitHub for the 1.6 release
# by its tag, which finds a pre-release too. Its screen says "Update test".
set(UPDATE_TEST_TAG "" CACHE STRING "Test build: the release tag to ask GitHub for")
set(UPDATE_TEST_HAVE "" CACHE STRING "Test build: the Engram version to say it carries")
if(UPDATE_TEST_TAG AND UPDATE_TEST_HAVE)
  set(ENGRAM_VERSION "${UPDATE_TEST_HAVE}")
endif()

add_executable(engram_installer main.c gfx.c liquid.c files.c net.c sha256.c unzip.c update.c)
target_compile_definitions(engram_installer PRIVATE ENGRAM_VERSION="${ENGRAM_VERSION}")
if(UPDATE_TEST_TAG)
  target_compile_definitions(engram_installer PRIVATE UPDATE_TEST_TAG="${UPDATE_TEST_TAG}")
endif()
# -O3: the liquid is drawn on the CPU.
target_compile_options(engram_installer PRIVATE -O3 -Wall)
target_link_libraries(engram_installer
  SceDisplay_stub
  SceCtrl_stub
  SceSysmodule_stub
  ScePower_stub
  ScePvf_stub
  SceIofilemgr_stub
  SceSysmem_stub
  SceLibKernel_stub
  SceKernelThreadMgr_stub
  SceAppMgr_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  m
)

# UNSAFE: the installer writes to ur0:tai and ur0:data.
vita_create_self(eboot.bin engram_installer UNSAFE)
vita_create_vpk(engram_installer.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${ENGRAM_SUPRX} engram.suprx
  # the fonts the app's text is in (SIL Open Font License: the licence goes with them)
  FILE font/Poppins-Regular.ttf font/Poppins-Regular.ttf
  FILE font/OFL-Poppins.txt font/OFL-Poppins.txt
  FILE font/Righteous-Regular.ttf font/Righteous-Regular.ttf
  FILE font/OFL-Righteous.txt font/OFL-Righteous.txt
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
