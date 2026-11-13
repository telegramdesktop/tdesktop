/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_info_field.h"

#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

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

void TrackBotInfoField(
		not_null<Ui::InputField*> field,
		rpl::producer<QString> value) {
	const auto last = field->lifetime().make_state<QString>(
		field->getLastText());
	std::move(value) | rpl::on_next([=](const QString &now) {
		if (field->getLastText() == *last) {
			field->setText(now);
		}
		*last = now;
	}, field->lifetime());
}
