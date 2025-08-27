/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_layer.h"

#include "styles/style_info.h"
#include "styles/style_info_userpic_builder.h"
#include "styles/style_layers.h"

namespace UserpicBuilder {

LayerWidget::LayerWidget()
: _corners(Ui::PrepareCornerPixmaps(st::boxRadius, st::boxDividerBg)) {
}

void LayerWidget::setContent(not_null<Ui::RpWidget*> content) {
	_content = content;
}

void LayerWidget::parentResized() {
	Expects(_content != nullptr);
	const auto parentSize = parentWidget()->size();
	const auto currentHeight = resizeGetHeight(0);
	const auto currentWidth = _content->width();
	resizeToWidth(currentWidth);
	moveToLeft(
		(parentSize.width() - currentWidth) / 2,
		(parentSize.height() - currentHeight) / 2);
}

bool LayerWidget::closeByOutsideClick() const {
	return false;
}

int LayerWidget::resizeGetHeight(int newWidth) {
	Expects(_content != nullptr);
	_content->resizeToWidth(st::infoDesiredWidth);
	return st::userpicBuilderEmojiLayerMinHeight;
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	Ui::FillRoundRect(p, rect(), st::boxDividerBg, _corners);
}

} // namespace UserpicBuilder
