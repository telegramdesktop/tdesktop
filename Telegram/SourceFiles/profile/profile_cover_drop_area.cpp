/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "profile/profile_cover_drop_area.h"

#include "ui/ui_utility.h"
#include "styles/style_profile.h"

namespace Profile {

CoverDropArea::CoverDropArea(QWidget *parent, const QString &title, const QString &subtitle) : TWidget(parent)
, _title(title)
, _subtitle(subtitle)
, _titleWidth(st::profileDropAreaTitleFont->width(_title))
, _subtitleWidth(st::profileDropAreaSubtitleFont->width(_subtitle)) {
}

void CoverDropArea::showAnimated() {
	show();
	_hiding = false;
	setupAnimation();
}

void CoverDropArea::hideAnimated(HideFinishCallback &&callback) {
	_hideFinishCallback = std::move(callback);
	_hiding = true;
	setupAnimation();
}

void CoverDropArea::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (_a_appearance.animating()) {
		p.setOpacity(_a_appearance.value(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
		return;
	}

	if (!_cache.isNull()) {
		_cache = QPixmap();
		if (_hiding) {
			hide();
			if (_hideFinishCallback) {
				_hideFinishCallback(this);
			}
			return;
		}
	}

	p.fillRect(e->rect(), st::profileDropAreaBg);

	if (width() < st::profileDropAreaPadding.left() + st::profileDropAreaPadding.right()) return;
	if (height() < st::profileDropAreaPadding.top() + st::profileDropAreaPadding.bottom()) return;

	auto border = st::profileDropAreaBorderWidth;
	auto &borderFg = st::profileDropAreaBorderFg;
	auto inner = rect().marginsRemoved(st::profileDropAreaPadding);
	p.fillRect(inner.x(), inner.y(), inner.width(), border, borderFg);
	p.fillRect(inner.x(), inner.y() + inner.height() - border, inner.width(), border, borderFg);
	p.fillRect(inner.x(), inner.y() + border, border, inner.height() - 2 * border, borderFg);
	p.fillRect(inner.x() + inner.width() - border, inner.y() + border, border, inner.height() - 2 * border, borderFg);

	int titleLeft = inner.x() + (inner.width() - _titleWidth) / 2;
	int titleTop = inner.y() + st::profileDropAreaTitleTop + st::profileDropAreaTitleFont->ascent;
	p.setFont(st::profileDropAreaTitleFont);
	p.setPen(st::profileDropAreaFg);
	p.drawText(titleLeft, titleTop, _title);

	int subtitleLeft = inner.x() + (inner.width() - _subtitleWidth) / 2;
	int subtitleTop = inner.y() + st::profileDropAreaSubtitleTop + st::profileDropAreaSubtitleFont->ascent;
	p.setFont(st::profileDropAreaSubtitleFont);
	p.setPen(st::profileDropAreaFg);
	p.drawText(subtitleLeft, subtitleTop, _subtitle);
}

void CoverDropArea::setupAnimation() {
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(this);
	}
	auto from = _hiding ? 1. : 0., to = _hiding ? 0. : 1.;
	_a_appearance.start(
		[=] { update(); },
		from,
		to,
		st::profileDropAreaDuration);
}

} // namespace Profile
