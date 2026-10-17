/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_share_theme.h"

#include "api/api_peer_colors.h"
#include "api/api_share_themes.h"
#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "window/themes/window_theme.h"

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

[[nodiscard]] std::optional<Ui::PeerQrAppearance> ColorAppearance(
		not_null<Main::Session*> session,
		int colorId) {
	if (colorId < 0 || colorId > std::numeric_limits<uint8>::max()) {
		return std::nullopt;
	}
	const auto set = session->api().peerColors().colorProfileFor(
		uint8(colorId));
	if (!set || set->bg.empty()) {
		return std::nullopt;
	}
	return Ui::PeerQrAppearance{
		.edge = set->bg.front(),
		.center = (set->bg.size() > 1) ? set->bg[1] : set->bg.front(),
	};
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

[[nodiscard]] Ui::PeerQrSwatch DefaultSwatch(
		const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
	const auto i = ranges::find_if(backdrops, IsDefaultBackdrop);
	const auto found = (i != end(backdrops));
	return {
		.key = {
			.type = found ? ThemeType::Backdrop : ThemeType::Default,
			.id = found ? i->id : 0,
		},
		.appearance = Ui::PeerQrDefaultAppearance(),
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

[[nodiscard]] std::optional<Data::ShareTheme> EmojiTheme(
		const std::vector<QString> &keys) {
	auto emojis = std::vector<Data::ReactionId>();
	emojis.reserve(keys.size());
	for (const auto &key : keys) {
		auto id = Data::ReactionFromEntityData(key);
		if (!id.empty() && !id.paid()) {
			emojis.push_back(std::move(id));
		}
	}
	if (emojis.empty()) {
		return std::nullopt;
	}
	return Data::ShareThemeEmoji{ std::move(emojis) };
}

[[nodiscard]] Ui::PeerQrSwatch ResolveSwatch(
		not_null<Main::Session*> session,
		const Data::ShareTheme &theme,
		const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
	return v::match(theme, [&](const Data::ShareThemeDefault &) {
		return DefaultSwatch(backdrops);
	}, [&](const Data::ShareThemeColor &data) {
		return Ui::PeerQrSwatch{
			.key = { .type = ThemeType::Color, .id = data.colorId },
			.appearance = ColorAppearance(
				session,
				data.colorId
			).value_or(Ui::PeerQrAppearance()),
		};
	}, [&](const Data::ShareThemeBackdrop &data) {
		const auto i = ranges::find(
			backdrops,
			data.backdropId,
			&Data::UniqueGiftBackdrop::id);
		return Ui::PeerQrSwatch{
			.key = { .type = ThemeType::Backdrop, .id = data.backdropId },
			.appearance = ((i != end(backdrops))
				? BackdropAppearance(*i)
				: Ui::PeerQrAppearance()),
		};
	}, [](const Data::ShareThemeEmoji &data) {
		auto keys = EmojiKeys(data.emojis);
		if (keys.empty()) {
			return Ui::PeerQrSwatch{ .key = { .type = ThemeType::Other } };
		}
		return Ui::PeerQrSwatch{
			.key = { .type = ThemeType::Emoji, .emoji = keys },
			.appearance = { .emoji = std::move(keys) },
		};
	}, [](const auto &) {
		return Ui::PeerQrSwatch{ .key = { .type = ThemeType::Other } };
	});
}

[[nodiscard]] std::optional<Data::ShareTheme> ThemeFromKey(
		const Ui::PeerQrThemeKey &key) {
	switch (key.type) {
	case ThemeType::Default: return Data::ShareThemeDefault();
	case ThemeType::Color: return Data::ShareThemeColor{ key.id };
	case ThemeType::Backdrop: return Data::ShareThemeBackdrop{ key.id };
	case ThemeType::Emoji: return EmojiTheme(key.emoji);
	case ThemeType::Other: return std::nullopt;
	}
	Unexpected("Type in ThemeFromKey.");
}

} // namespace

auto QrThemeSwatchesValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Ui::PeerQrSwatch>> {
	return session->api().shareThemes().backdropsValue(
	) | rpl::map([](const std::vector<Data::UniqueGiftBackdrop> &backdrops) {
		auto result = std::vector<Ui::PeerQrSwatch>();
		result.reserve(backdrops.size() + 1);
		if (ranges::none_of(backdrops, IsDefaultBackdrop)) {
			result.push_back(DefaultSwatch(backdrops));
		}
		for (const auto &backdrop : backdrops) {
			result.push_back({
				.key = { .type = ThemeType::Backdrop, .id = backdrop.id },
				.appearance = BackdropAppearance(backdrop),
			});
		}
		return result;
	}) | rpl::distinct_until_changed();
}

rpl::producer<QrServedTheme> QrServedThemeValue(not_null<PeerData*> peer) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			base::has_weak_ptr guard;
			rpl::variable<std::optional<Data::ShareTheme>> theme;
			bool kept = false;
		};
		const auto state = lifetime.make_state<State>();
		const auto session = &peer->session();
		auto &themes = session->api().shareThemes();
		if (auto kept = themes.kept(peer)) {
			state->kept = true;
			state->theme = std::move(kept);
		}

		rpl::combine(
			state->theme.value(
			) | rpl::filter([](const std::optional<Data::ShareTheme> &theme) {
				return theme.has_value();
			}),
			session->api().peerColors().suggestedProfileValue(),
			themes.backdropsValue(),
			Window::Theme::IsNightModeValue()
		) | rpl::map([=](
				const std::optional<Data::ShareTheme> &theme,
				const std::vector<uint8> &,
				const std::vector<Data::UniqueGiftBackdrop> &backdrops,
				bool) {
			return QrServedTheme{
				.swatch = ResolveSwatch(session, *theme, backdrops),
				.kept = state->kept,
			};
		}) | rpl::distinct_until_changed(
		) | rpl::on_next([=](QrServedTheme &&value) {
			consumer.put_next(std::move(value));
		}, lifetime);

		themes.request(peer, crl::guard(&state->guard, [=](
				const Data::ShareTheme &theme) {
			state->kept = false;
			state->theme.force_assign(theme);
		}));

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
	peer->session().api().shareThemes().save(
		peer,
		std::move(*theme),
		std::move(done),
		[=](const MTP::Error &error) {
			if (fail) {
				fail(ErrorText(error));
			}
		});
}

} // namespace Info::Profile
