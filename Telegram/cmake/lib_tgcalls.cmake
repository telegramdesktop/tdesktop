# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_tgcalls STATIC)
init_target(lib_tgcalls cxx_std_14)
add_library(tdesktop::lib_tgcalls ALIAS lib_tgcalls)

set(tgcalls_dir ${third_party_loc}/tgcalls)
set(tgcalls_loc ${tgcalls_dir}/tgcalls)

nice_target_sources(lib_tgcalls ${tgcalls_loc}
PRIVATE
    Manager.cpp
    Manager.h
    MediaManager.cpp
    MediaManager.h
    NetworkManager.cpp
    NetworkManager.h
    Instance.cpp
    Instance.h
    InstanceImpl.cpp
    InstanceImpl.h
    SignalingMessage.cpp
    SignalingMessage.h
    ThreadLocalObject.h
    VideoCaptureInterface.cpp
    VideoCaptureInterface.h
    VideoCaptureInterfaceImpl.cpp
    VideoCaptureInterfaceImpl.h
    VideoCapturerInterface.h

    platform/PlatformInterface.h

    # Windows
    platform/windows/WindowsInterface.cpp
    platform/windows/WindowsInterface.h
    platform/windows/VideoCapturerInterfaceImpl.cpp
    platform/windows/VideoCapturerInterfaceImpl.h

    # iOS / macOS
    platform/darwin/DarwinInterface.h
    platform/darwin/DarwinInterface.mm
    platform/darwin/TGRTCDefaultVideoDecoderFactory.h
    platform/darwin/TGRTCDefaultVideoDecoderFactory.mm
    platform/darwin/TGRTCDefaultVideoEncoderFactory.h
    platform/darwin/TGRTCDefaultVideoEncoderFactory.mm
    platform/darwin/TGRTCVideoDecoderH265.h
    platform/darwin/TGRTCVideoDecoderH265.mm
    platform/darwin/TGRTCVideoEncoderH265.h
    platform/darwin/TGRTCVideoEncoderH265.mm
    platform/darwin/VideoCameraCapturer.h
    platform/darwin/VideoCameraCapturer.mm
    platform/darwin/VideoCapturerInterfaceImpl.h
    platform/darwin/VideoCapturerInterfaceImpl.mm
    platform/darwin/VideoMetalView.h
    platform/darwin/VideoMetalView.mm

    # Linux

    # POSIX
)

if (WIN32)
    target_compile_definitions(lib_tgcalls
    PRIVATE
        TARGET_OS_WIN
    )
endif()

target_include_directories(lib_tgcalls
PUBLIC
    ${tgcalls_dir}
PRIVATE
    ${tgcalls_loc}
)

target_link_libraries(lib_tgcalls
PRIVATE
    desktop-app::external_webrtc
)

add_library(lib_tgcalls_legacy STATIC)
init_target(lib_tgcalls_legacy cxx_std_14)
add_library(tdesktop::lib_tgcalls_legacy ALIAS lib_tgcalls_legacy)

nice_target_sources(lib_tgcalls_legacy ${tgcalls_loc}
PRIVATE
    legacy/InstanceImplLegacy.cpp
    legacy/InstanceImplLegacy.h
)

target_include_directories(lib_tgcalls_legacy
PRIVATE
    ${tgcalls_loc}
)

target_link_libraries(lib_tgcalls_legacy
PRIVATE
    tdesktop::lib_tgcalls
    tdesktop::lib_tgvoip
    desktop-app::external_openssl
)
