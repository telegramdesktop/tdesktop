/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_share_themes.h"

#include "api/api_credits.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "data/data_wall_paper.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kRefreshEach = 3600 * crl::time(1000);

[[nodiscard]] Data::ShareTheme Parse(
		not_null<Main::Session*> session,
		const MTPShareTheme &theme) {
	using Result = Data::ShareTheme;
	return theme.match([](const MTPDshareThemeDefault &) -> Result {
		return Data::ShareThemeDefault();
	}, [](const MTPDshareThemeEmojiBackdrop &data) -> Result {
		auto result = Data::ShareThemeEmojiBackdrop();
		if (const auto id = data.vbackdrop_id()) {
			result.backdropId = id->v;
		}
		if (const auto list = data.vemojis()) {
			result.emojis.reserve(list->v.size());
			for (const auto &reaction : list->v) {
				result.emojis.push_back(Data::ReactionFromMTP(reaction));
			}
		}
		if (!result.backdropId && result.emojis.empty()) {
			LOG(("API Error: Empty shareThemeEmojiBackdrop."));
			return Data::ShareThemeDefault();
		}
		return result;
	}, [&](const MTPDshareThemeGift &data) -> Result {
		auto gift = FromTL(session, data.vstargift());
		if (!gift || !gift->unique) {
			LOG(("API Error: Not a unique gift in shareThemeGift."));
			return Data::ShareThemeDefault();
		}
		return Data::ShareThemeGift{ .gift = std::move(*gift) };
	}, [](const MTPDinputShareThemeGift &) -> Result {
		LOG(("API Error: inputShareThemeGift in full peer share_theme."));
		return Data::ShareThemeDefault();
	});
}

[[nodiscard]] MTPShareTheme Serialize(const Data::ShareTheme &theme) {
	using Flag = MTPDshareThemeEmojiBackdrop::Flag;
	return v::match(theme, [](const Data::ShareThemeDefault &) {
		return MTP_shareThemeDefault();
	}, [](const Data::ShareThemeEmojiBackdrop &data) {
		auto emojis = QVector<MTPReaction>();
		emojis.reserve(data.emojis.size());
		for (const auto &id : data.emojis) {
			emojis.push_back(Data::ReactionToMTP(id));
		}
		if (!data.backdropId && emojis.isEmpty()) {
			return MTP_shareThemeDefault();
		}
		const auto flags = (data.backdropId ? Flag::f_backdrop_id : Flag())
			| (emojis.isEmpty() ? Flag() : Flag::f_emojis);
		return MTP_shareThemeEmojiBackdrop(
			MTP_flags(flags),
			MTP_int(data.backdropId.value_or(0)),
			MTP_vector<MTPReaction>(std::move(emojis)));
	}, [](const Data::ShareThemeGift &data) {
		Expects(data.savedId || data.gift.unique);

		return MTP_inputShareThemeGift(
			InputSavedStarGiftId(data.savedId, data.gift.unique));
	});
}

} // namespace

ShareThemes::ShareThemes(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

ShareThemes::~ShareThemes() = default;

void ShareThemes::apply(
		not_null<PeerData*> peer,
		const MTPShareTheme *theme) {
	auto parsed = theme
		? Parse(_session, *theme)
		: Data::ShareTheme(Data::ShareThemeDefault());
	const auto i = _kept.find(peer);
	if (i != end(_kept) && i->second == parsed) {
		return;
	}
	_kept[peer] = std::move(parsed);
	_updates.fire_copy(peer);
}

std::optional<Data::ShareTheme> ShareThemes::kept(
		not_null<PeerData*> peer) const {
	const auto i = _kept.find(peer);
	if (i == end(_kept)) {
		return std::nullopt;
	}
	return i->second;
}

rpl::producer<not_null<PeerData*>> ShareThemes::updates() const {
	return _updates.events();
}

bool ShareThemes::CanSave(not_null<PeerData*> peer) {
	if (peer->isSelf()) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
		return !channel->username().isEmpty() && channel->canEditEmoji();
	}
	return false;
}

rpl::producer<bool> ShareThemes::CanSaveValue(not_null<PeerData*> peer) {
	const auto channel = peer->asChannel();
	if (!channel) {
		return rpl::single(CanSave(peer));
	}
	using Flag = Data::PeerUpdate::Flag;
	return rpl::combine(
		Data::PeerFlagValue(channel, ChannelDataFlag::Creator),
		channel->adminRightsValue(),
		channel->session().changes().peerFlagsValue(
			channel,
			Flag::Username | Flag::Usernames)
	) | rpl::map([=] {
		return CanSave(channel);
	}) | rpl::distinct_until_changed();
}

void ShareThemes::save(
		not_null<PeerData*> peer,
		Data::ShareTheme theme,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail) {
	if (!CanSave(peer)) {
		return;
	}
	const auto finish = [=](mtpRequestId requestId) {
		const auto i = _saveRequests.find(peer);
		if (i != end(_saveRequests) && i->second == requestId) {
			_saveRequests.erase(i);
		}
	};
	const auto i = _saveRequests.find(peer);
	const auto after = (i != end(_saveRequests)) ? i->second : mtpRequestId();
	_saveRequests[peer] = _api.request(MTPmessages_SetShareTheme(
		peer->input(),
		Serialize(theme)
	)).done([=](const MTPBool &, mtpRequestId requestId) {
		finish(requestId);
		_kept[peer] = theme;
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		finish(requestId);
		if (fail) {
			fail(error);
		}
	}).afterRequest(after).send();
}

auto ShareThemes::backdropsValue()
-> rpl::producer<std::vector<Data::UniqueGiftBackdrop>> {
	if (!_backdropsRequestedAt
		|| crl::now() - _backdropsRequestedAt >= kRefreshEach) {
		requestBackdrops();
	}
	return _backdrops.value();
}

void ShareThemes::requestBackdrops() {
	if (_backdropsRequestId) {
		return;
	}
	_backdropsRequestedAt = crl::now();
	_backdropsRequestId = _api.request(MTPmessages_GetShareThemeBackdrops(
		MTP_int(_backdropsHash)
	)).done([=](const MTPShareThemeBackdrops &result) {
		_backdropsRequestId = 0;
		result.match([&](const MTPDshareThemeBackdrops &data) {
			_backdropsHash = data.vhash().v;
			applyBackdrops(data);
		}, [](const MTPDshareThemeBackdropsNotModified &) {
		});
	}).fail([=] {
		_backdropsRequestId = 0;
		_backdropsRequestedAt = 0;
	}).send();
}

void ShareThemes::applyBackdrops(const MTPDshareThemeBackdrops &data) {
	const auto &list = data.vbackdrops().v;
	auto backdrops = std::vector<Data::UniqueGiftBackdrop>();
	backdrops.reserve(list.size());
	for (const auto &option : list) {
		const auto &fields = option.data();
		auto backdrop = Data::UniqueGiftBackdrop{
			.id = fields.vbackdrop_id().v,
		};
		backdrop.name = qs(fields.vname());
		backdrop.centerColor = Ui::ColorFromSerialized(
			fields.vcenter_color());
		backdrop.edgeColor = Ui::ColorFromSerialized(
			fields.vedge_color());
		backdrop.patternColor = Ui::ColorFromSerialized(
			fields.vpattern_color());
		backdrop.textColor = Ui::ColorFromSerialized(
			fields.vtext_color());
		backdrops.push_back(std::move(backdrop));
	}
	_backdrops = std::move(backdrops);
}

} // namespace Api
