# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_stripe OBJECT)
add_library(desktop-app::lib_stripe ALIAS lib_stripe)
init_target(lib_stripe)

set(stripe_src_loc ${src_loc}/payments)

target_precompile_headers(lib_stripe PRIVATE ${stripe_src_loc}/stripe/stripe_pch.h)
nice_target_sources(lib_stripe ${stripe_src_loc}
PRIVATE
    stripe/stripe_address.h
    stripe/stripe_api_client.cpp
    stripe/stripe_api_client.h
    stripe/stripe_callbacks.h
    stripe/stripe_card.cpp
    stripe/stripe_card.h
    stripe/stripe_card_params.cpp
    stripe/stripe_card_params.h
    stripe/stripe_card_validator.cpp
    stripe/stripe_card_validator.h
    stripe/stripe_decode.cpp
    stripe/stripe_decode.h
    stripe/stripe_error.cpp
    stripe/stripe_error.h
    stripe/stripe_form_encodable.h
    stripe/stripe_form_encoder.cpp
    stripe/stripe_form_encoder.h
    stripe/stripe_payment_configuration.h
    stripe/stripe_token.cpp
    stripe/stripe_token.h

    smartglocal/smartglocal_api_client.cpp
    smartglocal/smartglocal_api_client.h
    smartglocal/smartglocal_callbacks.h
    smartglocal/smartglocal_card.cpp
    smartglocal/smartglocal_card.h
    smartglocal/smartglocal_error.cpp
    smartglocal/smartglocal_error.h
    smartglocal/smartglocal_token.cpp
    smartglocal/smartglocal_token.h
    
    stripe/stripe_pch.h
)

target_include_directories(lib_stripe
PUBLIC
    ${stripe_src_loc}
)

target_link_libraries(lib_stripe
PUBLIC
    desktop-app::lib_crl
    desktop-app::external_qt
)
