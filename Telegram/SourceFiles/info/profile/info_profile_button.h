/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
class ToggleView;
} // namespace Ui

namespace Info {
namespace Profile {

class Button : public Ui::RippleButton {
public:
	Button(
		QWidget *parent,
		rpl::producer<QString> &&text);
	Button(
		QWidget *parent,
		rpl::producer<QString> &&text,
		const style::InfoProfileButton &st);

	Button *toggleOn(rpl::producer<bool> &&toggled);
	rpl::producer<bool> toggledValue() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void onStateChanged(
		State was,
		StateChangeSource source) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setText(QString &&text);
	QRect toggleRect() const;
	void updateVisibleText(int newWidth);

	const style::InfoProfileButton &_st;
	QString _original;
	QString _text;
	int _originalWidth = 0;
	int _textWidth = 0;
	std::unique_ptr<Ui::ToggleView> _toggle;

};

} // namespace Profile
} // namespace Info
