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

#include "ui/rp_widget.h"

namespace Overview {
namespace Layout {
class Document;
} // namespace Layout
} // namespace Overview

namespace Media {
namespace Player {

class ListWidget : public Ui::RpWidget, private base::Subscriber {
public:
	ListWidget(QWidget *parent);

	QRect getCurrentTrackGeometry() const;

	~ListWidget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	void itemRemoved(not_null<const HistoryItem*> item);
	int marginTop() const;
	void repaintItem(const HistoryItem *item);
	void playlistUpdated();

	using Layout = Overview::Layout::Document;
	using Layouts = QMap<FullMsgId, Layout*>;
	Layouts _layouts;

	using List = QList<Layout*>;
	List _list;

	style::cursor _cursor = style::cur_default;

};

} // namespace Clip
} // namespace Media
