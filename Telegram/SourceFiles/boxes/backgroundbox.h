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

#include "abstractbox.h"
#include "core/lambda_wrap.h"

class BackgroundBox : public ItemListBox {
	Q_OBJECT

public:
	BackgroundBox();

public slots:
	void onBackgroundChosen(int index);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	class Inner;
	ChildWidget<Inner> _inner;

};

// This class is hold in header because it requires Qt preprocessing.
class BackgroundBox::Inner : public ScrolledWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent);

signals:
	void backgroundChosen(int index);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void gotWallpapers(const MTPVector<MTPWallPaper> &result);
	void updateWallpapers();

	int32 _bgCount, _rows;
	int32 _over, _overDown;

};
