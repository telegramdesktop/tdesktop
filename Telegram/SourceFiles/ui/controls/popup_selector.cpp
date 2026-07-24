// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "ui/controls/popup_selector.h"

#include "ui/painter.h"
#include "ui/style/style_core.h"
#include "ui/effects/animation_value.h"
#include "ui/platform/ui_platform_utility.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"

#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <private/qapplication_p.h>

namespace Ui {

PopupSelector::PopupSelector(
	not_null<QWidget*> parent,
	QSize size,
	PopupAppearType appearType)
: RpWidget(parent)
, _innerSize(size)
, _cachedRound(
	size,
	st::reactionCornerShadow,
	std::min(size.width(), size.height()))
, _appearType(appearType) {
	_useTransparency = Platform::TranslucentWindowsSupported();
	const auto margins = marginsForShadow();
	resize(size + QSize(
		margins.left() + margins.right(),
		margins.top() + margins.bottom()));

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::BypassWindowManagerHint
		| Qt::Popup
		| Qt::NoDropShadowWindowHint);
	setMouseTracking(true);
	setAttribute(Qt::WA_NoSystemBackground, true);

	if (_useTransparency) {
		setAttribute(Qt::WA_TranslucentBackground, true);
	} else {
		setAttribute(Qt::WA_TranslucentBackground, false);
		setAttribute(Qt::WA_OpaquePaintEvent, true);
	}

	installEventFilter(this);

	hide();
}

void PopupSelector::popup(const QPoint &p) {
	const auto screen = QGuiApplication::screenAt(p);
	createWinId();
	windowHandle()->removeEventFilter(this);
	windowHandle()->installEventFilter(this);
	if (screen) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		setScreen(screen);
#else
		windowHandle()->setScreen(screen);
#endif
	}

	auto w = p;
	const auto r = screen ? screen->availableGeometry() : QRect();
	if (!r.isNull()) {
		if (w.x() + width() > r.x() + r.width()) {
			w.setX(r.x() + r.width() - width());
		}
		if (w.x() < r.x()) {
			w.setX(r.x());
		}
		if (w.y() + height() > r.y() + r.height()) {
			w.setY(r.y() + r.height() - height());
		}
		if (w.y() < r.y()) {
			w.setY(r.y());
		}
	}
	move(w);
	show();
	Platform::ShowOverAll(this);
	raise();
	activateWindow();
}

QMargins PopupSelector::marginsForShadow() const {
	const auto line = st::lineWidth;
	return _useTransparency
		? st::reactionCornerShadow
		: QMargins(line, line, line, line);
}

void PopupSelector::updateShowState(
		float64 progress,
		float64 opacity,
		bool appearing) {
	if (_appearing && !appearing && !_paintBuffer.isNull()) {
		paintBackgroundToBuffer();
	}
	_appearing = appearing;
	_appearProgress = progress;
	_appearOpacity = opacity;
	if (_appearing && isHidden()) {
		show();
	} else if (!_appearing && _appearOpacity == 0. && !isHidden()) {
		hide();
	}
	update();
}

void PopupSelector::hideAnimated() {
	if (/*!isHidden() && */!_hiding) {
		_hiding = true;
		_hideAnimation.start([=] {
			const auto progress = 1. - _hideAnimation.value(0.);
			updateShowState(progress, progress, false);
			if (!_hideAnimation.animating()) {
				_hiding = false;
				if (_hideFinishedCallback) {
					_hideFinishedCallback();
				}
			}
		}, _appearProgress, 0., st::defaultPopupMenu.duration);
	}
}

void PopupSelector::setHideFinishedCallback(Fn<void()> callback) {
	_hideFinishedCallback = std::move(callback);
}

void PopupSelector::paintBackgroundToBuffer() {
	const auto factor = style::DevicePixelRatio();
	if (_paintBuffer.size() != size() * factor) {
		_paintBuffer = _cachedRound.PrepareImage(size());
	}
	_paintBuffer.fill(Qt::transparent);

	_cachedRound.setBackgroundColor(st::boxBg->c);
	_cachedRound.setShadowColor(st::shadowFg->c);

	auto p = QPainter(&_paintBuffer);
	const auto radius = std::min(_innerSize.width(), _innerSize.height()) / 2.;
	const auto frame = _cachedRound.validateFrame(0, 1., radius);
	const auto fill = _cachedRound.FillWithImage(p, rect(), frame);
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::boxBg);
	}
}

void PopupSelector::paintAppearing(QPainter &p) {
	p.setOpacity(_appearOpacity);
	const auto factor = style::DevicePixelRatio();
	if (_paintBuffer.size() != size() * factor) {
		_paintBuffer = _cachedRound.PrepareImage(size());
	}
	_paintBuffer.fill(st::boxBg->c);

	auto q = QPainter(&_paintBuffer);
	const auto margins = marginsForShadow();

	if (_appearType == PopupAppearType::CenterExpand) {
		const auto appearedWidth = anim::interpolate(
			_innerSize.height(),
			_innerSize.width(),
			_appearProgress);
		const auto fullWidth = margins.left() + appearedWidth + margins.right();
		const auto drawSize = QSize(fullWidth, height());
		const auto offsetX = (width() - fullWidth) / 2;

		_cachedRound.setBackgroundColor(st::boxBg->c);
		_cachedRound.setShadowColor(st::shadowFg->c);
		const auto radius
			= std::min(_innerSize.width(), _innerSize.height()) / 2.;
		_cachedRound.overlayExpandedBorder(
			q,
			drawSize,
			_appearProgress,
			radius,
			radius,
			1.);
		q.end();

		p.drawImage(
			QRect(QPoint(offsetX, 0), drawSize),
			_paintBuffer,
			QRect(QPoint(), drawSize * factor));
	} else if (_appearType == PopupAppearType::RightToLeft) {
		const auto appearedWidth = anim::interpolate(
			_innerSize.height(),
			_innerSize.width(),
			_appearProgress);
		const auto fullWidth = margins.left()
			+ appearedWidth
			+ margins.right();
		const auto drawSize = QSize(fullWidth, height());
		const auto offsetX = width() - fullWidth;

		_cachedRound.setBackgroundColor(st::boxBg->c);
		_cachedRound.setShadowColor(st::shadowFg->c);
		const auto radius
			= std::min(_innerSize.width(), _innerSize.height()) / 2.;
		_cachedRound.overlayExpandedBorder(
			q,
			drawSize,
			_appearProgress,
			radius,
			radius,
			1.);
		q.end();

		p.drawImage(
			QRect(QPoint(offsetX, 0), drawSize),
			_paintBuffer,
			QRect(QPoint(), drawSize * factor));
	} else {
		const auto appearedWidth = anim::interpolate(
			_innerSize.height(),
			_innerSize.width(),
			_appearProgress);
		const auto fullWidth = margins.left() + appearedWidth + margins.right();
		const auto drawSize = QSize(fullWidth, height());

		_cachedRound.setBackgroundColor(st::boxBg->c);
		_cachedRound.setShadowColor(st::shadowFg->c);
		const auto radius
			= std::min(_innerSize.width(), _innerSize.height()) / 2.;
		_cachedRound.overlayExpandedBorder(
			q,
			drawSize,
			_appearProgress,
			radius,
			radius,
			1.);
		q.end();

	}
}

void PopupSelector::paintCollapsed(QPainter &p) {
	if (_paintBuffer.isNull()) {
		paintBackgroundToBuffer();
	}
	p.drawImage(0, 0, _paintBuffer);
}

void PopupSelector::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	if (!_useTransparency) {
		p.fillRect(rect(), st::boxBg);
	}
	if (_appearing) {
		paintAppearing(p);
	} else {
		paintCollapsed(p);
	}
}

bool PopupSelector::eventFilter(QObject *obj, QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd) {
		if (obj == windowHandle() && isActiveWindow()) {
			const auto event = static_cast<QTouchEvent*>(e);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
			e->setAccepted(
				QApplicationPrivate::translateRawTouchEvent(
					this,
					event->device(),
					event->touchPoints(),
					event->timestamp()));
#elif QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
			e->setAccepted(
				QApplicationPrivate::translateRawTouchEvent(
					this,
					event->pointingDevice(),
					const_cast<QList<QEventPoint> &>(event->points()),
					event->timestamp()));
#else
			e->setAccepted(
				QApplicationPrivate::translateRawTouchEvent(this, event));
#endif
			return e->isAccepted();
		}
	}
	return RpWidget::eventFilter(obj, e);
}

void PopupSelector::mousePressEvent(QMouseEvent *e) {
	if (!rect().contains(e->pos())) {
		hideAnimated();
	}
}

void PopupSelector::focusOutEvent(QFocusEvent *e) {
	hideAnimated();
}

} // namespace Ui
