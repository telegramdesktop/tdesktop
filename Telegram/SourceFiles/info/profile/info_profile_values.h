/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/flags.h"

#include <rpl/producer.h>
#include <rpl/map.h>

struct ChannelLocation;

namespace Data {
class ForumTopic;
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Info::Profile {

inline auto ToSingleLine() {
	return rpl::map([](const QString &text) {
		return TextUtilities::SingleLine(text);
	});
}

rpl::producer<not_null<PeerData*>> MigratedOrMeValue(
	not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<QString> NameValue(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<QString> TitleValue(
	not_null<Data::ForumTopic*> topic);
[[nodiscard]] rpl::producer<DocumentId> IconIdValue(
	not_null<Data::ForumTopic*> topic);
[[nodiscard]] rpl::producer<int32> ColorIdValue(
	not_null<Data::ForumTopic*> topic);
[[nodiscard]] rpl::producer<TextWithEntities> PhoneValue(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<TextWithEntities> PhoneOrHiddenValue(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<TextWithEntities> UsernameValue(
	not_null<UserData*> user,
	bool primary = false);
[[nodiscard]] rpl::producer<std::vector<TextWithEntities>> UsernamesValue(
	not_null<PeerData*> peer);
[[nodiscard]] TextWithEntities AboutWithEntities(
	not_null<PeerData*> peer,
	const QString &value);
[[nodiscard]] rpl::producer<TextWithEntities> AboutValue(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<QString> LinkValue(
	not_null<PeerData*> peer,
	bool primary = false);
[[nodiscard]] rpl::producer<const ChannelLocation*> LocationValue(
	not_null<ChannelData*> channel);
[[nodiscard]] rpl::producer<bool> NotificationsEnabledValue(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<bool> NotificationsEnabledValue(
	not_null<Data::Thread*> thread);
[[nodiscard]] rpl::producer<bool> IsContactValue(not_null<UserData*> user);
[[nodiscard]] rpl::producer<QString> InviteToChatButton(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<QString> InviteToChatAbout(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<bool> CanShareContactValue(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<bool> CanAddContactValue(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<bool> AmInChannelValue(
	not_null<ChannelData*> channel);
[[nodiscard]] rpl::producer<int> MembersCountValue(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<int> PendingRequestsCountValue(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<int> AdminsCountValue(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<int> RestrictionsCountValue(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<int> RestrictedCountValue(
	not_null<ChannelData*> channel);
[[nodiscard]] rpl::producer<int> KickedCountValue(
	not_null<ChannelData*> channel);
[[nodiscard]] rpl::producer<int> SharedMediaCountValue(
	not_null<PeerData*> peer,
	MsgId topicRootId,
	PeerData *migrated,
	Storage::SharedMediaType type);
[[nodiscard]] rpl::producer<int> CommonGroupsCountValue(
	not_null<UserData*> user);
[[nodiscard]] rpl::producer<bool> CanAddMemberValue(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<int> FullReactionsCountValue(
	not_null<Main::Session*> peer);
[[nodiscard]] rpl::producer<bool> CanViewParticipantsValue(
	not_null<ChannelData*> megagroup);

enum class BadgeType;
[[nodiscard]] rpl::producer<BadgeType> BadgeValue(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<DocumentId> EmojiStatusIdValue(
	not_null<PeerData*> peer);

} // namespace Info::Profile
