#pragma once

#include "ui/wrap/slide_wrap.h"

namespace Window {

template <typename Inner>
class TopBarWrapWidget : public Ui::SlideWrap<Inner> {
	using Parent = Ui::SlideWrap<Inner>;

public:
	TopBarWrapWidget(QWidget *parent, object_ptr<Inner> inner)
	: Parent(parent, std::move(inner)) {
		this->sizeValue(
		) | rpl::start_with_next([this](const QSize &size) {
			updateShadowGeometry(size);
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
		auto skip = Adaptive::OneColumn() ? 0 : st::lineWidth;
		this->entity()->setShadowGeometryToLeft(
			skip,
			size.height() - st::lineWidth,
			size.width() - skip,
			st::lineWidth);
	}

};

} // namespace Window
