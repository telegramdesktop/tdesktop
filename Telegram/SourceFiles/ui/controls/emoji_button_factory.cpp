/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/emoji_button_factory.h"

#include "base/event_filter.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "ui/controls/emoji_button.h"
#include "ui/effects/fade_animation.h"
#include "ui/layers/box_content.h"
#include "ui/rect.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h" // defaultComposeFiles.
#include "styles/style_settings.h"

namespace Ui {

[[nodiscard]] not_null<Ui::EmojiButton*> AddEmojiToggleToField(
		not_null<Ui::InputField*> field,
		not_null<Ui::BoxContent*> box,
		not_null<Window::SessionController*> controller,
		not_null<ChatHelpers::TabbedPanel*> emojiPanel,
		QPoint shift) {
	const auto emojiToggle = Ui::CreateChild<Ui::EmojiButton>(
		field->parentWidget(),
		st::defaultComposeFiles.emoji);
	const auto fade = Ui::CreateChild<Ui::FadeAnimation>(
		emojiToggle,
		emojiToggle,
		0.5);
	{
		const auto fadeTarget = Ui::CreateChild<Ui::RpWidget>(emojiToggle);
		fadeTarget->resize(emojiToggle->size());
		fadeTarget->paintRequest(
		) | rpl::start_with_next([=](const QRect &rect) {
			auto p = QPainter(fadeTarget);
			if (fade->animating()) {
				p.fillRect(fadeTarget->rect(), st::boxBg);
			}
			fade->paint(p);
		}, fadeTarget->lifetime());
		rpl::single(false) | rpl::then(
			field->focusedChanges()
		) | rpl::start_with_next([=](bool shown) {
			if (shown) {
				fade->fadeIn(st::universalDuration);
			} else {
				fade->fadeOut(st::universalDuration);
			}
		}, emojiToggle->lifetime());
		fade->fadeOut(1);
		fade->finish();
	}


	const auto outer = box->getDelegate()->outerContainer();
	const auto allow = [](not_null<DocumentData*>) { return true; };
	InitMessageFieldHandlers(
		controller,
		field,
		Window::GifPauseReason::Layer,
		allow);
	Ui::Emoji::SuggestionsController::Init(
		outer,
		field,
		&controller->session(),
		Ui::Emoji::SuggestionsController::Options{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = allow,
		});
	const auto updateEmojiPanelGeometry = [=] {
		const auto parent = emojiPanel->parentWidget();
		const auto global = emojiToggle->mapToGlobal({ 0, 0 });
		const auto local = parent->mapFromGlobal(global);
		const auto right = local.x() + emojiToggle->width() * 3;
		const auto isDropDown = local.y() < parent->height() / 2;
		emojiPanel->setDropDown(isDropDown);
		if (isDropDown) {
			emojiPanel->moveTopRight(
				local.y() + emojiToggle->height(),
				right);
		} else {
			emojiPanel->moveBottomRight(local.y(), right);
		}
	};
	rpl::combine(
		box->sizeValue(),
		field->geometryValue()
	) | rpl::start_with_next([=](QSize outer, QRect inner) {
		emojiToggle->moveToLeft(
			rect::right(inner) + shift.x(),
			inner.y() + shift.y());
		emojiToggle->update();
	}, emojiToggle->lifetime());

	emojiToggle->installEventFilter(emojiPanel);
	emojiToggle->addClickHandler([=] {
		updateEmojiPanelGeometry();
		emojiPanel->toggleAnimated();
	});
	const auto filterCallback = [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Enter) {
			updateEmojiPanelGeometry();
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(emojiToggle, filterCallback);

	return emojiToggle;
}

} // namespace Ui
