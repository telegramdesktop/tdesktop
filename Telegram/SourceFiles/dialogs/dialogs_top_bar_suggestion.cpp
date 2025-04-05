/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_top_bar_suggestion.h"

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

} // namespace

object_ptr<Ui::SlideWrap<Ui::RpWidget>> CreateTopBarSuggestion(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session) {
	const auto content = Ui::CreateChild<TopBarSuggestionContent>(parent);
	auto result = object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>::fromRaw(content));
	const auto wrap = result.data();
	wrap->toggle(false, anim::type::instant);
	struct State {
		rpl::lifetime birthdayLifetime;
	};
	const auto state = content->lifetime().make_state<State>();

	const auto processCurrentSuggestion = [=](auto repeat) -> void {
		if (session->appConfig().suggestionCurrent(kSugSetBirthday.utf8())
			&& !Data::IsBirthdayToday(session->user()->birthday())) {
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
				session->appConfig().dismissSuggestion(
					kSugSetBirthday.utf8());
				repeat(repeat);
			});
			content->setContent(
				tr::lng_dialogs_top_bar_suggestions_birthday_title(
					tr::now,
					Ui::Text::Bold),
				tr::lng_dialogs_top_bar_suggestions_birthday_about(
					tr::now,
					TextWithEntities::Simple));
			wrap->toggle(true, anim::type::normal);
		} else {
			wrap->toggle(false, anim::type::normal);
			base::call_delayed(st::slideWrapDuration * 2, wrap, [=] {
				delete wrap;
			});
		}
	};

	session->appConfig().refreshed() | rpl::start_with_next([=] {
		processCurrentSuggestion(processCurrentSuggestion);
	}, content->lifetime());

	rpl::combine(
		parent->widthValue(),
		content->desiredHeightValue()
	) | rpl::start_with_next([=](int width, int height) {
		content->resize(width, height);
	}, content->lifetime());

	return result;
}

} // namespace Dialogs
