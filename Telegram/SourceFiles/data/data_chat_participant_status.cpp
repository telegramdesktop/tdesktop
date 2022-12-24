/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat_participant_status.h"

#include "boxes/peers/edit_peer_permissions_box.h"

namespace {

[[nodiscard]] ChatAdminRights ChatAdminRightsFlags(
		const MTPChatAdminRights &rights) {
	return rights.match([](const MTPDchatAdminRights &data) {
		return ChatAdminRights::from_raw(int32(data.vflags().v));
	});
}

[[nodiscard]] ChatRestrictions ChatBannedRightsFlags(
		const MTPChatBannedRights &rights) {
	return rights.match([](const MTPDchatBannedRights &data) {
		return ChatRestrictions::from_raw(int32(data.vflags().v));
	});
}

[[nodiscard]] TimeId ChatBannedRightsUntilDate(
		const MTPChatBannedRights &rights) {
	return rights.match([](const MTPDchatBannedRights &data) {
		return data.vuntil_date().v;
	});
}

} // namespace

ChatAdminRightsInfo::ChatAdminRightsInfo(const MTPChatAdminRights &rights)
: flags(ChatAdminRightsFlags(rights)) {
}

ChatRestrictionsInfo::ChatRestrictionsInfo(const MTPChatBannedRights &rights)
: flags(ChatBannedRightsFlags(rights))
, until(ChatBannedRightsUntilDate(rights)) {
}

namespace Data {

std::vector<ChatRestrictions> ListOfRestrictions(
		RestrictionsSetOptions options) {
	auto labels = RestrictionLabels(options);
	return ranges::views::all(labels)
		| ranges::views::transform(&RestrictionLabel::flags)
		| ranges::to_vector;
}

} // namespace Data
