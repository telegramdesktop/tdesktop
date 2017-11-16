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

namespace Ui {

class VerticalLayout : public RpWidget {
public:
	using RpWidget::RpWidget;

	template <
		typename Widget,
		typename = std::enable_if_t<
			std::is_base_of_v<RpWidget, Widget>>>
	Widget *add(
			object_ptr<Widget> &&child,
			const style::margins &margin = style::margins()) {
		return static_cast<Widget*>(addChild(
			std::move(child),
			margin));
	}

	QMargins getMargins() const override;
	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	RpWidget *addChild(
		object_ptr<RpWidget> child,
		const style::margins &margin);
	void childHeightUpdated(RpWidget *child);
	void removeChild(RpWidget *child);
	void updateChildGeometry(
		const style::margins &margins,
		RpWidget *child,
		const style::margins &margin,
		int width,
		int top) const;

	struct Row {
		object_ptr<RpWidget> widget;
		style::margins margin;
	};
	std::vector<Row> _rows;
	bool _inResize = false;

};

} // namespace Ui
