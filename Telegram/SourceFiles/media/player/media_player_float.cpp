/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_float.h"

#include <rpl/merge.h>
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "history/history_media.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "media/media_clip_reader.h"
#include "media/media_audio.h"
#include "media/view/media_clip_playback.h"
#include "media/player/media_player_round_controller.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "styles/style_media_player.h"
#include "styles/style_history.h"

namespace Media {
namespace Player {

Float::Float(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<HistoryItem*> item,
	base::lambda<void(bool visible)> toggleCallback,
	base::lambda<void(bool closed)> draggedCallback)
: RpWidget(parent)
, _controller(controller)
, _item(item)
, _toggleCallback(std::move(toggleCallback))
, _draggedCallback(std::move(draggedCallback)) {
	auto media = _item->media();
	Assert(media != nullptr);

	auto document = media->document();
	Assert(document != nullptr);
	Assert(document->isVideoMessage());

	auto margin = st::mediaPlayerFloatMargin;
	auto size = 2 * margin + st::mediaPlayerFloatSize;
	resize(size, size);

	prepareShadow();

	Auth().data().itemRepaintRequest(
	) | rpl::start_with_next([this](auto item) {
		if (_item == item) {
			repaintItem();
		}
	}, lifetime());
	Auth().data().itemRemoved(
	) | rpl::start_with_next([this](auto item) {
		if (_item == item) {
			detach();
		}
	}, lifetime());

	setCursor(style::cur_pointer);
}

void Float::mousePressEvent(QMouseEvent *e) {
	_down = true;
	_downPoint = e->pos();
}

void Float::mouseMoveEvent(QMouseEvent *e) {
	if (_down && (e->pos() - _downPoint).manhattanLength() > QApplication::startDragDistance()) {
		_down = false;
		_drag = true;
		_dragLocalPoint = e->pos();
	} else if (_drag) {
		auto delta = (e->pos() - _dragLocalPoint);
		move(pos() + delta);
		setOpacity(outRatio());
	}
}

float64 Float::outRatio() const {
	auto parent = parentWidget()->rect();
	auto min = 1.;
	if (x() < parent.x()) {
		accumulate_min(min, 1. - (parent.x() - x()) / float64(width()));
	}
	if (y() < parent.y()) {
		accumulate_min(min, 1. - (parent.y() - y()) / float64(height()));
	}
	if (x() + width() > parent.x() + parent.width()) {
		accumulate_min(min, 1. - (x() + width() - parent.x() - parent.width()) / float64(width()));
	}
	if (y() + height() > parent.y() + parent.height()) {
		accumulate_min(min, 1. - (y() + height() - parent.y() - parent.height()) / float64(height()));
	}
	return snap(min, 0., 1.);
}

void Float::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_down) && _item) {
		if (const auto controller = _controller->roundVideo(_item)) {
			controller->pauseResume();
		}
	}
	if (_drag) {
		finishDrag(outRatio() < 0.5);
	}
}

void Float::finishDrag(bool closed) {
	_drag = false;
	if (_draggedCallback) {
		_draggedCallback(closed);
	}
}

void Float::mouseDoubleClickEvent(QMouseEvent *e) {
	if (_item) {
		// Handle second click.
		if (const auto controller = _controller->roundVideo(_item)) {
			controller->pauseResume();
		}
		Ui::showPeerHistoryAtItem(_item);
	}
}

void Float::detach() {
	if (_item) {
		_item = nullptr;
		if (_toggleCallback) {
			_toggleCallback(false);
		}
	}
}

void Float::prepareShadow() {
	auto shadow = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	shadow.fill(Qt::transparent);
	shadow.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&shadow);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::shadowFg);
		auto extend = 2 * st::lineWidth;
		p.drawEllipse(getInnerRect().marginsAdded(QMargins(extend, extend, extend, extend)));
	}
	_shadow = App::pixmapFromImageInPlace(Images::prepareBlur(std::move(shadow)));
}

QRect Float::getInnerRect() const {
	auto margin = st::mediaPlayerFloatMargin;
	return rect().marginsRemoved(QMargins(margin, margin, margin, margin));
}

void Float::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setOpacity(_opacity);
	p.drawPixmap(0, 0, _shadow);

	if (!fillFrame() && _toggleCallback) {
		_toggleCallback(false);
	}

	auto inner = getInnerRect();
	p.drawImage(inner.topLeft(), _frame);

	const auto playback = getPlayback();
	const auto progress = playback ? playback->value(getms()) : 1.;
	if (progress > 0.) {
		auto pen = st::historyVideoMessageProgressFg->p;
		auto was = p.pen();
		pen.setWidth(st::radialLine);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setOpacity(_opacity * st::historyVideoMessageProgressOpacity);

		auto from = QuarterArcLength;
		auto len = -qRound(FullArcLength * progress);
		auto stepInside = st::radialLine / 2;
		{
			PainterHighQualityEnabler hq(p);
			p.drawArc(inner.marginsRemoved(QMargins(stepInside, stepInside, stepInside, stepInside)), from, len);
		}

		//p.setPen(was);
		//p.setOpacity(_opacity);
	}
}

Clip::Reader *Float::getReader() const {
	if (detached()) {
		return nullptr;
	}
	if (const auto controller = _controller->roundVideo(_item)) {
		if (const auto reader = controller->reader()) {
			if (reader->started()) {
				return reader;
			}
		}
	}
	return nullptr;
}

Clip::Playback *Float::getPlayback() const {
	if (detached()) {
		return nullptr;
	}
	if (const auto controller = _controller->roundVideo(_item)) {
		return controller->playback();
	}
	return nullptr;
}

bool Float::hasFrame() const {
	if (const auto reader = getReader()) {
		return !reader->current().isNull();
	}
	return false;
}

bool Float::fillFrame() {
	auto creating = _frame.isNull();
	if (creating) {
		_frame = QImage(
			getInnerRect().size() * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(cRetinaFactor());
	}
	auto frameInner = [&] {
		return QRect(QPoint(), _frame.size() / cIntRetinaFactor());
	};
	if (const auto reader = getReader()) {
		auto frame = reader->current();
		if (!frame.isNull()) {
			_frame.fill(Qt::transparent);

			Painter p(&_frame);
			PainterHighQualityEnabler hq(p);
			p.drawPixmap(frameInner(), frame);
			return true;
		}
	}
	if (creating) {
		_frame.fill(Qt::transparent);

		Painter p(&_frame);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::imageBg);
		p.drawEllipse(frameInner());
	}
	return false;
}

void Float::repaintItem() {
	update();
	if (hasFrame() && _toggleCallback) {
		_toggleCallback(true);
	}
}

} // namespace Player
} // namespace Media
