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

#include "layerwidget.h"
#include "ui/widgets/shadow.h"

namespace Ui {
class IconButton;
class GradientShadow;
class ScrollArea;
} // namespace Ui

class AbstractBox : public LayerWidget, protected base::Subscriber {
	Q_OBJECT

public:
	AbstractBox(int w = 0, const QString &title = QString());
	void parentResized() override;

	void setTitleText(const QString &title);
	void setAdditionalTitle(const QString &additionalTitle);
	void setBlockTitle(bool block, bool withClose = true, bool withShadow = true);

public slots:
	void onClose();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void raiseShadow();
	int titleHeight() const;
	void paintTitle(Painter &p, const QString &title, const QString &additional = QString());
	void setMaxHeight(int32 maxHeight);
	void resizeMaxHeight(int32 newWidth, int32 maxHeight);

	virtual void closePressed() {
	}

private:
	void updateBlockTitleGeometry();
	int countHeight() const;

	int _maxHeight = 0;

	bool _closed = false;

	QString _title;
	QString _additionalTitle;
	bool _blockTitle = false;
	ChildWidget<Ui::IconButton> _blockClose = { nullptr };
	ChildWidget<Ui::GradientShadow> _blockShadow = { nullptr };

};

class ScrollableBoxShadow : public Ui::PlainShadow {
public:
	ScrollableBoxShadow(QWidget *parent);

};

class ScrollableBox : public AbstractBox {
public:
	ScrollableBox(const style::FlatScroll &scroll, int w = 0);

protected:
	void init(TWidget *inner, int bottomSkip = -1, int topSkip = -1);
	void setScrollSkips(int bottomSkip = -1, int topSkip = -1);

	void resizeEvent(QResizeEvent *e) override;

	Ui::ScrollArea *scrollArea() {
		return _scroll;
	}

private:
	void updateScrollGeometry();

	ChildWidget<Ui::ScrollArea> _scroll;
	int _topSkip, _bottomSkip;

};

class ItemListBox : public ScrollableBox {
public:
	ItemListBox(const style::FlatScroll &scroll, int32 w = 0);

};

enum CreatingGroupType {
	CreatingGroupNone,
	CreatingGroupGroup,
	CreatingGroupChannel,
};
