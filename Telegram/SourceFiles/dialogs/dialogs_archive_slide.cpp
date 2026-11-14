/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_archive_slide.h"

#include "dialogs/ui/dialogs_layout.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

namespace Dialogs {
namespace {

void EraseTitle(ArchiveBand &band) {
	const auto &font = st::semiboldFont;
	const auto ratio = band.image.devicePixelRatio();
	const auto bg = band.image.pixelColor(band.image.width() - 1, 0);
	auto p = QPainter(&band.image);
	const auto left = band.titleBaseline.x() - 1;
	p.fillRect(
		left,
		band.titleBaseline.y() - font->ascent,
		std::min(
			font->width(band.title) + 2,
			int(band.image.width() / ratio) - left),
		font->height,
		bg);
}

} // namespace

ArchiveBand GrabArchiveBand(
		not_null<QWidget*> widget,
		int height,
		bool collapsed,
		const style::DialogRow &st,
		const QString &title) {
	const auto narrow = (widget->width() < st::columnMinimalWidthLeft / 2);
	return {
		.image = Ui::GrabWidgetToImage(
			widget,
			QRect(0, 0, widget->width(), height),
			st::dialogsBg->c),
		.title = narrow ? QString() : title,
		.titleBaseline = (collapsed
			? QPoint(
				st::dialogsTopBarLeftPadding,
				Ui::CollapsedRowTextBaseline())
			: QPoint(st.nameLeft, st.nameTop + st::semiboldFont->ascent)),
	};
}

ArchiveSlide::ArchiveSlide(Fn<void()> update)
: _update(std::move(update)) {
}

void ArchiveSlide::start(
		int from,
		int to,
		ArchiveBand was,
		ArchiveBand now,
		rpl::producer<bool> covered) {
	stop();
	_from = from;
	_to = to;
	_was = std::move(was);
	_now = std::move(now);
	if (!_now.image.isNull()) {
		if (_was.title.isEmpty() || _was.title != _now.title) {
			_now = ArchiveBand();
		} else {
			EraseTitle(_was);
			EraseTitle(_now);
		}
	}
	_waiting = true;
	std::move(
		covered
	) | rpl::filter(!rpl::mappers::_1) | rpl::take(1) | rpl::on_next([=] {
		_waiting = false;
		_animation.start([=] {
			if (!_animation.animating()) {
				_was = ArchiveBand();
				_now = ArchiveBand();
			}
			_update();
		}, _from, _to, st::slideWrapDuration, anim::easeOutCubic);
	}, _lifetime);
}

void ArchiveSlide::stop() {
	_lifetime.destroy();
	_waiting = false;
	_animation.stop();
	_was = ArchiveBand();
	_now = ArchiveBand();
}

int ArchiveSlide::current() const {
	return _animation.animating()
		? int(base::SafeRound(_animation.value(_to)))
		: _waiting
		? _from
		: _to;
}

int ArchiveSlide::shift() const {
	return current() - _to;
}

void ArchiveSlide::paintBand(QPainter &p) const {
	if (_was.image.isNull() || (!_waiting && !_animation.animating())) {
		return;
	}
	const auto now = current();
	if (!_now.image.isNull()) {
		paintMorph(p, now);
		return;
	}
	const auto top = std::min(now - _from, 0);
	if (!_to) {
		p.drawImage(0, top, _was.image);
		return;
	}
	const auto progress = std::abs(now - _from)
		/ float64(std::abs(_to - _from));
	p.setOpacity(1. - progress);
	p.drawImage(0, top, _was.image);
	p.setOpacity(1.);
}

void ArchiveSlide::paintMorph(QPainter &p, int now) const {
	const auto progress = (now - _from) / float64(_to - _from);
	const auto wasTop = (now - _from) / 2;
	const auto nowTop = (now - _to) / 2;
	const auto ratio = _was.image.devicePixelRatio();
	const auto width = int(_was.image.width() / ratio);

	p.save();
	p.setClipRect(0, 0, width, now);
	p.fillRect(0, 0, width, now, st::dialogsBg);
	p.setOpacity(progress);
	p.drawImage(0, nowTop, _now.image);
	p.setOpacity(1. - progress);
	p.drawImage(0, wasTop, _was.image);
	p.setOpacity(1.);

	const auto from = QPointF(_was.titleBaseline + QPoint(0, wasTop));
	const auto to = QPointF(_now.titleBaseline + QPoint(0, nowTop));
	p.setFont(st::semiboldFont);
	p.setPen(st::dialogsNameFg);
	p.drawText(from + (to - from) * progress, _was.title);
	p.restore();
}

} // namespace Dialogs
