/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/components/promo_suggestions.h"
#include "data/data_birthday.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"

namespace Dialogs::TopBarSuggestions {
namespace {

constexpr auto kSugSetBirthday = "BIRTHDAY_SETUP"_cs;

bool Available(const Context &context) {
	const auto session = context.session.get();
	return session->promoSuggestions().current(kSugSetBirthday.utf8())
		&& !Data::IsBirthdayToday(session->user()->birthday());
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto content = args.context.ensureContent();
	const auto findController = args.context.findController;
	const auto recompute = args.recompute;
	const auto lifetime = args.lifetime;
	using RightIcon = TopBarSuggestionContent::RightIcon;
	content->setRightIcon(RightIcon::Close);
	content->setLeadingWidget(nullptr);
	content->setClickedCallback([=] {
		const auto controller = findController();
		Core::App().openInternalUrl(
			u"internal:edit_birthday:add_privacy"_q,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));

		Info::Profile::BirthdayValue(
			session->user()
		) | rpl::map(
			Data::IsBirthdayTodayValue
		) | rpl::flatten_latest(
		) | rpl::distinct_until_changed(
		) | rpl::on_next([=] {
			recompute();
		}, *lifetime);
	});
	content->setHideCallback([=] {
		promo->dismiss(kSugSetBirthday.utf8());
		recompute();
	});
	content->setContent(
		tr::lng_dialogs_suggestions_birthday_title(
			tr::now,
			tr::bold),
		tr::lng_dialogs_suggestions_birthday_about(
			tr::now,
			TextWithEntities::Simple));
	args.done(content, [content] { content->prepareCollapseSnapshot(); });
}

} // namespace

Spec MakeBirthdaySetupSpec() {
	return {
		.priority = Priority::BirthdaySetup,
		.available = Available,
		.activate = Activate,
		.dayDependent = true,
	};
}

} // namespace Dialogs::TopBarSuggestions
