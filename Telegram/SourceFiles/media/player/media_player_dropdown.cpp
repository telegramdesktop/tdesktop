/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_dropdown.h"

#include "ui/cached_round_corners.h"
#include "ui/widgets/shadow.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

namespace Media::Player {

Dropdown::Dropdown(QWidget *parent)
: RpWidget(parent)
, _hideTimer([=] { startHide(); })
, _showTimer([=] { startShow(); }) {
	hide();

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		leaveEvent(nullptr);
	}, lifetime());

	hide();
	auto margin = getMargin();
	resize(margin.left() + st::mediaPlayerVolumeSize.width() + margin.right(), margin.top() + st::mediaPlayerVolumeSize.height() + margin.bottom());
}

QMargins Dropdown::getMargin() const {
	const auto top1 = st::mediaPlayerHeight
		+ st::lineWidth
		- st::mediaPlayerPlayTop
		- st::mediaPlayerVolumeToggle.height;
	const auto top2 = st::mediaPlayerPlayback.fullWidth;
	const auto top = std::max(top1, top2);
	return QMargins(st::mediaPlayerVolumeMargin, top, st::mediaPlayerVolumeMargin, st::mediaPlayerVolumeMargin);
}

bool Dropdown::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	return rect().marginsRemoved(getMargin()).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void Dropdown::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating();
		if (animating) {
			p.setOpacity(_a_appearance.value(_hiding ? 0. : 1.));
		} else if (_hiding || isHidden()) {
			hidingFinished();
			return;
		}
		p.drawPixmap(0, 0, _cache);
		if (!animating) {
			showChildren();
			_cache = QPixmap();
		}
		return;
	}

	// draw shadow
	auto shadowedRect = rect().marginsRemoved(getMargin());
	auto shadowedSides = RectPart::Left | RectPart::Right | RectPart::Bottom;
	Ui::Shadow::paint(p, shadowedRect, width(), st::defaultRoundShadow, shadowedSides);
	const auto &corners = Ui::CachedCornerPixmaps(Ui::MenuCorners);
	const auto fill = Ui::CornersPixmaps{
		.p = { QPixmap(), QPixmap(), corners.p[2], corners.p[3] },
	};
	Ui::FillRoundRect(
		p,
		shadowedRect.x(),
		0,
		shadowedRect.width(),
		shadowedRect.y() + shadowedRect.height(),
		st::menuBg,
		fill);
}

void Dropdown::enterEventHook(QEnterEvent *e) {
	_hideTimer.cancel();
	if (_a_appearance.animating()) {
		startShow();
	} else {
		_showTimer.callOnce(0);
	}
	return RpWidget::enterEventHook(e);
}

void Dropdown::leaveEventHook(QEvent *e) {
	_showTimer.cancel();
	if (_a_appearance.animating()) {
		startHide();
	} else {
		_hideTimer.callOnce(300);
	}
	return RpWidget::leaveEventHook(e);
}

void Dropdown::otherEnter() {
	_hideTimer.cancel();
	if (_a_appearance.animating()) {
		startShow();
	} else {
		_showTimer.callOnce(0);
	}
}

void Dropdown::otherLeave() {
	_showTimer.cancel();
	if (_a_appearance.animating()) {
		startHide();
	} else {
		_hideTimer.callOnce(0);
	}
}

void Dropdown::startShow() {
	if (isHidden()) {
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void Dropdown::startHide() {
	if (_hiding) {
		return;
	}

	_hiding = true;
	startAnimation();
}

void Dropdown::startAnimation() {
	if (_cache.isNull()) {
		showChildren();
		_cache = Ui::GrabWidget(this);
	}
	hideChildren();
	_a_appearance.start(
		[=] { appearanceCallback(); },
		_hiding ? 1. : 0.,
		_hiding ? 0. : 1.,
		st::defaultInnerDropdown.duration);
}

void Dropdown::appearanceCallback() {
	if (!_a_appearance.animating() && _hiding) {
		_hiding = false;
		hidingFinished();
	} else {
		update();
	}
}

void Dropdown::hidingFinished() {
	hide();
	_cache = QPixmap();
}

bool Dropdown::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	}
	return false;
}

} // namespace Media::Player
