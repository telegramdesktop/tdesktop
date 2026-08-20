# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

option(TDESKTOP_API_TEST "Use test API credentials." OFF)
set(TDESKTOP_API_ID "0" CACHE STRING "Provide 'api_id' for the Telegram API access.")
set(TDESKTOP_API_HASH "" CACHE STRING "Provide 'api_hash' for the Telegram API access.")

if (TDESKTOP_API_TEST)
    set(TDESKTOP_API_ID 17349)
    set(TDESKTOP_API_HASH 344583e45741c457fe1862106095a5eb)
endif()

if (TDESKTOP_API_ID STREQUAL "0" OR TDESKTOP_API_HASH STREQUAL "")
    message(FATAL_ERROR
    " \n"
    " PROVIDE: -D TDESKTOP_API_ID=[API_ID] -D TDESKTOP_API_HASH=[API_HASH]\n"
    " \n"
    " > To build your version of Telegram Desktop you're required to provide\n"
    " > your own 'api_id' and 'api_hash' for the Telegram API access.\n"
    " >\n"
    " > How to obtain your 'api_id' and 'api_hash' is described here:\n"
    " > https://core.telegram.org/api/obtaining_api_id\n"
    " >\n"
    " > If you're building the application not for deployment,\n"
    " > but only for test purposes you can use TEST ONLY credentials,\n"
    " > which are very limited by the Telegram API server:\n"
    " >\n"
    " > api_id: 17349\n"
    " > api_hash: 344583e45741c457fe1862106095a5eb\n"
    " >\n"
    " > Your users will start getting internal server errors on login\n"
    " > if you deploy an app using those 'api_id' and 'api_hash'.\n"
    " ")
endif()

if (DESKTOP_APP_DISABLE_AUTOUPDATE)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_AUTOUPDATE)
endif()

if (DESKTOP_APP_DISABLE_CRASH_REPORTS)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_CRASH_REPORTS)
endif()

if (DESKTOP_APP_USE_PACKAGED)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_USE_PACKAGED)
endif()

if (DESKTOP_APP_SPECIAL_TARGET)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_ALLOW_CLOSED_ALPHA)
endif()

option(DESKTOP_APP_DISABLE_SWIFT6 "Disable local on-device translation (build without Swift 6 on macOS)." OFF)
if (DESKTOP_APP_DISABLE_SWIFT6)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_SWIFT6)
endif()

set(TDESKTOP_UPDATE_CHANNEL "stable" CACHE STRING "Compile-time update channel (stable, beta, canary-public, canary-private).")
set(TDESKTOP_CANARY_COUNTER "0" CACHE STRING "Per-channel canary build counter, required positive for canary channels.")
set(TDESKTOP_CANARY_COMMIT "" CACHE STRING "Short commit hash shown in the canary version string.")
set(TDESKTOP_CANARY_PUBLIC_CHANNEL "" CACHE STRING "Public canary channel username (canary-public builds).")
set(TDESKTOP_CANARY_PRIVATE_CHANNEL_ID "0" CACHE STRING "Private canary channel numeric id (canary-private builds).")
set(TDESKTOP_CANARY_METADATA_MSG_ID "0" CACHE STRING "Fixed metadata message id in the canary channel.")

# CI passes these straight from repository variables, an unset variable
# arrives as an empty string and must mean "not configured", not an
# empty macro body.
foreach(numeric_option
    TDESKTOP_CANARY_COUNTER
    TDESKTOP_CANARY_PRIVATE_CHANNEL_ID
    TDESKTOP_CANARY_METADATA_MSG_ID)
    if (${numeric_option} STREQUAL "")
        set(${numeric_option} 0)
    elseif (NOT ${numeric_option} MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${numeric_option} must be a non-negative integer, got '${${numeric_option}}'.")
    endif()
endforeach()

if (TDESKTOP_UPDATE_CHANNEL STREQUAL "stable")
    set(tdesktop_update_channel_value 0)
elseif (TDESKTOP_UPDATE_CHANNEL STREQUAL "beta")
    set(tdesktop_update_channel_value 1)
elseif (TDESKTOP_UPDATE_CHANNEL STREQUAL "canary-public")
    set(tdesktop_update_channel_value 2)
elseif (TDESKTOP_UPDATE_CHANNEL STREQUAL "canary-private")
    set(tdesktop_update_channel_value 3)
else()
    message(FATAL_ERROR "Bad TDESKTOP_UPDATE_CHANNEL '${TDESKTOP_UPDATE_CHANNEL}'")
endif()

if (tdesktop_update_channel_value GREATER 1)
    if (TDESKTOP_CANARY_COUNTER LESS_EQUAL 0)
        message(FATAL_ERROR "Canary channels require a positive TDESKTOP_CANARY_COUNTER.")
    endif()
elseif (NOT TDESKTOP_CANARY_COUNTER EQUAL 0)
    message(FATAL_ERROR "TDESKTOP_CANARY_COUNTER requires a canary TDESKTOP_UPDATE_CHANNEL.")
endif()

target_compile_definitions(Telegram
PRIVATE
    TDESKTOP_UPDATE_CHANNEL=${tdesktop_update_channel_value}
    TDESKTOP_CANARY_COUNTER=${TDESKTOP_CANARY_COUNTER}
    TDESKTOP_CANARY_PRIVATE_CHANNEL_ID=${TDESKTOP_CANARY_PRIVATE_CHANNEL_ID}
    TDESKTOP_CANARY_METADATA_MSG_ID=${TDESKTOP_CANARY_METADATA_MSG_ID}
)
if (NOT TDESKTOP_CANARY_COMMIT STREQUAL "")
    target_compile_definitions(Telegram PRIVATE TDESKTOP_CANARY_COMMIT=${TDESKTOP_CANARY_COMMIT})
endif()
if (NOT TDESKTOP_CANARY_PUBLIC_CHANNEL STREQUAL "")
    target_compile_definitions(Telegram PRIVATE TDESKTOP_CANARY_PUBLIC_CHANNEL=${TDESKTOP_CANARY_PUBLIC_CHANNEL})
endif()
