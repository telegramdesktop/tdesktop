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

#include "ui/twidget.h"
#include "ui/effects/rect_shadow.h"

class Dropdown : public TWidget {
	Q_OBJECT

public:
	Dropdown(QWidget *parent, const style::dropdown &st = st::dropdownDef);

	IconedButton *addButton(IconedButton *button);
	void resetButtons();
	void updateButtons();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void otherEnter();
	void otherLeave();

	void fastHide();
	void ignoreShow(bool ignore = true);

	void step_appearance(float64 ms, bool timer);

	bool eventFilter(QObject *obj, QEvent *e);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_appearance.animating()) return false;

		return QRect(_st.padding.left(),
					 _st.padding.top(),
					 _width - _st.padding.left() - _st.padding.right(),
					 _height - _st.padding.top() - _st.padding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

signals:
	void hiding();

public slots:
	void hideStart();
	void hideFinish();

	void showStart();
	void onWndActiveChanged();

	void buttonStateChanged(int oldState, ButtonStateChangeSource source);

private:
	void adjustButtons();

	bool _ignore = false;

	typedef QVector<IconedButton*> Buttons;
	Buttons _buttons;

	int32 _selected = -1;

	const style::dropdown &_st;

	int32 _width, _height;
	bool _hiding = false;

	anim::fvalue a_opacity;
	Animation _a_appearance;

	QTimer _hideTimer;

	Ui::RectShadow _shadow;

};

class DragArea : public TWidget {
	Q_OBJECT

public:
	DragArea(QWidget *parent);

	void setText(const QString &text, const QString &subtext);

	void otherEnter();
	void otherLeave();

	void fastHide();

	void step_appearance(float64 ms, bool timer);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_appearance.animating()) return false;

		return QRect(st::dragPadding.left(),
					 st::dragPadding.top(),
					 width() - st::dragPadding.left() - st::dragPadding.right(),
					 height() - st::dragPadding.top() - st::dragPadding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;

signals:
	void dropped(const QMimeData *data);

public slots:
	void hideStart();
	void hideFinish();

	void showStart();

private:
	bool _hiding, _in;

	anim::fvalue a_opacity;
	anim::cvalue a_color;
	Animation _a_appearance;

	Ui::RectShadow _shadow;

	QString _text, _subtext;

};
