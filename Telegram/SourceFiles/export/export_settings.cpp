/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_settings.h"

#include "export/output/export_output_abstract.h"

namespace Export {
namespace {

constexpr auto kMaxFileSize = 2000 * 1024 * 1024;

} // namespace

bool MediaSettings::validate() const {
	if ((types | Type::AllMask) != Type::AllMask) {
		return false;
	} else if (sizeLimit < 0 || sizeLimit > kMaxFileSize) {
		return false;
	}
	return true;
}

bool Settings::validate() const {
	using Format = Output::Format;
	const auto MustBeFull = Type::PersonalChats | Type::BotChats;
	const auto MustNotBeFull = Type::PublicGroups | Type::PublicChannels;
	if ((types | Type::AllMask) != Type::AllMask) {
		return false;
	} else if ((fullChats | Type::AllMask) != Type::AllMask) {
		return false;
	} else if ((fullChats & MustBeFull) != MustBeFull) {
		return false;
	} else if ((fullChats & MustNotBeFull) != 0) {
		return false;
	} else if (format != Format::Html && format != Format::Json) {
		return false;
	} else if (!media.validate()) {
		return false;
	} else if (singlePeerTill > 0 && singlePeerTill <= singlePeerFrom) {
		return false;
	}
	return true;
};

} // namespace Export
