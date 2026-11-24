/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_view.h"
#include "media/view/media_view_overlay_widget.h"
#include "ui/painter.h"

#include "styles/style_media_view.h"

#include <QtGui/QPainterPath>

namespace Media::View {

void OverlayWidget::setupStoryCaptionLayer() {
	_storyAreaLayer->setAttribute(Qt::WA_TransparentForMouseEvents);
	_storyAreaLayer->show();
	_storyCaptionLayer->setAttribute(Qt::WA_TransparentForMouseEvents);
	_storyCaptionLayer->hide();
	_body->sizeValue() | rpl::on_next([=](QSize size) {
		_storyAreaLayer->resize(size);
	}, _storyAreaLayer->lifetime());
	_storyCaptionLayer->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		if (!_stories || _stories->skipCaption()) {
			return;
		}
		auto p = Painter(_storyCaptionLayer.get());
		const auto position = _storyCaptionLayer->pos();
		p.translate(-position);
		const auto layout = _stories->contentLayout();
		const auto shadow = storyCaptionShadow();
		if (!_caption.isEmpty()) {
			auto hq = PainterHighQualityEnabler(p);
			auto shape = QPainterPath();
			shape.addRoundedRect(
				QRectF(layout.geometry),
				layout.radius,
				layout.radius);
			p.save();
			p.setClipPath(shape, Qt::IntersectClip);
			p.setOpacity(1. - layout.fade);
			st::storiesShadowBottom.fill(p, shadow);
			const auto bottom = shadow.y() + shadow.height();
			const auto end = layout.geometry.bottom() + 1;
			p.fillRect(
				layout.geometry.x(),
				bottom,
				layout.geometry.width(),
				std::max(end - bottom, 0),
				QColor(0, 0, 0, 88));
			p.restore();
		}
		paintCaptionContent(
			p,
			captionGeometry(),
			clip.translated(position),
			1.);
	}, _storyCaptionLayer->lifetime());
}

void OverlayWidget::updateStoryCaptionLayer() {
	if (!_stories
		|| _stories->skipCaption()
		|| _fullScreenVideo
		|| _controlsOpacity.current() <= 0.
		|| _captionRect.width() <= 0) {
		_storyCaptionLayer->hide();
		return;
	}
	auto bounds = captionGeometry();
	if (!_caption.isEmpty()) {
		auto shade = _stories->contentLayout().geometry;
		shade.setTop(storyCaptionShadow().top());
		bounds = bounds.united(shade);
	}
	_storyCaptionLayer->setGeometry(bounds);
	_storyCaptionLayer->show();
	_storyCaptionLayer->update();
}

QRect OverlayWidget::storyCaptionShadow() const {
	const auto content = _stories->contentLayout().geometry;
	return QRect(
		content.x(),
		_captionRect.y() - st::mediaviewCaptionStyle.font->height,
		content.width(),
		st::storiesShadowBottom.height());
}

} // namespace Media::View
