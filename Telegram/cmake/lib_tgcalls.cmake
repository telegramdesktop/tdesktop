# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_tgcalls STATIC)

if (WIN32)
    init_target(lib_tgcalls cxx_std_17) # Small amount of patches required here.
elseif (LINUX)
    init_target(lib_tgcalls) # All C++20 on Linux, because otherwise ODR violation.
else()
    init_target(lib_tgcalls cxx_std_14) # Can't use std::optional::value on macOS.
endif()

add_library(tdesktop::lib_tgcalls ALIAS lib_tgcalls)

set(tgcalls_dir ${third_party_loc}/tgcalls)
set(tgcalls_loc ${tgcalls_dir}/tgcalls)

nice_target_sources(lib_tgcalls ${tgcalls_loc}
PRIVATE
    Instance.cpp
    Instance.h
)

nice_target_sources(lib_tgcalls ${tgcalls_loc}
PRIVATE
    AudioDeviceHelper.cpp
    AudioDeviceHelper.h
    CodecSelectHelper.cpp
    CodecSelectHelper.h
    CryptoHelper.cpp
    CryptoHelper.h
    EncryptedConnection.cpp
    EncryptedConnection.h
    FakeAudioDeviceModule.cpp
    FakeAudioDeviceModule.h
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
    SctpDataChannelProviderInterfaceImpl.cpp
    SctpDataChannelProviderInterfaceImpl.h
    StaticThreads.cpp
    StaticThreads.h
    ThreadLocalObject.h
    TurnCustomizerImpl.cpp
    TurnCustomizerImpl.h
    VideoCaptureInterface.cpp
    VideoCaptureInterface.h
    VideoCaptureInterfaceImpl.cpp
    VideoCaptureInterfaceImpl.h
    VideoCapturerInterface.h

    # Desktop capturer
    desktop_capturer/DesktopCaptureSource.h
    desktop_capturer/DesktopCaptureSource.cpp
    desktop_capturer/DesktopCaptureSourceHelper.h
    desktop_capturer/DesktopCaptureSourceHelper.cpp
    desktop_capturer/DesktopCaptureSourceManager.h
    desktop_capturer/DesktopCaptureSourceManager.cpp

    # Group calls
    group/GroupInstanceCustomImpl.cpp
    group/GroupInstanceCustomImpl.h
    group/GroupInstanceImpl.h
    group/GroupJoinPayloadInternal.cpp
    group/GroupJoinPayloadInternal.h
    group/GroupJoinPayload.h
    group/GroupNetworkManager.cpp
    group/GroupNetworkManager.h
    group/StreamingPart.cpp
    group/StreamingPart.h

    platform/PlatformInterface.h

    # Android
    platform/android/AndroidContext.cpp
    platform/android/AndroidContext.h
    platform/android/AndroidInterface.cpp
    platform/android/AndroidInterface.h
    platform/android/VideoCameraCapturer.cpp
    platform/android/VideoCameraCapturer.h
    platform/android/VideoCapturerInterfaceImpl.cpp
    platform/android/VideoCapturerInterfaceImpl.h

    # iOS / macOS
    platform/darwin/DarwinInterface.h
    platform/darwin/DarwinInterface.mm
    platform/darwin/DarwinVideoSource.h
    platform/darwin/DarwinVideoSource.mm
    platform/darwin/DesktopCaptureSourceView.h
    platform/darwin/DesktopCaptureSourceView.mm
    platform/darwin/DesktopSharingCapturer.h
    platform/darwin/DesktopSharingCapturer.mm
    platform/darwin/GLVideoView.h
    platform/darwin/GLVideoView.mm
    platform/darwin/GLVideoViewMac.h
    platform/darwin/GLVideoViewMac.mm
    platform/darwin/objc_video_encoder_factory.h
    platform/darwin/objc_video_encoder_factory.mm
    platform/darwin/TGCMIOCapturer.h
    platform/darwin/TGCMIOCapturer.m
    platform/darwin/TGCMIODevice.h
    platform/darwin/TGCMIODevice.mm
    platform/darwin/TGRTCCVPixelBuffer.h
    platform/darwin/TGRTCCVPixelBuffer.mm
    platform/darwin/TGRTCDefaultVideoDecoderFactory.h
    platform/darwin/TGRTCDefaultVideoDecoderFactory.mm
    platform/darwin/TGRTCDefaultVideoEncoderFactory.h
    platform/darwin/TGRTCDefaultVideoEncoderFactory.mm
    platform/darwin/TGRTCVideoDecoderH264.h
    platform/darwin/TGRTCVideoDecoderH264.mm
    platform/darwin/TGRTCVideoDecoderH265.h
    platform/darwin/TGRTCVideoDecoderH265.mm
    platform/darwin/TGRTCVideoEncoderH264.h
    platform/darwin/TGRTCVideoEncoderH264.mm
    platform/darwin/TGRTCVideoEncoderH265.h
    platform/darwin/TGRTCVideoEncoderH265.mm
    platform/darwin/VideoCameraCapturer.h
    platform/darwin/VideoCameraCapturer.mm
    platform/darwin/VideoCameraCapturerMac.h
    platform/darwin/VideoCameraCapturerMac.mm
    platform/darwin/VideoCapturerInterfaceImpl.h
    platform/darwin/VideoCapturerInterfaceImpl.mm
    platform/darwin/VideoCMIOCapture.h
    platform/darwin/VideoCMIOCapture.mm
    platform/darwin/VideoMetalView.h
    platform/darwin/VideoMetalView.mm
    platform/darwin/VideoMetalViewMac.h
    platform/darwin/VideoMetalViewMac.mm
    
    # POSIX

    # Teleram Desktop
    platform/tdesktop/DesktopInterface.cpp
    platform/tdesktop/DesktopInterface.h
    platform/tdesktop/VideoCapturerInterfaceImpl.cpp
    platform/tdesktop/VideoCapturerInterfaceImpl.h
    platform/tdesktop/VideoCapturerTrackSource.cpp
    platform/tdesktop/VideoCapturerTrackSource.h
    platform/tdesktop/VideoCameraCapturer.cpp
    platform/tdesktop/VideoCameraCapturer.h

    # All
    reference/InstanceImplReference.cpp
    reference/InstanceImplReference.h

    # third-party
    third-party/json11.cpp
    third-party/json11.hpp
)

target_link_libraries(lib_tgcalls
PRIVATE
    desktop-app::external_webrtc
    desktop-app::external_ffmpeg
    desktop-app::external_rnnoise
)

target_compile_definitions(lib_tgcalls
PUBLIC
    TGCALLS_USE_STD_OPTIONAL
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
        platform/darwin/DesktopCaptureSourceView.h
        platform/darwin/DesktopCaptureSourceView.mm
        platform/darwin/GLVideoView.h
        platform/darwin/GLVideoView.mm
        platform/darwin/GLVideoViewMac.h
        platform/darwin/GLVideoViewMac.mm
        platform/darwin/VideoCameraCapturer.h
        platform/darwin/VideoCameraCapturer.mm
        platform/darwin/VideoMetalView.h
        platform/darwin/VideoMetalView.mm
        platform/darwin/VideoMetalViewMac.h
        platform/darwin/VideoMetalViewMac.mm
        platform/tdesktop/DesktopInterface.cpp
        platform/tdesktop/DesktopInterface.h
        platform/tdesktop/VideoCapturerInterfaceImpl.cpp
        platform/tdesktop/VideoCapturerInterfaceImpl.h
        platform/tdesktop/VideoCapturerTrackSource.cpp
        platform/tdesktop/VideoCapturerTrackSource.h
        platform/tdesktop/VideoCameraCapturer.cpp
        platform/tdesktop/VideoCameraCapturer.h
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lib_tgcalls
    PRIVATE
        WEBRTC_LINUX
    )
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(lib_tgcalls
    PRIVATE
        -Wno-deprecated-volatile
        -Wno-ambiguous-reversed-operator
    )
endif()

remove_target_sources(lib_tgcalls ${tgcalls_loc}
    platform/android/AndroidContext.cpp
    platform/android/AndroidContext.h
    platform/android/AndroidInterface.cpp
    platform/android/AndroidInterface.h
    platform/android/VideoCameraCapturer.cpp
    platform/android/VideoCameraCapturer.h
    platform/android/VideoCapturerInterfaceImpl.cpp
    platform/android/VideoCapturerInterfaceImpl.h
    reference/InstanceImplReference.cpp
    reference/InstanceImplReference.h
)

target_include_directories(lib_tgcalls
PUBLIC
    ${tgcalls_dir}
PRIVATE
    ${tgcalls_loc}
)

add_library(lib_tgcalls_legacy STATIC)

if (WIN32)
    init_target(lib_tgcalls_legacy cxx_std_17) # Small amount of patches required here.
elseif (LINUX)
    init_target(lib_tgcalls_legacy) # All C++20 on Linux, because otherwise ODR violation.
else()
    init_target(lib_tgcalls_legacy cxx_std_14) # Can't use std::optional::value on macOS.
endif()

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
