# Build system for trikRos2 — a shared library that integrates a ROS 2 node
# into the trikGui process. The library is loaded via QLibrary (same mechanism
# as trikCppProgram .so files) and receives the existing BrickInterface*
# from trikGui, so there is only one process accessing the hardware.

TEMPLATE = lib
CONFIG  += shared c++17
QT      += core xml

include(../global.pri)

# ─── ROS 2 sysroot ────────────────────────────────────────────────────────────
# SDK build:   SDKTARGETSYSROOT is set by sourcing environment-setup-...
# Yocto build: TRIK_ROS2_SYSROOT=${STAGING_DIR_TARGET} is passed via EXTRA_QMAKEVARS_PRE
isEmpty(TRIK_ROS2_SYSROOT): TRIK_ROS2_SYSROOT = $$(SDKTARGETSYSROOT)
isEmpty(TRIK_ROS2_SYSROOT): TRIK_ROS2_SYSROOT = $$(PKG_CONFIG_SYSROOT_DIR)

ROS2_ROOT = $${TRIK_ROS2_SYSROOT}/opt/ros/humble

# ROS 2 headers live in a nonstandard location (/opt/ros/humble/include), so
# the compiler's --sysroot doesn't cover them — add manually. -isystem (not
# -I) also suppresses -Werror inside ROS 2 headers (e.g. libstatistics_collector
# violates -Werror=non-virtual-dtor).
#
# IMPORTANT: include/dds (CycloneDDS) breaks the ament "include/<pkg>/<pkg>/..."
# layout — its headers sit directly under include/dds/*.h and are included as
# "dds/config.h" etc., so the correct -isystem root is $$ROS2_ROOT/include (the
# parent), NOT $$ROS2_ROOT/include/dds. Adding include/dds as its own -isystem
# root makes its features.h shadow glibc's <features.h>, breaking feature-test
# macros and causing cascading "was not declared in this scope" errors.
QMAKE_CXXFLAGS += -isystem $$ROS2_ROOT/include
ROS2_INCLUDE_DIRS = \
    $$system(find $$ROS2_ROOT/include -maxdepth 1 -mindepth 1 -type d 2>/dev/null)
for(dir, ROS2_INCLUDE_DIRS) {
# There may be other libraries besides dds with a single level of nesting.
    !equals(dir, $$ROS2_ROOT/include/dds) {
        QMAKE_CXXFLAGS += -isystem $$dir
    }
}

LIBS += -L$${ROS2_ROOT}/lib

LIBS += \
    -lrclcpp \
    -lrcl \
    -lrcl_yaml_param_parser \
    -lrcutils \
    -lrcpputils \
    -lrosidl_runtime_c \
    -lrosidl_typesupport_cpp \
    -lament_index_cpp \
    -llibstatistics_collector \
    -lstatistics_msgs__rosidl_typesupport_cpp \
    -lrosgraph_msgs__rosidl_typesupport_cpp \
    -lrcl_interfaces__rosidl_typesupport_cpp \
    -lrcl_logging_interface \
    -lrmw_implementation \
    -lrmw \
    -ltracetools \
    -lstd_msgs__rosidl_typesupport_cpp \
    -lsensor_msgs__rosidl_typesupport_cpp \
    -lgeometry_msgs__rosidl_typesupport_cpp \
    -lddsc \
    -lrmw_cyclonedds_cpp

# ─── trikRuntime ──────────────────────────────────────────────────────────────
implementationIncludes(trikKernel trikControl trikNetwork trikScriptRunner)
links(trikKernel trikControl trikNetwork trikScriptRunner)

# ─── Sources ──────────────────────────────────────────────────────────────────
SOURCES += \
    src/ros2Config.cpp \
    src/sensorPublisher.cpp \
    src/encoderPublisher.cpp \
    src/motorSubscriber.cpp \
    src/accelerometerPublisher.cpp \
    src/gyroscopePublisher.cpp \
    src/trikRos2Node.cpp \
    src/ros2Program.cpp \

HEADERS += \
    src/ros2Config.h \
    src/sensorPublisher.h \
    src/encoderPublisher.h \
    src/motorSubscriber.h \
    src/accelerometerPublisher.h \
    src/gyroscopePublisher.h \
    src/trikRos2Node.h \
