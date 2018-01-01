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
#include "ui/wrap/vertical_layout.h"

namespace Ui {

QMargins VerticalLayout::getMargins() const {
	auto result = QMargins();
	if (!_rows.empty()) {
		auto &top = _rows.front();
		auto topMargin = top.widget->getMargins().top();
		result.setTop(
			qMax(topMargin - top.margin.top(), 0));
		auto &bottom = _rows.back();
		auto bottomMargin = bottom.widget->getMargins().bottom();
		result.setBottom(
			qMax(bottomMargin - bottom.margin.bottom(), 0));
		for (auto &row : _rows) {
			auto margins = row.widget->getMargins();
			result.setLeft(qMax(
				margins.left() - row.margin.left(),
				result.left()));
			result.setRight(qMax(
				margins.right() - row.margin.right(),
				result.right()));
		}
	}
	return result;
}

int VerticalLayout::naturalWidth() const {
	auto result = 0;
	for (auto &row : _rows) {
		auto natural = row.widget->naturalWidth();
		if (natural < 0) {
			return natural;
		}
		accumulate_max(result, natural);
	}
	return result;
}

int VerticalLayout::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	auto margins = getMargins();
	auto result = 0;
	for (auto &row : _rows) {
		updateChildGeometry(
			margins,
			row.widget,
			row.margin,
			newWidth,
			result);
		result += row.margin.top()
			+ row.widget->heightNoMargins()
			+ row.margin.bottom();
	}
	return result - margins.bottom();
}

void VerticalLayout::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	for (auto &row : _rows) {
		setChildVisibleTopBottom(
			row.widget,
			visibleTop,
			visibleBottom);
	}
}

void VerticalLayout::updateChildGeometry(
		const style::margins &margins,
		RpWidget *child,
		const style::margins &margin,
		int width,
		int top) const {
	auto availRowWidth = width
		- margin.left()
		- margin.right();
	child->resizeToNaturalWidth(availRowWidth);
	child->moveToLeft(
		margins.left() + margin.left(),
		margins.top() + margin.top() + top,
		width);
}

RpWidget *VerticalLayout::addChild(
		object_ptr<RpWidget> child,
		const style::margins &margin) {
	if (auto weak = AttachParentChild(this, child)) {
		_rows.push_back({ std::move(child), margin });
		auto margins = getMargins();
		updateChildGeometry(
			margins,
			weak,
			margin,
			width() - margins.left() - margins.right(),
			height() - margins.top() - margins.bottom());
		weak->heightValue(
		) | rpl::start_with_next_done([this, weak] {
			if (!_inResize) {
				childHeightUpdated(weak);
			}
		}, [this, weak] {
			removeChild(weak);
		}, lifetime());
		return weak;
	}
	return nullptr;
}

void VerticalLayout::childHeightUpdated(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});

	auto margins = getMargins();
	auto top = [&] {
		if (it == _rows.begin()) {
			return margins.top();
		}
		auto prev = it - 1;
		return prev->widget->bottomNoMargins() + prev->margin.bottom();
	}() - margins.top();
	for (auto end = _rows.end(); it != end; ++it) {
		auto &row = *it;
		auto margin = row.margin;
		auto widget = row.widget.data();
		widget->moveToLeft(
			margins.left() + margin.left(),
			margins.top() + top + margin.top());
		top += margin.top()
			+ widget->heightNoMargins()
			+ margin.bottom();
	}
	resize(width(), margins.top() + top + margins.bottom());
}

void VerticalLayout::removeChild(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});
	auto end = _rows.end();
	Assert(it != end);

	auto margins = getMargins();
	auto top = [&] {
		if (it == _rows.begin()) {
			return margins.top();
		}
		auto prev = it - 1;
		return prev->widget->bottomNoMargins() + prev->margin.bottom();
	}() - margins.top();
	for (auto next = it + 1; it != end; ++it) {
		auto &row = *it;
		auto margin = row.margin;
		auto widget = row.widget.data();
		widget->moveToLeft(
			margins.left() + margin.left(),
			margins.top() + top + margin.top());
		top += margin.top()
			+ widget->heightNoMargins()
			+ margin.bottom();
	}
	_rows.erase(it);

	resize(width(), margins.top() + top + margins.bottom());
}

} // namespace Ui
