# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_tgcalls STATIC)
init_target(lib_tgcalls) # Can't use std::optional::value on macOS.

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
    ChannelManager.cpp
    ChannelManager.h
    CodecSelectHelper.cpp
    CodecSelectHelper.h
    CryptoHelper.cpp
    CryptoHelper.h
    DirectConnectionChannel.h
    EncryptedConnection.cpp
    EncryptedConnection.h
    FakeAudioDeviceModule.cpp
    FakeAudioDeviceModule.h
    FieldTrialsConfig.cpp
    FieldTrialsConfig.h
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

    utils/gzip.cpp
    utils/gzip.h

    v2/ContentNegotiation.cpp
    v2/ContentNegotiation.h
    v2/DirectNetworkingImpl.cpp
    v2/DirectNetworkingImpl.h
    v2/ExternalSignalingConnection.cpp
    v2/ExternalSignalingConnection.h
    v2/InstanceNetworking.h
    v2/InstanceV2ReferenceImpl.cpp
    v2/InstanceV2ReferenceImpl.h
    v2/InstanceV2Impl.cpp
    v2/InstanceV2Impl.h
    v2/NativeNetworkingImpl.cpp
    v2/NativeNetworkingImpl.h
    v2/RawTcpSocket.cpp
    v2/RawTcpSocket.h
    v2/ReflectorPort.cpp
    v2/ReflectorPort.h
    v2/ReflectorRelayPortFactory.cpp
    v2/ReflectorRelayPortFactory.h
    v2/Signaling.cpp
    v2/Signaling.h
    v2/SignalingConnection.cpp
    v2/SignalingConnection.h
    v2/SignalingEncryption.cpp
    v2/SignalingEncryption.h
    v2/SignalingKcpConnection.cpp
    v2/SignalingKcpConnection.h
    v2/SignalingSctpConnection.cpp
    v2/SignalingSctpConnection.h
    v2/ikcp.cpp
    v2/ikcp.h

    # Desktop capturer
    desktop_capturer/DesktopCaptureSource.h
    desktop_capturer/DesktopCaptureSource.cpp
    desktop_capturer/DesktopCaptureSourceHelper.h
    desktop_capturer/DesktopCaptureSourceHelper.cpp
    desktop_capturer/DesktopCaptureSourceManager.h
    desktop_capturer/DesktopCaptureSourceManager.cpp

    # Group calls
    group/AVIOContextImpl.cpp
    group/AVIOContextImpl.h
    group/AudioStreamingPart.cpp
    group/AudioStreamingPart.h
    group/AudioStreamingPartInternal.cpp
    group/AudioStreamingPartInternal.h
    group/AudioStreamingPartPersistentDecoder.cpp
    group/AudioStreamingPartPersistentDecoder.h
    group/GroupInstanceCustomImpl.cpp
    group/GroupInstanceCustomImpl.h
    group/GroupInstanceImpl.h
    group/GroupJoinPayloadInternal.cpp
    group/GroupJoinPayloadInternal.h
    group/GroupJoinPayload.h
    group/GroupNetworkManager.cpp
    group/GroupNetworkManager.h
    group/StreamingMediaContext.cpp
    group/StreamingMediaContext.h
    group/VideoStreamingPart.cpp
    group/VideoStreamingPart.h

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
    platform/darwin/CustomSimulcastEncoderAdapter.cpp
    platform/darwin/CustomSimulcastEncoderAdapter.h
    platform/darwin/DarwinFFMpeg.h
    platform/darwin/DarwinFFMpeg.mm
    platform/darwin/DarwinInterface.h
    platform/darwin/DarwinInterface.mm
    platform/darwin/DarwinVideoSource.h
    platform/darwin/DarwinVideoSource.mm
    platform/darwin/DesktopSharingCapturer.h
    platform/darwin/DesktopSharingCapturer.mm
    platform/darwin/ExtractCVPixelBuffer.h
    platform/darwin/ExtractCVPixelBuffer.mm
    platform/darwin/h265_nalu_rewriter.cc
    platform/darwin/h265_nalu_rewriter.h
    platform/darwin/objc_video_encoder_factory.h
    platform/darwin/objc_video_encoder_factory.mm
    platform/darwin/objc_video_decoder_factory.h
    platform/darwin/objc_video_decoder_factory.mm
    platform/darwin/RTCCodecSpecificInfoH265+Private.h
    platform/darwin/RTCCodecSpecificInfoH265.h
    platform/darwin/RTCCodecSpecificInfoH265.mm
    platform/darwin/RTCH265ProfileLevelId.h
    platform/darwin/RTCH265ProfileLevelId.mm
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

    # third-party
    third-party/json11.cpp
    third-party/json11.hpp
)

target_link_libraries(lib_tgcalls
PRIVATE
    desktop-app::external_webrtc
    desktop-app::external_ffmpeg
    desktop-app::external_openssl
    desktop-app::external_rnnoise
    desktop-app::external_zlib
)

target_compile_definitions(lib_tgcalls
PUBLIC
    TGCALLS_USE_STD_OPTIONAL
PRIVATE
    WEBRTC_APP_TDESKTOP
    RTC_ENABLE_H265
    RTC_ENABLE_VP9
)

if (APPLE)
    target_compile_options(lib_tgcalls
    PRIVATE
        -fobjc-arc
    )
    remove_target_sources(lib_tgcalls ${tgcalls_loc}
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
endif()

if (NOT MSVC)
    target_compile_options_if_exists(lib_tgcalls
    PRIVATE
        -Wno-deprecated-volatile
        -Wno-ambiguous-reversed-operator
        -Wno-deprecated-declarations
        -Wno-unqualified-std-cast-call
        -Wno-unused-function
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
