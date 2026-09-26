/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_quality_slider.h"

#include "ui/painter.h"
#include "styles/style_editor.h"

#include <QtGui/QtEvents>

namespace Editor {
namespace {

constexpr auto kShiftDuration = crl::time(120);

[[nodiscard]] int LabelsMinSkip() {
	return st::videoQualitySliderStyle.font->height;
}

} // namespace

VideoQualitySlider::VideoQualitySlider(not_null<Ui::RpWidget*> parent)
: RpWidget(parent) {
	setMouseTracking(true);
}

void VideoQualitySlider::setLevels(std::vector<VideoQualityLevel> levels) {
	if (_levels == levels) {
		return;
	}
	const auto was = value();
	_levels = std::move(levels);

	_labels.clear();
	_labels.reserve(_levels.size());
	_labelsWidth = 0;
	const auto &font = st::videoQualitySliderStyle.font;
	for (const auto &level : _levels) {
		_labels.push_back(VideoQualityLabel(level));
		_labelsWidth += font->width(_labels.back());
	}

	// Keep the chosen resolution, not the chosen position; when it is gone,
	// fall back to the original one.
	auto index = int(_levels.size()) - 1;
	for (auto i = 0, count = int(_levels.size()); i != count; ++i) {
		const auto side = _levels[i].shorterSide;
		if (side == was || (was && side > was)) {
			index = i;
			break;
		}
	}
	_index = index;
	_activeShift.stop();
	setVisible(hasChoice());
	update();
	if (value() != was) {
		_valueChanges.fire(value());
	}
}

bool VideoQualitySlider::hasChoice() const {
	return _levels.size() > 1;
}

int VideoQualitySlider::value() const {
	return (_index >= 0 && _index < int(_levels.size()))
		? _levels[_index].shorterSide
		: 0;
}

void VideoQualitySlider::setValue(int shorterSide) {
	for (auto i = 0, count = int(_levels.size()); i != count; ++i) {
		if (_levels[i].shorterSide == shorterSide) {
			_index = i;
			_activeShift.stop();
			update();
			return;
		}
	}
}

void VideoQualitySlider::setIndex(int index, bool notify) {
	const auto count = int(_levels.size());
	index = count ? std::clamp(index, 0, count - 1) : 0;
	if (_index == index) {
		return;
	}
	const auto from = float64(_index);
	_index = index;
	_activeShift.start([=] { update(); }, from, _index, kShiftDuration);
	update();
	if (notify) {
		_valueChanges.fire(value());
	}
}

int VideoQualitySlider::resizeGetHeight(int newWidth) {
	return st::videoQualitySliderHeight;
}

bool VideoQualitySlider::labelsFit() const {
	const auto count = int(_levels.size());
	return (count > 1)
		&& (_labelsWidth + (count - 1) * LabelsMinSkip() <= width());
}

int VideoQualitySlider::dotX(int index) const {
	const auto count = int(_levels.size());
	if (count < 2) {
		return width() / 2;
	}
	const auto side = st::videoQualitySliderSideSkip;
	const auto available = std::max(width() - 2 * side, 1);
	return side + (available * index) / (count - 1);
}

int VideoQualitySlider::indexAt(int x) const {
	const auto count = int(_levels.size());
	if (count < 2) {
		return 0;
	}
	const auto side = st::videoQualitySliderSideSkip;
	const auto available = std::max(width() - 2 * side, 1);
	const auto shifted = std::clamp(x - side, 0, available);
	return std::clamp(
		int(base::SafeRound(shifted * (count - 1) / float64(available))),
		0,
		count - 1);
}

void VideoQualitySlider::paintEvent(QPaintEvent *e) {
	const auto count = int(_levels.size());
	if (count < 2) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto active = _activeShift.value(float64(_index));
	const auto activeSize = st::videoQualitySliderDotActiveSize;
	const auto dotSize = st::videoQualitySliderDotSize;
	const auto lineY = activeSize / 2.;
	const auto lineWidth = st::videoQualitySliderLineWidth;
	const auto gap = st::videoQualitySliderLineGap;

	p.setPen(Qt::NoPen);
	for (auto i = 0; i + 1 != count; ++i) {
		const auto from = dotX(i) + dotSize / 2. + gap;
		const auto till = dotX(i + 1) - dotSize / 2. - gap;
		if (till <= from) {
			continue;
		}
		p.setBrush(st::videoQualitySliderFg);
		p.drawRoundedRect(
			QRectF(from, lineY - lineWidth / 2., till - from, lineWidth),
			lineWidth / 2.,
			lineWidth / 2.);
	}
	for (auto i = 0; i != count; ++i) {
		const auto x = dotX(i);
		const auto distance = std::min(std::abs(active - i), 1.);
		const auto size = activeSize + (dotSize - activeSize) * distance;
		p.setBrush(st::videoQualitySliderActiveFg);
		p.setOpacity((distance < 1.) ? 1. : 0.6);
		p.drawEllipse(QRectF(x - size / 2., lineY - size / 2., size, size));
	}
	p.setOpacity(1.);

	const auto &font = st::videoQualitySliderStyle.font;
	const auto top = activeSize + st::videoQualitySliderLabelSkip;
	p.setFont(font);
	if (labelsFit()) {
		for (auto i = 0; i != count; ++i) {
			const auto width = font->width(_labels[i]);
			const auto x = std::clamp(
				dotX(i) - width / 2,
				0,
				std::max(this->width() - width, 0));
			p.setPen((i == _index)
				? st::videoQualitySliderLabelActiveFg
				: st::videoQualitySliderLabelFg);
			p.drawText(x, top + font->ascent, _labels[i]);
		}
	} else {
		p.setPen(st::videoQualitySliderLabelActiveFg);
		p.drawText(
			QRect(0, top, width(), font->height),
			Qt::AlignHCenter | Qt::AlignTop,
			_labels[_index]);
	}
}

void VideoQualitySlider::applyPosition(int x) {
	setIndex(indexAt(x), true);
}

void VideoQualitySlider::mousePressEvent(QMouseEvent *e) {
	if (_levels.size() < 2 || e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = true;
	applyPosition(e->pos().x());
}

void VideoQualitySlider::mouseMoveEvent(QMouseEvent *e) {
	if (_pressed) {
		applyPosition(e->pos().x());
	}
}

void VideoQualitySlider::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (base::take(_pressed)) {
		applyPosition(e->pos().x());
	}
}

} // namespace Editor
