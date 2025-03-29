/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_top_bar_suggestion.h"

#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/wrap/slide_wrap.h"

namespace Dialogs {

object_ptr<Ui::SlideWrap<Ui::RpWidget>> CreateTopBarSuggestion(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session) {
	const auto content = Ui::CreateChild<TopBarSuggestionContent>(parent);
	auto result = object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>::fromRaw(content));
	const auto wrap = result.data();

	content->setContent(
		tr::lng_dialogs_top_bar_suggestions_birthday_title(
			tr::now,
			Ui::Text::Bold),
		tr::lng_dialogs_top_bar_suggestions_birthday_about(
			tr::now,
			TextWithEntities::Simple));

	rpl::combine(
		parent->widthValue(),
		content->desiredHeightValue()
	) | rpl::start_with_next([=](int width, int height) {
		content->resize(width, height);
	}, content->lifetime());

	return result;
}

} // namespace Dialogs
