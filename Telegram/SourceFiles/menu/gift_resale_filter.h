/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/menu/menu_action.h"
#include "ui/text/text_custom_emoji.h"

namespace Ui {

class GiftResaleFilterAction final : public Menu::Action {
public:
	GiftResaleFilterAction(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		const TextWithEntities &text,
		const Text::MarkedContext &context,
		QString iconEmojiData,
		const style::icon *icon);

	void setChecked(bool checked);

private:
	void paintEvent(QPaintEvent *e) override;

	const std::unique_ptr<Text::CustomEmoji> _iconEmoji;
	bool _checked = false;

};

class GiftResaleColorEmoji final : public Text::CustomEmoji {
public:
	GiftResaleColorEmoji(QStringView data);

	[[nodiscard]] static bool Owns(QStringView data);
	[[nodiscard]] static QString DataFor(QColor color);

	int width() override;
	QString entityData() override;

	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	QColor _color;

};

} // namespace Ui
