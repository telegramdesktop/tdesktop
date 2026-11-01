/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_share_theme.h"

#include "api/api_share_themes.h"
#include "apiwrap.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"

namespace Info::Profile {
namespace {

using ThemeType = Ui::PeerQrThemeKey::Type;

constexpr auto kMaxEmojiKeys = 6;

[[nodiscard]] QString ErrorText(const MTP::Error &error) {
	return MTP::IgnoreError(error)
		? QString()
		: MTP::IsFloodError(error)
		? tr::lng_flood_error(tr::now)
		: error.type();
}

[[nodiscard]] Ui::PeerQrAppearance BackdropAppearance(
		const Data::UniqueGiftBackdrop &backdrop) {
	return {
		.edge = backdrop.edgeColor,
		.center = backdrop.centerColor,
	};
}

[[nodiscard]] bool IsDefaultBackdrop(
		const Data::UniqueGiftBackdrop &backdrop) {
	return (BackdropAppearance(backdrop) == Ui::PeerQrDefaultAppearance());
}

[[nodiscard]] std::optional<int> DefaultBackdropId(
		const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
	const auto i = ranges::find_if(backdrops, IsDefaultBackdrop);
	return (i != end(backdrops)) ? std::make_optional(i->id) : std::nullopt;
}

[[nodiscard]] Ui::PeerQrSwatch DefaultSwatch() {
	return { .appearance = Ui::PeerQrDefaultAppearance() };
}

// WHY: the default circle sends no backdrop_id, so a served backdrop_id
// naming the default backdrop must read as that circle, or choosing the
// circle again would save.
[[nodiscard]] Ui::PeerQrSwatch BackdropSwatch(
		int id,
		const std::vector<Data::UniqueGiftBackdrop> &backdrops,
		std::optional<int> defaultId) {
	if (defaultId == id) {
		return DefaultSwatch();
	}
	const auto i = ranges::find(
		backdrops,
		id,
		&Data::UniqueGiftBackdrop::id);
	return {
		.key = { .type = ThemeType::Backdrop, .id = id },
		.appearance = ((i != end(backdrops))
			? BackdropAppearance(*i)
			: Ui::PeerQrDefaultAppearance()),
	};
}

[[nodiscard]] std::vector<QString> EmojiKeys(
		const std::vector<Data::ReactionId> &emojis) {
	auto result = std::vector<QString>();
	for (const auto &id : emojis) {
		if (int(result.size()) == kMaxEmojiKeys) {
			break;
		} else if (id.empty() || id.paid()) {
			continue;
		}
		auto key = Data::ReactionEntityData(id);
		if (!ranges::contains(result, key)) {
			result.push_back(std::move(key));
		}
	}
	return result;
}

[[nodiscard]] std::vector<Data::ReactionId> EmojiIds(
		const std::vector<QString> &keys) {
	auto result = std::vector<Data::ReactionId>();
	result.reserve(keys.size());
	for (const auto &key : keys) {
		auto id = Data::ReactionFromEntityData(key);
		if (!id.empty() && !id.paid()) {
			result.push_back(std::move(id));
		}
	}
	return result;
}

[[nodiscard]] Ui::PeerQrSwatch ResolveSwatch(
		const Data::ShareTheme &theme,
		const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
	return v::match(theme, [](const Data::ShareThemeDefault &) {
		return DefaultSwatch();
	}, [&](const Data::ShareThemeEmojiBackdrop &data) {
		auto result = data.backdropId
			? BackdropSwatch(
				*data.backdropId,
				backdrops,
				DefaultBackdropId(backdrops))
			: DefaultSwatch();
		auto keys = EmojiKeys(data.emojis);
		result.key.emoji = keys;
		result.appearance.emoji = std::move(keys);
		return result;
	}, [](const Data::ShareThemeGift &data) {
		return data.gift.unique
			? QrGiftSwatch(*data.gift.unique)
			: DefaultSwatch();
	});
}

[[nodiscard]] std::optional<Data::ShareTheme> ThemeFromKey(
		const Ui::PeerQrThemeKey &key) {
	if (key.type == ThemeType::Gift) {
		return std::nullopt;
	}
	auto result = Data::ShareThemeEmojiBackdrop{
		.emojis = EmojiIds(key.emoji),
	};
	if (key.type == ThemeType::Backdrop) {
		result.backdropId = key.id;
	}
	if (!result.backdropId && result.emojis.empty()) {
		return Data::ShareThemeDefault();
	}
	return result;
}

void SaveShareTheme(
		not_null<PeerData*> peer,
		Data::ShareTheme theme,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	peer->session().api().shareThemes().save(
		peer,
		std::move(theme),
		std::move(done),
		[=](const MTP::Error &error) {
			if (fail) {
				fail(ErrorText(error));
			}
		});
}

} // namespace

auto QrThemeSwatchesValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Ui::PeerQrSwatch>> {
	return session->api().shareThemes().backdropsValue(
	) | rpl::map([](const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
		const auto defaultId = DefaultBackdropId(backdrops);
		auto result = std::vector<Ui::PeerQrSwatch>();
		result.reserve(backdrops.size() + 1);
		if (!defaultId) {
			result.push_back(DefaultSwatch());
		}
		for (const auto &backdrop : backdrops) {
			result.push_back(BackdropSwatch(backdrop.id, backdrops, defaultId));
		}
		return result;
	}) | rpl::distinct_until_changed();
}

Ui::PeerQrSwatch QrGiftSwatch(const Data::UniqueGift &gift) {
	return {
		.key = { .type = ThemeType::Gift, .gift = gift.id },
		.appearance = {
			.edge = gift.backdrop.edgeColor,
			.center = gift.backdrop.centerColor,
			.gift = {
				.id = gift.id,
				.model = Data::SerializeCustomEmojiId(gift.model.document),
				.pattern = Data::SerializeCustomEmojiId(
					gift.pattern.document),
				.patternColor = gift.backdrop.patternColor,
			},
		},
	};
}

rpl::producer<QrServedTheme> QrServedThemeValue(not_null<PeerData*> peer) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			rpl::variable<std::optional<Data::ShareTheme>> theme;
			bool kept = false;
		};
		const auto state = lifetime.make_state<State>();
		const auto session = &peer->session();
		const auto themes = &session->api().shareThemes();
		if (auto kept = themes->kept(peer)) {
			state->kept = true;
			state->theme = std::move(kept);
		} else {
			session->api().requestFullPeer(peer);
		}

		rpl::combine(
			state->theme.value(
			) | rpl::filter([](const std::optional<Data::ShareTheme> &theme) {
				return theme.has_value();
			}),
			themes->backdropsValue()
		) | rpl::map([=](
				const std::optional<Data::ShareTheme> &theme,
				const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
			return QrServedTheme{
				.swatch = ResolveSwatch(*theme, backdrops),
				.kept = state->kept,
			};
		}) | rpl::distinct_until_changed(
		) | rpl::on_next([=](QrServedTheme &&value) {
			consumer.put_next(std::move(value));
		}, lifetime);

		themes->updates(
		) | rpl::filter(
			rpl::mappers::_1 == peer
		) | rpl::on_next([=] {
			state->kept = false;
			state->theme = themes->kept(peer);
		}, lifetime);

		return lifetime;
	};
}

rpl::producer<bool> QrThemeCanSaveValue(not_null<PeerData*> peer) {
	return Api::ShareThemes::CanSaveValue(peer);
}

void SaveQrTheme(
		not_null<PeerData*> peer,
		Ui::PeerQrThemeKey key,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	auto theme = ThemeFromKey(key);
	if (!theme) {
		return;
	}
	SaveShareTheme(
		peer,
		std::move(*theme),
		std::move(done),
		std::move(fail));
}

void SaveQrGiftTheme(
		not_null<PeerData*> peer,
		const Data::ShareThemeGift &theme,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	SaveShareTheme(peer, theme, std::move(done), std::move(fail));
}

} // namespace Info::Profile
