# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_tgvoip INTERFACE IMPORTED GLOBAL)
add_library(tdesktop::lib_tgvoip ALIAS lib_tgvoip)

if (DESKTOP_APP_USE_PACKAGED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TGVOIP IMPORTED_TARGET tgvoip)

    if (TGVOIP_FOUND)
        target_link_libraries(lib_tgvoip INTERFACE PkgConfig::TGVOIP)
    endif()
endif()

if (NOT TGVOIP_FOUND)
    add_library(lib_tgvoip_bundled STATIC)

    if (WIN32)
        init_target(lib_tgvoip_bundled cxx_std_17) # Small amount of patches required here.
    elseif (LINUX)
        init_target(lib_tgvoip_bundled) # All C++20 on Linux, because otherwise ODR violation.
    else()
        init_target(lib_tgvoip_bundled cxx_std_14) # Can't use std::optional::value on macOS.
    endif()

    option(LIBTGVOIP_DISABLE_ALSA "Disable libtgvoip's ALSA backend (Linux only)." OFF)
    option(LIBTGVOIP_DISABLE_PULSEAUDIO "Disable libtgvoip's PulseAudio backend (Linux only)." OFF)

    set(tgvoip_loc ${third_party_loc}/libtgvoip)

    nice_target_sources(lib_tgvoip_bundled ${tgvoip_loc}
    PRIVATE
        BlockingQueue.cpp
        BlockingQueue.h
        Buffers.cpp
        Buffers.h
        CongestionControl.cpp
        CongestionControl.h
        EchoCanceller.cpp
        EchoCanceller.h
        JitterBuffer.cpp
        JitterBuffer.h
        logging.cpp
        logging.h
        MediaStreamItf.cpp
        MediaStreamItf.h
        OpusDecoder.cpp
        OpusDecoder.h
        OpusEncoder.cpp
        OpusEncoder.h
        threading.h
        VoIPController.cpp
        VoIPGroupController.cpp
        VoIPController.h
        PrivateDefines.h
        VoIPServerConfig.cpp
        VoIPServerConfig.h
        audio/AudioInput.cpp
        audio/AudioInput.h
        audio/AudioOutput.cpp
        audio/AudioOutput.h
        audio/Resampler.cpp
        audio/Resampler.h
        NetworkSocket.cpp
        NetworkSocket.h
        PacketReassembler.cpp
        PacketReassembler.h
        MessageThread.cpp
        MessageThread.h
        audio/AudioIO.cpp
        audio/AudioIO.h
        video/ScreamCongestionController.cpp
        video/ScreamCongestionController.h
        video/VideoSource.cpp
        video/VideoSource.h
        video/VideoRenderer.cpp
        video/VideoRenderer.h
        json11.cpp
        json11.hpp

        # Windows
        os/windows/NetworkSocketWinsock.cpp
        os/windows/NetworkSocketWinsock.h
        os/windows/AudioInputWave.cpp
        os/windows/AudioInputWave.h
        os/windows/AudioOutputWave.cpp
        os/windows/AudioOutputWave.h
        os/windows/AudioOutputWASAPI.cpp
        os/windows/AudioOutputWASAPI.h
        os/windows/AudioInputWASAPI.cpp
        os/windows/AudioInputWASAPI.h
        os/windows/MinGWSupport.h
        os/windows/WindowsSpecific.cpp
        os/windows/WindowsSpecific.h

        # macOS
        os/darwin/AudioInputAudioUnit.cpp
        os/darwin/AudioInputAudioUnit.h
        os/darwin/AudioOutputAudioUnit.cpp
        os/darwin/AudioOutputAudioUnit.h
        os/darwin/AudioInputAudioUnitOSX.cpp
        os/darwin/AudioInputAudioUnitOSX.h
        os/darwin/AudioOutputAudioUnitOSX.cpp
        os/darwin/AudioOutputAudioUnitOSX.h
        os/darwin/AudioUnitIO.cpp
        os/darwin/AudioUnitIO.h
        os/darwin/DarwinSpecific.mm
        os/darwin/DarwinSpecific.h

        # Linux
        os/linux/AudioInputALSA.cpp
        os/linux/AudioInputALSA.h
        os/linux/AudioOutputALSA.cpp
        os/linux/AudioOutputALSA.h
        os/linux/AudioOutputPulse.cpp
        os/linux/AudioOutputPulse.h
        os/linux/AudioInputPulse.cpp
        os/linux/AudioInputPulse.h
        os/linux/AudioPulse.cpp
        os/linux/AudioPulse.h

        # POSIX
        os/posix/NetworkSocketPosix.cpp
        os/posix/NetworkSocketPosix.h
    )

    target_compile_definitions(lib_tgvoip_bundled
    PRIVATE
        TGVOIP_USE_DESKTOP_DSP
    )

    if (WIN32)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(lib_tgvoip_bundled
            PRIVATE
                /wd4005
                /wd4244 # conversion from 'int' to 'float', possible loss of data (several in webrtc)
                /wd5055 # operator '>' deprecated between enumerations and floating-point types
            )
        else()
            target_compile_definitions(lib_tgvoip_bundled
            PUBLIC
                # Doesn't build with mingw for now
                TGVOIP_NO_DSP
            )
        endif()
    elseif (APPLE)
        target_compile_definitions(lib_tgvoip_bundled
        PUBLIC
            TARGET_OS_OSX
            TARGET_OSX
        )
        if (build_macstore)
            target_compile_definitions(lib_tgvoip_bundled
            PUBLIC
                TGVOIP_NO_OSX_PRIVATE_API
            )
        endif()
    else()
        target_compile_options(lib_tgvoip_bundled
        PRIVATE
            -Wno-unknown-pragmas
            -Wno-error=sequence-point
            -Wno-error=unused-result
        )
        if (build_linux32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "i686.*|i386.*|x86.*")
            target_compile_options(lib_tgvoip_bundled PRIVATE -msse2)
        endif()
    endif()

    target_include_directories(lib_tgvoip_bundled
    PUBLIC
        ${tgvoip_loc}
    )
    target_link_libraries(lib_tgvoip_bundled
    PRIVATE
        desktop-app::external_webrtc
        desktop-app::external_opus
    )

    if (LINUX)
        if (NOT LIBTGVOIP_DISABLE_ALSA)
            find_package(ALSA REQUIRED)
            target_include_directories(lib_tgvoip_bundled PRIVATE ${ALSA_INCLUDE_DIRS})
        else()
            remove_target_sources(lib_tgvoip_bundled ${tgvoip_loc}
                os/linux/AudioInputALSA.cpp
                os/linux/AudioInputALSA.h
                os/linux/AudioOutputALSA.cpp
                os/linux/AudioOutputALSA.h
            )

            target_compile_definitions(lib_tgvoip_bundled PRIVATE WITHOUT_ALSA)
        endif()

        if (NOT LIBTGVOIP_DISABLE_PULSEAUDIO)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(PULSE REQUIRED libpulse)
            target_include_directories(lib_tgvoip_bundled PRIVATE ${PULSE_INCLUDE_DIRS})
        else()
            remove_target_sources(lib_tgvoip_bundled ${tgvoip_loc}
                os/linux/AudioOutputPulse.cpp
                os/linux/AudioOutputPulse.h
                os/linux/AudioInputPulse.cpp
                os/linux/AudioInputPulse.h
                os/linux/AudioPulse.cpp
                os/linux/AudioPulse.h
            )

            target_compile_definitions(lib_tgvoip_bundled PRIVATE WITHOUT_PULSE)
        endif()

        target_link_libraries(lib_tgvoip_bundled
        PRIVATE
            ${CMAKE_DL_LIBS}
            pthread
        )
    endif()

    target_link_libraries(lib_tgvoip
    INTERFACE
        lib_tgvoip_bundled
    )
endif()
