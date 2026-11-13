/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_info_field.h"

#include "base/event_filter.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/emoji_button.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

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

rpl::producer<QString> BotInfoFieldTitle(Data::BotInfoField field) {
	switch (field) {
	case Data::BotInfoField::Name: return tr::lng_bot_info_name();
	case Data::BotInfoField::About: return tr::lng_bot_info_about();
	case Data::BotInfoField::Description: return tr::lng_bot_info_intro();
	}
	Unexpected("Field in BotInfoFieldTitle.");
}

not_null<Ui::InputField*> AddBotInfoField(
		not_null<Ui::VerticalLayout*> container,
		Data::BotInfoField field,
		rpl::producer<QString> placeholder) {
	const auto &titleFont = st::defaultSubsectionTitle.style.font;
	const auto row = container->add(
		object_ptr<Ui::FixedHeightWidget>(container, titleFont->height),
		st::botInfoTitlePadding);
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		row,
		BotInfoFieldTitle(field),
		st::defaultSubsectionTitle);
	title->moveToLeft(0, 0);

	const auto result = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::botInfoField,
			((field == Data::BotInfoField::Name)
				? Ui::InputField::Mode::SingleLine
				: Ui::InputField::Mode::MultiLine),
			std::move(placeholder)),
		st::botInfoFieldPadding);
	const auto limitTop = titleFont->ascent
		- st::defaultInputFieldLimit.style.font->ascent;
	Ui::AddLengthLimitLabel(result, Data::BotInfoFieldLimit(field), {
		.customParent = row,
		.customUpdatePosition = [=](QSize parent, QSize label) {
			return QPoint(parent.width() - label.width(), limitTop);
		},
		.customCharactersCount = [=] {
			return Data::BotInfoTextLength(result->getLastText());
		},
	});
	row->showChildren();
	return result;
}

void PlaceBotInfoFieldButton(
		not_null<Ui::InputField*> field,
		not_null<Ui::RpWidget*> button,
		int index) {
	field->widthValue() | rpl::on_next([=](int width) {
		const auto slot = st::botInfoFieldButtonSize;
		const auto &position = st::botInfoFieldButtonPosition;
		button->moveToRight(
			position.x() + index * slot + (slot - button->width()) / 2,
			position.y() + (slot - button->height()) / 2,
			width);
	}, button->lifetime());
}

void SetBotInfoFieldButtonsCount(not_null<Ui::InputField*> field, int count) {
	const auto right = (count - 1) * st::botInfoFieldButtonSize;
	field->setAdditionalMargins({ 0, 0, right, 0 });
}
