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

#include "ui/widgets/buttons.h"
#include "styles/style_widgets.h"

namespace Ui {

class HistoryDownButton : public RippleButton {
public:
	HistoryDownButton(QWidget *parent, const style::TwoIconButton &st);

	void setUnreadCount(int unreadCount);
	int unreadCount() const {
		return _unreadCount;
	}

	bool hidden() const;

	void showAnimated();
	void hideAnimated();

	void finishAnimation();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void toggleAnimated();
	void step_arrowOver(float64 ms, bool timer);

	const style::TwoIconButton &_st;

	bool _shown = false;

	anim::fvalue a_arrowOpacity;
	Animation _a_arrowOver;

	FloatAnimation _a_show;

	int _unreadCount = 0;

};

class EmojiButton : public AbstractButton {
public:
	EmojiButton(QWidget *parent, const style::IconButton &st);

	void setLoading(bool loading);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(int oldState, StateChangeSource source) override;

private:
	const style::IconButton &_st;

	bool _loading = false;
	FloatAnimation a_loading;
	Animation _a_loading;

	void step_loading(uint64 ms, bool timer) {
		if (timer) {
			update();
		}
	}

};

} // namespace Ui
