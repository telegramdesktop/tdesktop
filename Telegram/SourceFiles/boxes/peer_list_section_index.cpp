/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_section_index.h"

#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kMaxBoost = 1.8;
constexpr auto kEase = 0.6;
constexpr auto kFisheyeReachSlots = 2.2;
constexpr auto kBulgeReserveSlots = 2.5;

} // namespace

PeerListSectionIndex::PeerListSectionIndex(QWidget *parent)
: RpWidget(parent) {
	setMouseTracking(true);
	_fisheye.init([=](crl::time) {
		const auto moving = fisheyeFrame();
		update();
		return moving;
	});
}

void PeerListSectionIndex::setLetters(std::vector<Entry> letters) {
	_letters = std::move(letters);
	relayout();
	update();
}

void PeerListSectionIndex::setVisibleLetters(base::flat_set<QString> letters) {
	if (_visible != letters) {
		_visible = std::move(letters);
		update();
	}
}

void PeerListSectionIndex::setJumpCallback(
		Fn<void(int, anim::type)> callback) {
	_jump = std::move(callback);
}

void PeerListSectionIndex::setScrollCallback(
		Fn<void(not_null<QWheelEvent*>)> callback) {
	_scroll = std::move(callback);
}

int PeerListSectionIndex::idealWidth() const {
	return st::peerListIndexWidth + st::peerListIndexApproach;
}

float64 PeerListSectionIndex::columnCenterX() const {
	return style::RightToLeft()
		? (st::peerListIndexWidth / 2.)
		: (width() - st::peerListIndexWidth / 2.);
}

void PeerListSectionIndex::resizeEvent(QResizeEvent *e) {
	relayout();
}

void PeerListSectionIndex::relayout() {
	_slots.clear();
	_current = -1;
	const auto count = int(_letters.size());
	if (!count || height() <= 0) {
		_scale.clear();
		return;
	}
	const auto itemHeight = st::peerListIndexSlotHeight;
	const auto minHeight = st::peerListIndexMinSlotHeight;
	const auto inset = st::peerListIndexVerticalInset;
	const auto fullHeight = std::max(height() - 2 * inset, minHeight);
	const auto reserve = int(kBulgeReserveSlots * itemHeight);
	const auto usable = std::max(fullHeight - reserve, minHeight);
	const auto maxFit = std::max(usable / minHeight, 1);
	const auto skip = (count <= maxFit)
		? 1
		: ((count + maxFit - 1) / maxFit);
	auto kept = std::vector<int>();
	for (auto i = 0; i < count; i += skip) {
		kept.push_back(i);
	}
	if (kept.back() != count - 1) {
		kept.push_back(count - 1);
	}
	const auto actualCount = int(kept.size());
	const auto pitch = std::min(itemHeight, usable / actualCount);
	_pitch = pitch;
	const auto totalHeight = actualCount * pitch;
	const auto blockTop = inset + std::max(0, (fullHeight - totalHeight) / 2);
	_slots.reserve(actualCount);
	for (auto d = 0; d != actualCount; ++d) {
		_slots.push_back({
			_letters[kept[d]].letter,
			kept[d],
			blockTop + pitch * d + (pitch / 2),
		});
	}
	_scale.assign(actualCount, 1.);
}

float64 PeerListSectionIndex::slotScale(int slotY) const {
	if (_cursorY < 0 || !_pitch) {
		return 1.;
	}
	const auto reach = kFisheyeReachSlots * _pitch;
	const auto vertical = std::clamp(
		std::abs(slotY - _cursorY) / reach,
		0.,
		1.);
	const auto verticalFalloff = std::cos(M_PI_2 * vertical);

	const auto approach = float64(st::peerListIndexApproach);
	const auto edge = style::RightToLeft()
		? float64(st::peerListIndexWidth)
		: (width() - st::peerListIndexWidth);
	const auto away = style::RightToLeft()
		? std::clamp((_cursorX - edge) / approach, 0., 1.)
		: std::clamp((edge - _cursorX) / approach, 0., 1.);
	const auto horizontalFalloff = 1. - away * away * (3. - 2. * away);

	return 1. + (kMaxBoost - 1.) * verticalFalloff * horizontalFalloff;
}

bool PeerListSectionIndex::updateCursorFromGlobal() {
	const auto position = mapFromGlobal(QCursor::pos());
	const auto scrollBar = st::boxScroll.width;
	const auto rtl = style::RightToLeft();
	const auto left = rtl ? -scrollBar : 0;
	const auto right = rtl ? width() : (width() + scrollBar);
	if (position.x() >= left
		&& position.x() <= right
		&& position.y() >= 0
		&& position.y() <= height()) {
		_cursorX = position.x();
		_cursorY = position.y();
		return true;
	}
	_cursorX = -1;
	_cursorY = -1;
	return false;
}

bool PeerListSectionIndex::fisheyeFrame() {
	const auto wasX = _cursorX;
	const auto wasY = _cursorY;
	const auto influenced = updateCursorFromGlobal();
	auto moving = false;
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto target = slotScale(_slots[i].y);
		const auto delta = target - _scale[i];
		if (std::abs(delta) > 0.002) {
			_scale[i] += delta * kEase;
			moving = true;
		} else {
			_scale[i] = target;
		}
	}
	if (moving || _cursorX != wasX || _cursorY != wasY) {
		update();
	}
	return moving || influenced;
}

void PeerListSectionIndex::setCursor(QPoint position) {
	_cursorX = position.x();
	_cursorY = position.y();
	if (!_fisheye.animating()) {
		_fisheye.start();
	}
}

int PeerListSectionIndex::slotAtY(int y) const {
	auto best = -1;
	auto bestDistance = 0;
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto distance = std::abs(_slots[i].y - y);
		if (best < 0 || distance < bestDistance) {
			best = i;
			bestDistance = distance;
		}
	}
	return best;
}

void PeerListSectionIndex::scrubTo(int slot, anim::type animated) {
	if (slot < 0 || slot == _current) {
		return;
	}
	_current = slot;
	if (_jump) {
		_jump(_letters[_slots[slot].sourceIndex].contentTop, animated);
	}
	update();
}

void PeerListSectionIndex::mousePressEvent(QMouseEvent *e) {
	_scrubbing = true;
	setCursor(e->pos());
	scrubTo(slotAtY(e->pos().y()), anim::type::normal);
}

void PeerListSectionIndex::mouseMoveEvent(QMouseEvent *e) {
	setCursor(e->pos());
	if (_scrubbing) {
		scrubTo(slotAtY(e->pos().y()), anim::type::instant);
	} else {
		update();
	}
}

void PeerListSectionIndex::mouseReleaseEvent(QMouseEvent *e) {
	_scrubbing = false;
	_current = -1;
	update();
}

void PeerListSectionIndex::wheelEvent(QWheelEvent *e) {
	if (_scroll) {
		_scroll(e);
	}
}

void PeerListSectionIndex::leaveEventHook(QEvent *e) {
	if (!_fisheye.animating()) {
		_fisheye.start();
	}
}

void PeerListSectionIndex::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto count = int(_slots.size());
	if (!count) {
		return;
	}
	const auto &font = st::peerListIndexFont;
	p.setFont(font);
	const auto centerX = columnCenterX();

	auto centers = std::vector<float64>(count);
	if (_cursorY < 0) {
		for (auto i = 0; i != count; ++i) {
			centers[i] = _slots[i].y;
		}
	} else {
		auto expanded = 0.;
		for (auto i = 0; i != count; ++i) {
			expanded += _scale[i] * _pitch;
		}
		const auto inset = st::peerListIndexVerticalInset;
		auto edge = inset + (height() - 2 * inset - expanded) / 2.;
		for (auto i = 0; i != count; ++i) {
			const auto size = _scale[i] * _pitch;
			centers[i] = edge + size / 2.;
			edge += size;
		}
	}

	for (auto i = 0; i != count; ++i) {
		const auto &slot = _slots[i];
		const auto scale = _scale[i];
		const auto active = (_current == i)
			|| _visible.contains(slot.letter);
		p.setPen(active
			? st::peerListIndexActiveFg
			: st::peerListIndexFg);
		const auto centerY = centers[i];
		const auto baseline = centerY + (font->ascent - font->descent) / 2.;
		const auto x = centerX - (font->width(slot.letter) / 2.);
		if (scale > 1.001) {
			p.save();
			p.translate(centerX, centerY);
			p.scale(scale, scale);
			p.translate(-centerX, -centerY);
			p.drawText(QPointF(x, baseline), slot.letter);
			p.restore();
		} else {
			p.drawText(QPointF(x, baseline), slot.letter);
		}
	}
}
