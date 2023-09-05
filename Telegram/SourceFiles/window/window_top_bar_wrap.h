/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"

namespace Window {

template <typename Inner>
class TopBarWrapWidget : public Ui::SlideWrap<Inner> {
	using Parent = Ui::SlideWrap<Inner>;

public:
	TopBarWrapWidget(
		QWidget *parent,
		object_ptr<Inner> inner,
		rpl::producer<bool> oneColumnValue)
	: Parent(parent, std::move(inner)) {
		this->sizeValue(
		) | rpl::start_with_next([=](const QSize &size) {
			updateShadowGeometry(size);
		}, this->lifetime());

		std::move(
			oneColumnValue
		) | rpl::start_with_next([=](bool oneColumn) {
			_isOneColumn = oneColumn;
		}, this->lifetime());
	}

	void updateAdaptiveLayout() {
		updateShadowGeometry(this->size());
	}
	void showShadow() {
		this->entity()->showShadow();
	}
	void hideShadow() {
		this->entity()->hideShadow();
	}
	int contentHeight() const {
		return qMax(this->height() - st::lineWidth, 0);
	}

private:
	void updateShadowGeometry(const QSize &size) {
		const auto skip = _isOneColumn ? 0 : st::lineWidth;
		this->entity()->setShadowGeometryToLeft(
			skip,
			size.height() - st::lineWidth,
			size.width() - skip,
			st::lineWidth);
	}

	bool _isOneColumn = false;

};

} // namespace Window
