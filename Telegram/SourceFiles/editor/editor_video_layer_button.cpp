/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_video_layer_button.h"

#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/effects/ripple_animation.h"
#include "styles/style_editor.h"
#include "ui/ui_utility.h"

#include <QtGui/QPainterPath>

namespace Editor {
namespace {

constexpr auto kTooltipDelay = crl::time(1000);
constexpr auto kBadgeHaloTopLeft = QPointF(12., 11.);
constexpr auto kBadgeHaloSide = 15.;
constexpr auto kIconViewBoxSide = 24.;

} // namespace

VideoLayerButton::VideoLayerButton(QWidget *parent)
: RippleButton(parent, st::photoEditorRotateButton.ripple) {
	resize(Size(st::photoEditorAudioDiscSize));
	setLocked(true, anim::type::instant);

	events(
	) | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Enter) {
			Ui::Tooltip::Show(kTooltipDelay, this);
		} else if (event->type() == QEvent::Leave) {
			Ui::Tooltip::Hide();
		}
	}, lifetime());
}

VideoLayerButton::~VideoLayerButton() {
	Ui::Tooltip::Hide();
}

void VideoLayerButton::setLocked(bool locked, anim::type animated) {
	if (_locked == locked) {
		return;
	}
	_locked = locked;
	Ui::Tooltip::Hide();
	if ((animated == anim::type::instant) || anim::Disabled()) {
		_activeAnimation.stop();
	} else {
		_activeAnimation.start(
			[=] { update(); },
			_locked ? 1. : 0.,
			_locked ? 0. : 1.,
			st::photoEditorToolButtonSelectDuration);
	}
	update();
}

bool VideoLayerButton::locked() const {
	return _locked;
}

QString VideoLayerButton::tooltipText() const {
	return _locked
		? tr::lng_story_video_unlock(tr::now)
		: tr::lng_story_video_lock(tr::now);
}

QPoint VideoLayerButton::tooltipPos() const {
	return QCursor::pos();
}

bool VideoLayerButton::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

void VideoLayerButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto full = QRectF(rect());
	p.setPen(Qt::NoPen);
	p.setBrush(st::roundedBg);
	p.drawEllipse(full);
	paintRipple(p, QPoint());

	const auto activeProgress = _activeAnimation.value(_locked ? 0. : 1.);
	if (activeProgress > 0.) {
		const auto ring = float64(st::photoEditorAudioDiscRing);
		auto ringColor = st::photoEditorAudioDiscRingFg->c;
		ringColor.setAlphaF(ringColor.alphaF() * activeProgress);
		auto pen = QPen(ringColor);
		pen.setWidthF(ring);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(full.marginsRemoved(
			{ ring / 2., ring / 2., ring / 2., ring / 2. }));
		p.setPen(Qt::NoPen);
	}
	const auto &body = isOver()
		? st::photoEditorVideoLayerIconBodyOver
		: st::photoEditorVideoLayerIconBody;
	const auto &badge = isOver()
		? st::photoEditorVideoLayerIconBadgeOver
		: st::photoEditorVideoLayerIconBadge;
	const auto &activeIcon = st::photoEditorVideoLayerIconActive;

	const auto paintLocked = [&](float64 opacity) {
		const auto factor = body.width() / kIconViewBoxSide;
		auto excluded = QPainterPath();
		excluded.addRect(rect());
		auto halo = QPainterPath();
		halo.addEllipse(QRectF(
			QPointF(
				(rect().width() - body.width()) / 2.,
				(rect().height() - body.height()) / 2.)
				+ kBadgeHaloTopLeft * factor,
			QSizeF(kBadgeHaloSide, kBadgeHaloSide) * factor));
		p.save();
		p.setOpacity(opacity);
		p.setClipPath(excluded.subtracted(halo));
		body.paintInCenter(p, rect());
		p.restore();
		p.setOpacity(opacity);
		badge.paintInCenter(p, rect());
		p.setOpacity(1.);
	};

	if (activeProgress <= 0.) {
		paintLocked(1.);
	} else if (activeProgress >= 1.) {
		activeIcon.paintInCenter(p, rect());
	} else {
		paintLocked(1. - activeProgress);
		p.setOpacity(activeProgress);
		activeIcon.paintInCenter(p, rect());
		p.setOpacity(1.);
	}
}

void VideoLayerButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if ((was & StateFlag::Over) != (state() & StateFlag::Over)) {
		update();
	}
}

QImage VideoLayerButton::prepareRippleMask() const {
	return Ui::RippleAnimation::MaskByDrawer(size(), false, [&](QPainter &p) {
		p.drawEllipse(rect());
	});
}

QPoint VideoLayerButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

} // namespace Editor
