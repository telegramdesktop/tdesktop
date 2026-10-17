/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"

namespace HistoryView {

class BotMenuButton final : public Ui::RoundButton {
public:
	BotMenuButton(
		QWidget *parent,
		const QString &menuText,
		bool small,
		Fn<void()> clicked,
		Fn<void()> widthChanged,
		Ui::Text::CustomEmojiFactory otherEmoji);

	[[nodiscard]] bool refresh(const QString &menuText, bool small);

private:
	void animateSize(const QString &wasText, bool wasSmall);
	void paintEvent(QPaintEvent *e) override;

	Ui::Animations::Simple _widthAnimation;
	Ui::Animations::Simple _contentFade;
	Ui::Text::String _fading;
	Ui::Text::String _appearing;
	int _fadeFromLeft = 0;
	int _fadeFromWidth = 0;
	int _fadeToLeft = 0;
	int _fadeToWidth = 0;
	Ui::Text::CustomEmojiFactory _otherEmoji;
	QString _text;
	bool _small = false;

};

} // namespace HistoryView
