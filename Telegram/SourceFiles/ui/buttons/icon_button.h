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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/button.h"
#include "styles/style_widgets.h"

namespace Ui {

class IconButton : public Button {
public:
	IconButton(QWidget *parent, const style::IconButton &st);

	// Pass nullptr to restore the default icon.
	void setIcon(const style::icon *icon, const style::icon *iconOver = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, ButtonStateChangeSource source) override;

private:
	const style::IconButton &_st;
	const style::icon *_iconOverride = nullptr;
	const style::icon *_iconOverrideOver = nullptr;

	FloatAnimation _a_over;

};

class MaskButton : public Button {
public:
	MaskButton(QWidget *parent, const style::MaskButton &st);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, ButtonStateChangeSource source) override;

private:
	const style::MaskButton &_st;

	FloatAnimation _a_iconOver;

};

} // namespace Ui
