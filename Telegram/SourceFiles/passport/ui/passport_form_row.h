/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"

namespace Passport::Ui {

using namespace ::Ui;

class FormRow : public RippleButton {
public:
	explicit FormRow(QWidget *parent);

	void updateContent(
		const QString &title,
		const QString &description,
		bool ready,
		bool error,
		anim::type animated);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	int countAvailableWidth() const;
	int countAvailableWidth(int newWidth) const;

	Text::String _title;
	Text::String _description;
	int _titleHeight = 0;
	int _descriptionHeight = 0;
	bool _ready = false;
	bool _error = false;
	Animations::Simple _errorAnimation;

};

} // namespace Passport::Ui
