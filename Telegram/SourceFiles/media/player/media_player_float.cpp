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
#include "media/player/media_player_float.h"

#include <rpl/merge.h>
#include "data/data_document.h"
#include "history/history_media.h"
#include "media/media_clip_reader.h"
#include "media/view/media_clip_playback.h"
#include "media/media_audio.h"
#include "auth_session.h"
#include "styles/style_media_player.h"
#include "styles/style_history.h"

namespace Media {
namespace Player {

Float::Float(
	QWidget *parent,
	HistoryItem *item,
	base::lambda<void(bool visible)> toggleCallback,
	base::lambda<void(bool closed)> draggedCallback)
: RpWidget(parent)
, _item(item)
, _toggleCallback(std::move(toggleCallback))
, _draggedCallback(std::move(draggedCallback)) {
	auto media = _item->getMedia();
	Assert(media != nullptr);

	auto document = media->getDocument();
	Assert(document != nullptr);
	Assert(document->isRoundVideo());

	auto margin = st::mediaPlayerFloatMargin;
	auto size = 2 * margin + st::mediaPlayerFloatSize;
	resize(size, size);

	prepareShadow();

	// #TODO rpl::merge
	rpl::merge(
		Auth().data().itemLayoutChanged(),
		Auth().data().itemRepaintRequest())
		| rpl::start_with_next([this](auto item) {
			if (_item == item) {
				repaintItem();
			}
		}, lifetime());
	Auth().data().itemRemoved()
		| rpl::start_with_next([this](auto item) {
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
	if (_down) {
		_down = false;
		if (auto media = _item ? _item->getMedia() : nullptr) {
			media->playInline();
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
		if (auto media = _item->getMedia()) {
			media->playInline();
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

	auto progress = _roundPlayback ? _roundPlayback->value(getms()) : 1.;
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
	if (auto media = _item ? _item->getMedia() : nullptr) {
		if (auto reader = media->getClipReader()) {
			if (reader->started() && reader->mode() == Clip::Reader::Mode::Video) {
				return reader;
			}
		}
	}
	return nullptr;
}

bool Float::hasFrame() const {
	if (auto reader = getReader()) {
		return !reader->current().isNull();
	}
	return false;
}

bool Float::fillFrame() {
	auto creating = _frame.isNull();
	if (creating) {
		_frame = QImage(getInnerRect().size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(cRetinaFactor());
	}
	auto frameInner = [this] {
		return QRect(0, 0, _frame.width() / cIntRetinaFactor(), _frame.height() / cIntRetinaFactor());
	};
	if (auto reader = getReader()) {
		updatePlayback();
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

void Float::updatePlayback() {
	if (_item) {
		if (!_roundPlayback) {
			_roundPlayback = std::make_unique<Media::Clip::Playback>();
			_roundPlayback->setValueChangedCallback([this](float64 value) {
				update();
			});
		}
		auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
		if (state.id.contextId() == _item->fullId()) {
			_roundPlayback->updateState(state);
		}
	}
}

void Float::repaintItem() {
	update();
	if (hasFrame() && _toggleCallback) {
		_toggleCallback(true);
	}
}

} // namespace Player
} // namespace Media
