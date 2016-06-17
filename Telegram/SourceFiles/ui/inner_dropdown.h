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

class ScrollArea;

namespace Ui {

class InnerDropdown : public TWidget {
	Q_OBJECT

public:
	InnerDropdown(QWidget *parent, const style::dropdown &st = st::dropdownDef, const style::flatScroll &scrollSt = st::scrollDef);

	void setOwnedWidget(ScrolledWidget *widget);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !_a_appearance.isNull()) return false;

		return rect().marginsRemoved(_st.padding).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void otherEnter();
	void otherLeave();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

signals:
	void hidden();

private slots:
	void onHideStart();
	void onWindowActiveChanged();
	void onScroll();

private:
	void repaintCallback();

	void hidingFinished();
	void showingStarted();

	void startAnimation();

	const style::dropdown &_st;

	bool _hiding = false;

	QPixmap _cache;
	FloatAnimation _a_appearance;

	QTimer _hideTimer;

	BoxShadow _shadow;
	ChildWidget<ScrollArea> _scroll;

};

} // namespace Ui
