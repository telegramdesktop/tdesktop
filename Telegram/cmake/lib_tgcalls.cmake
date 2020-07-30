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
    CodecSelectHelper.cpp
    CodecSelectHelper.h
    CryptoHelper.cpp
    CryptoHelper.h
    EncryptedConnection.cpp
    EncryptedConnection.h
    Instance.cpp
    Instance.h
    InstanceImpl.cpp
    InstanceImpl.h
    LogSinkImpl.cpp
    LogSinkImpl.h
    Manager.cpp
    Manager.h
    MediaManager.cpp
    MediaManager.h
    Message.cpp
    Message.h
    NetworkManager.cpp
    NetworkManager.h
    ThreadLocalObject.h
    VideoCaptureInterface.cpp
    VideoCaptureInterface.h
    VideoCaptureInterfaceImpl.cpp
    VideoCaptureInterfaceImpl.h
    VideoCapturerInterface.h

    platform/PlatformInterface.h

    # Teleram Desktop
    platform/tdesktop/VideoCapturerInterfaceImpl.cpp
    platform/tdesktop/VideoCapturerInterfaceImpl.h
    platform/tdesktop/VideoCapturerTrackSource.cpp
    platform/tdesktop/VideoCapturerTrackSource.h
    platform/tdesktop/VideoCameraCapturer.cpp
    platform/tdesktop/VideoCameraCapturer.h

    # Windows
    platform/windows/WindowsInterface.cpp
    platform/windows/WindowsInterface.h

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
    platform/darwin/VideoCameraCapturerMac.h
    platform/darwin/VideoCameraCapturerMac.mm
    platform/darwin/VideoCapturerInterfaceImpl.h
    platform/darwin/VideoCapturerInterfaceImpl.mm
    platform/darwin/VideoMetalView.h
    platform/darwin/VideoMetalView.mm
    platform/darwin/VideoMetalViewMac.h
    platform/darwin/VideoMetalViewMac.mm

    # Linux

    # POSIX

    reference/InstanceImplReference.cpp
    reference/InstanceImplReference.h
)

target_compile_definitions(lib_tgcalls
PRIVATE
    WEBRTC_APP_TDESKTOP
    RTC_ENABLE_VP9
)

if (WIN32)
    target_compile_definitions(lib_tgcalls
    PRIVATE
        WEBRTC_WIN
    )
elseif (APPLE)
    target_compile_options(lib_tgcalls
    PRIVATE
        -fobjc-arc
    )
    target_compile_definitions(lib_tgcalls
    PRIVATE
        WEBRTC_MAC
    )
    remove_target_sources(lib_tgcalls ${tgcalls_loc}
        platform/darwin/VideoCameraCapturer.h
        platform/darwin/VideoCameraCapturer.mm
        platform/darwin/VideoMetalView.h
        platform/darwin/VideoMetalView.mm
        platform/darwin/VideoMetalViewMac.h
        platform/darwin/VideoMetalViewMac.mm
        platform/tdesktop/VideoCapturerTrackSource.cpp
        platform/tdesktop/VideoCapturerTrackSource.h
        platform/tdesktop/VideoCapturerInterfaceImpl.cpp
        platform/tdesktop/VideoCapturerInterfaceImpl.h
    )
else()
    target_compile_definitions(lib_tgcalls
    PRIVATE
        WEBRTC_LINUX
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
