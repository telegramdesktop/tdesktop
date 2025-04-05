/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_top_bar_suggestion.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_birthday.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/text/text_utilities.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Dialogs {
namespace {

[[nodiscard]] Window::SessionController *FindSessionController(
		not_null<Ui::RpWidget*> widget) {
	const auto window = Core::App().findWindow(widget);
	return window ? window->sessionController() : nullptr;
}

constexpr auto kSugSetBirthday = "BIRTHDAY_SETUP"_cs;
constexpr auto kSugPremiumAnnual = "PREMIUM_ANNUAL"_cs;
constexpr auto kSugPremiumUpgrade = "PREMIUM_UPGRADE"_cs;
constexpr auto kSugPremiumRestore = "PREMIUM_RESTORE"_cs;

} // namespace

rpl::producer<Ui::SlideWrap<Ui::RpWidget>*> TopBarSuggestionValue(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			TopBarSuggestionContent *content = nullptr;
			Ui::SlideWrap<Ui::RpWidget> *wrap = nullptr;
			rpl::lifetime birthdayLifetime;
			rpl::lifetime premiumLifetime;
		};
		const auto state = lifetime.make_state<State>();
		const auto ensureWrap = [=] {
			if (!state->content) {
				state->content = Ui::CreateChild<TopBarSuggestionContent>(
					parent);
				rpl::combine(
					parent->widthValue(),
					state->content->desiredHeightValue()
				) | rpl::start_with_next([=](int width, int height) {
					state->content->resize(width, height);
				}, state->content->lifetime());
			}
			if (!state->wrap) {
				state->wrap = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
					parent,
					object_ptr<Ui::RpWidget>::fromRaw(state->content));
				state->wrap->toggle(false, anim::type::instant);
			}
		};

		const auto processCurrentSuggestion = [=](auto repeat) -> void {
			ensureWrap();
			const auto content = state->content;
			const auto wrap = state->wrap;
			using RightIcon = TopBarSuggestionContent::RightIcon;
			const auto config = &session->appConfig();
			auto hide = false;
			if (config->suggestionCurrent(kSugSetBirthday.utf8())
				&& !Data::IsBirthdayToday(session->user()->birthday())) {
				content->setRightIcon(RightIcon::Close);
				content->setClickedCallback([=] {
					const auto controller = FindSessionController(parent);
					if (!controller) {
						return;
					}
					Core::App().openInternalUrl(
						u"internal:edit_birthday"_q,
						QVariant::fromValue(ClickHandlerContext{
							.sessionWindow = base::make_weak(controller),
						}));

					state->birthdayLifetime = Info::Profile::BirthdayValue(
						session->user()
					) | rpl::map(
						Data::IsBirthdayTodayValue
					) | rpl::flatten_latest(
					) | rpl::distinct_until_changed(
					) | rpl::start_with_next([=] {
						repeat(repeat);
					});
				});
				content->setHideCallback([=] {
					config->dismissSuggestion(kSugSetBirthday.utf8());
					repeat(repeat);
				});
				content->setContent(
					tr::lng_dialogs_suggestions_birthday_title(
						tr::now,
						Ui::Text::Bold),
					tr::lng_dialogs_suggestions_birthday_about(
						tr::now,
						TextWithEntities::Simple));
				wrap->toggle(true, anim::type::normal);
			} else if (session->premiumPossible() && !session->premium()) {
				const auto isPremiumAnnual = config->suggestionCurrent(
					kSugPremiumAnnual.utf8());
				const auto isPremiumRestore = !isPremiumAnnual
					&& config->suggestionCurrent(kSugPremiumRestore.utf8());
				const auto isPremiumUpgrade = !isPremiumAnnual
					&& !isPremiumRestore
					&& config->suggestionCurrent(kSugPremiumUpgrade.utf8());
				const auto set = [=](QString discount) {
					constexpr auto kMinus = QChar(0x2212);
					const auto &title = isPremiumAnnual
						? tr::lng_dialogs_suggestions_premium_annual_title
						: isPremiumRestore
						? tr::lng_dialogs_suggestions_premium_restore_title
						: tr::lng_dialogs_suggestions_premium_upgrade_title;
					const auto &description = isPremiumAnnual
						? tr::lng_dialogs_suggestions_premium_annual_about
						: isPremiumRestore
						? tr::lng_dialogs_suggestions_premium_restore_about
						: tr::lng_dialogs_suggestions_premium_upgrade_about;
					content->setContent(
						title(
							tr::now,
							lt_text,
							{ discount.replace(kMinus, QChar()) },
							Ui::Text::Bold),
						description(tr::now, TextWithEntities::Simple));
					content->setClickedCallback([=] {
						const auto controller = FindSessionController(parent);
						if (!controller) {
							return;
						}
						Settings::ShowPremium(controller, "dialogs_hint");
						config->dismissSuggestion(isPremiumAnnual
							? kSugPremiumAnnual.utf8()
							: isPremiumRestore
							? kSugPremiumRestore.utf8()
							: kSugPremiumUpgrade.utf8());
						repeat(repeat);
					});
					wrap->toggle(true, anim::type::normal);
				};
				if (isPremiumAnnual || isPremiumRestore || isPremiumUpgrade) {
					content->setRightIcon(RightIcon::Arrow);
					const auto api = &session->api().premium();
					api->statusTextValue() | rpl::start_with_next([=] {
						for (const auto &o : api->subscriptionOptions()) {
							if (o.months == 12) {
								set(o.discount);
								state->premiumLifetime.destroy();
								return;
							}
						}
					}, state->premiumLifetime);
					api->reload();
				} else {
					hide = true;
				}
			} else {
				hide = true;
			}
			if (hide) {
				wrap->toggle(false, anim::type::normal);
				base::call_delayed(st::slideWrapDuration * 2, wrap, [=] {
					state->content = nullptr;
					state->wrap = nullptr;
					consumer.put_next(nullptr);
				});
			}
		};

		session->appConfig().value() | rpl::start_with_next([=] {
			const auto was = state->wrap;
			processCurrentSuggestion(processCurrentSuggestion);
			if (was != state->wrap) {
				consumer.put_next_copy(state->wrap);
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace Dialogs
