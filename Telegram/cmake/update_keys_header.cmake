# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Expects: -Droot_pem= -Dmanifest= -Dmanifest_sig= -Doutput=
# Embeds the three files from Resources/update/ verbatim: the manifest
# bytes are signed, so any transformation would break the signature.

function(append_array name file)
    file(READ ${file} content HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")
    string(REGEX REPLACE
        "(0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],)"
        "\\1\n"
        content
        "${content}")
    set(result "${result}inline constexpr unsigned char ${name}[] = {\n${content}\n};\n\n" PARENT_SCOPE)
endfunction()

set(result "// This file was generated from Telegram/Resources/update/,\n// see Telegram/cmake/update_keys_header.cmake.\n#pragma once\n\nnamespace Core::Updates::details {\n\n")
append_array(kRootPublicKeyPem ${root_pem})
append_array(kEmbeddedManifest ${manifest})
append_array(kEmbeddedManifestSig ${manifest_sig})
set(result "${result}} // namespace Core::Updates::details\n")

file(WRITE ${output} "${result}")
