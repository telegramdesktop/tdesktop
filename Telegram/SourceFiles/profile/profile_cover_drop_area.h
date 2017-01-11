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

namespace Profile {

class CoverDropArea : public TWidget {
public:
	CoverDropArea(QWidget *parent, const QString &title, const QString &subtitle);

	void showAnimated();

	using HideFinishCallback = base::lambda<void(CoverDropArea*)>;
	void hideAnimated(HideFinishCallback &&callback);

	bool hiding() const {
		return _hiding;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setupAnimation();

	QString _title, _subtitle;
	int _titleWidth, _subtitleWidth;

	QPixmap _cache;
	Animation _a_appearance;
	bool _hiding = false;
	HideFinishCallback _hideFinishCallback;

};

} // namespace Profile
