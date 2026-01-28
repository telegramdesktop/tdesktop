/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_status_label.h"

#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "base/unixtime.h"

namespace Info::Profile {
namespace {

[[nodiscard]] auto MembersStatusText(int count) {
	return tr::lng_chat_status_members(tr::now, lt_count_decimal, count);
};

[[nodiscard]] auto OnlineStatusText(int count) {
	return tr::lng_chat_status_online(tr::now, lt_count_decimal, count);
};

[[nodiscard]] auto ChatStatusText(
		int fullCount,
		int onlineCount,
		bool isGroup) {
	if (onlineCount > 1 && onlineCount <= fullCount) {
		return tr::lng_chat_status_members_online(
			tr::now,
			lt_members_count,
			MembersStatusText(fullCount),
			lt_online_count,
			OnlineStatusText(onlineCount));
	} else if (fullCount > 0) {
		return isGroup
			? tr::lng_chat_status_members(
				tr::now,
				lt_count_decimal,
				fullCount)
			: tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				fullCount);
	}
	return isGroup
		? tr::lng_group_status(tr::now)
		: tr::lng_channel_status(tr::now);
};

[[nodiscard]] auto ChannelTypeText(not_null<ChannelData*> channel) {
	const auto isPublic = channel->isPublic();
	const auto isMegagroup = channel->isMegagroup();
	return (isPublic
		? (isMegagroup
			? tr::lng_create_public_group_title(tr::now)
			: tr::lng_create_public_channel_title(tr::now))
		: (isMegagroup
			? tr::lng_create_private_group_title(tr::now)
			: tr::lng_create_private_channel_title(tr::now))).toLower();
};

} // namespace

StatusLabel::StatusLabel(
	not_null<Ui::FlatLabel*> label,
	not_null<PeerData*> peer)
: _label(label)
, _peer(peer)
, _refreshTimer([=] { refresh(); }) {
}

void StatusLabel::setOnlineCount(int count) {
	_onlineCount = count;
	refresh();
}

void StatusLabel::refresh() {
	auto hasMembersLink = [&] {
		if (auto megagroup = _peer->asMegagroup()) {
			return megagroup->canViewMembers();
		}
		return false;
	}();
	auto statusText = [&]() -> TextWithEntities {
		using namespace Ui::Text;
		auto currentTime = base::unixtime::now();
		if (auto user = _peer->asUser()) {
			const auto result = Data::OnlineTextFull(user, currentTime);
			const auto showOnline = Data::OnlineTextActive(
				user,
				currentTime);
			const auto updateIn = Data::OnlineChangeTimeout(
				user,
				currentTime);
			if (showOnline) {
				_refreshTimer.callOnce(updateIn);
			}
			return (showOnline && _colorized)
				? Ui::Text::Colorized(result)
				: TextWithEntities{ .text = result };
		} else if (auto chat = _peer->asChat()) {
			if (!chat->amIn()) {
				return tr::lng_chat_status_unaccessible(
					{},
					WithEntities);
			}
			const auto onlineCount = _onlineCount;
			const auto fullCount = std::max(
				chat->count,
				int(chat->participants.size()));
			return { .text = ChatStatusText(
				fullCount,
				onlineCount,
				true) };
		} else if (auto broadcast = _peer->monoforumBroadcast()) {
			if (!broadcast->membersCountKnown()) {
				return TextWithEntities{ .text = ChannelTypeText(broadcast) };
			}
			auto result = ChatStatusText(
				broadcast->membersCount(),
				0,
				false);
			return TextWithEntities{ .text = result };
		} else if (auto channel = _peer->asChannel()) {
			if (!channel->membersCountKnown()) {
				auto result = ChannelTypeText(channel);
				return hasMembersLink
					? tr::link(result)
					: TextWithEntities{ .text = result };
			}
			const auto onlineCount = _onlineCount;
			const auto fullCount = channel->membersCount();
			auto result = ChatStatusText(
				fullCount,
				onlineCount,
				channel->isMegagroup());
			return hasMembersLink
				? tr::link(result)
				: TextWithEntities{ .text = result };
		}
		return tr::lng_chat_status_unaccessible(tr::now, WithEntities);
	}();
	_label->setMarkedText(statusText);
	if (hasMembersLink && _membersLinkCallback) {
		_label->setLink(
			1,
			std::make_shared<LambdaClickHandler>(_membersLinkCallback));
	}
}

void StatusLabel::setMembersLinkCallback(Fn<void()> callback) {
	_membersLinkCallback = std::move(callback);
}

Fn<void()> StatusLabel::membersLinkCallback() const {
	return _membersLinkCallback;
}

void StatusLabel::setColorized(bool enabled) {
	_colorized = enabled;
	refresh();
}

} // namespace Info::Profile
