/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_info_emoji_panel.h"

#include "base/event_filter.h"
#include "boxes/peers/bot_info_field.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/controls/emoji_button.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"

BotInfoEmojiPanel::BotInfoEmojiPanel(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<UserData*> bot)
: _outer(box->getDelegate()->outerContainer().data())
, _bot(bot)
, _panel(base::make_unique_q<ChatHelpers::TabbedPanel>(
	_outer,
	ChatHelpers::TabbedPanelDescriptor{
		.ownedSelector = object_ptr<ChatHelpers::TabbedSelector>(
			nullptr,
			ChatHelpers::TabbedSelectorDescriptor{
				.show = std::move(show),
				.st = st::defaultComposeControls.tabbed,
				.level = Window::GifPauseReason::Layer,
				.mode = ChatHelpers::TabbedSelector::Mode::PeerTitle,
			}),
	})) {
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->hide();
	_panel->selector()->setCurrentPeer(bot);
	_panel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		if (const auto field = _field.get()) {
			Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
			field->setFocus();
		}
	}, _panel->lifetime());
}

BotInfoEmojiPanel::~BotInfoEmojiPanel() = default;

void BotInfoEmojiPanel::attach(not_null<Ui::InputField*> field) {
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue(),
		Core::App().settings().systemTextReplaceValue());
	Ui::Emoji::SuggestionsController::Init(_outer, field, &_bot->session());

	const auto toggle = Ui::CreateChild<Ui::EmojiButton>(
		field,
		st::defaultComposeControls.files.emoji);
	toggle->show();
	toggle->installEventFilter(_panel.get());
	toggle->addClickHandler([=] {
		aim(field, toggle);
		_panel->toggleAnimated();
	});
	base::install_event_filter(toggle, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Enter) {
			aim(field, toggle);
		}
		return base::EventFilterResult::Continue;
	});
	PlaceBotInfoFieldButton(field, toggle, 0);
}

void BotInfoEmojiPanel::aim(
		not_null<Ui::InputField*> field,
		not_null<Ui::EmojiButton*> toggle) {
	_field = field.get();

	const auto parent = _panel->parentWidget();
	const auto local = parent->mapFromGlobal(toggle->mapToGlobal(QPoint()));
	const auto right = local.x() + toggle->width() * 3;
	const auto dropDown = (local.y() < parent->height() / 2);
	_panel->setDropDown(dropDown);
	if (dropDown) {
		_panel->moveTopRight(local.y() + toggle->height(), right);
	} else {
		_panel->moveBottomRight(local.y(), right);
	}
}
