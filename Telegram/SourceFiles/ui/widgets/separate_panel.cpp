/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/separate_panel.h"

#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/tooltip.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/layers/layer_widget.h"
#include "styles/style_widgets.h"
#include "styles/style_info.h"
#include "styles/style_calls.h"
#include "logs.h" // #TODO logs

#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>

namespace Ui {

SeparatePanel::SeparatePanel()
: _close(this, st::separatePanelClose)
, _back(this, object_ptr<Ui::IconButton>(this, st::separatePanelBack))
, _body(this) {
	setMouseTracking(true);
	setWindowIcon(QGuiApplication::windowIcon());
	initControls();
	initLayout();
}

void SeparatePanel::setTitle(rpl::producer<QString> title) {
	_title.create(this, std::move(title), st::separatePanelTitle);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_title->show();
	updateTitleGeometry(width());
}

void SeparatePanel::initControls() {
	widthValue(
	) | rpl::start_with_next([=](int width) {
		_back->moveToLeft(_padding.left(), _padding.top());
		_close->moveToRight(_padding.right(), _padding.top());
		if (_title) {
			updateTitleGeometry(width);
		}
	}, lifetime());

	_back->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_titleLeft.start(
			[=] { updateTitlePosition(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _back->lifetime());
	_back->hide(anim::type::instant);
	_titleLeft.stop();
}

void SeparatePanel::updateTitleGeometry(int newWidth) {
	_title->resizeToWidth(newWidth
		- _padding.left() - _back->width()
		- _padding.right() - _close->width());
	updateTitlePosition();
}

void SeparatePanel::updateTitlePosition() {
	if (!_title) {
		return;
	}
	const auto progress = _titleLeft.value(_back->toggled() ? 1. : 0.);
	const auto left = anim::interpolate(
		st::separatePanelTitleLeft,
		_back->width() + st::separatePanelTitleSkip,
		progress);
	_title->moveToLeft(
		_padding.left() + left,
		_padding.top() + st::separatePanelTitleTop);
}

rpl::producer<> SeparatePanel::backRequests() const {
	return rpl::merge(
		_back->entity()->clicks() | rpl::to_empty,
		_synteticBackRequests.events());
}

rpl::producer<> SeparatePanel::closeRequests() const {
	return rpl::merge(
		_close->clicks() | rpl::to_empty,
		_userCloseRequests.events());
}

rpl::producer<> SeparatePanel::closeEvents() const {
	return _closeEvents.events();
}

void SeparatePanel::setBackAllowed(bool allowed) {
	if (allowed != _back->toggled()) {
		_back->toggle(allowed, anim::type::normal);
	}
}

void SeparatePanel::setHideOnDeactivate(bool hideOnDeactivate) {
	_hideOnDeactivate = hideOnDeactivate;
	if (!_hideOnDeactivate) {
		showAndActivate();
	} else if (!isActiveWindow()) {
		LOG(("Export Info: Panel Hide On Inactive Change."));
		hideGetDuration();
	}
}

void SeparatePanel::showAndActivate() {
	toggleOpacityAnimation(true);
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void SeparatePanel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		crl::on_main(this, [=] {
			if (_back->toggled()) {
				_synteticBackRequests.fire({});
			} else {
				_userCloseRequests.fire({});
			}
		});
	}
	return RpWidget::keyPressEvent(e);
}

bool SeparatePanel::eventHook(QEvent *e) {
	if (e->type() == QEvent::WindowDeactivate && _hideOnDeactivate) {
		LOG(("Export Info: Panel Hide On Inactive Window."));
		hideGetDuration();
	}
	return RpWidget::eventHook(e);
}

void SeparatePanel::initLayout() {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::NoDropShadowWindowHint
		| Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	createBorderImage();
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		createBorderImage();
		Ui::ForceFullRepaint(this);
	}, lifetime());

	Ui::Platform::InitOnTopPanel(this);
}

void SeparatePanel::createBorderImage() {
	const auto shadowPadding = st::callShadow.extend;
	const auto cacheSize = st::separatePanelBorderCacheSize;
	auto cache = QImage(
		cacheSize * style::DevicePixelRatio(),
		cacheSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(style::DevicePixelRatio());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = QRect(0, 0, cacheSize, cacheSize).marginsRemoved(
			shadowPadding);
		Ui::Shadow::paint(p, inner, cacheSize, st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::windowBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(
			myrtlrect(inner),
			st::callRadius,
			st::callRadius);
	}
	_borderParts = Ui::PixmapFromImage(std::move(cache));
}

void SeparatePanel::toggleOpacityAnimation(bool visible) {
	if (_visible == visible) {
		return;
	}

	_visible = visible;
	if (_useTransparency) {
		if (_animationCache.isNull()) {
			showControls();
			_animationCache = Ui::GrabWidget(this);
			hideChildren();
		}
		_opacityAnimation.start(
			[this] { opacityCallback(); },
			_visible ? 0. : 1.,
			_visible ? 1. : 0.,
			st::callPanelDuration,
			_visible ? anim::easeOutCirc : anim::easeInCirc);
	}
	if (isHidden() && _visible) {
		show();
	}
}

void SeparatePanel::opacityCallback() {
	update();
	if (!_visible && !_opacityAnimation.animating()) {
		finishAnimating();
	}
}

void SeparatePanel::finishAnimating() {
	_animationCache = QPixmap();
	if (_visible) {
		showControls();
		if (_inner) {
			_inner->setFocus();
		}
	} else {
		finishClose();
	}
}

void SeparatePanel::showControls() {
	showChildren();
	if (!_back->toggled()) {
		_back->setVisible(false);
	}
}

void SeparatePanel::finishClose() {
	hide();
	crl::on_main(this, [=] {
		if (isHidden() && !_visible && !_opacityAnimation.animating()) {
			LOG(("Export Info: Panel Closed."));
			_closeEvents.fire({});
		}
	});
}

int SeparatePanel::hideGetDuration() {
	LOG(("Export Info: Panel Hide Requested."));
	toggleOpacityAnimation(false);
	if (_animationCache.isNull()) {
		finishClose();
		return 0;
	}
	return st::callPanelDuration;
}

void SeparatePanel::showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) {
	ensureLayerCreated();
	_layer->showBox(std::move(box), options, animated);
}

void SeparatePanel::showToast(const TextWithEntities &text) {
	Ui::ShowMultilineToast({
		.parentOverride = this,
		.text = text,
	});
}

void SeparatePanel::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer = base::make_unique_q<Ui::LayerStackWidget>(_body);
	_layer->setHideByBackgroundClick(false);
	_layer->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_layer->resize(size);
	}, _layer->lifetime());
	_layer->hideFinishEvents(
	) | rpl::filter([=] {
		return _layer != nullptr; // Last hide finish is sent from destructor.
	}) | rpl::start_with_next([=] {
		destroyLayer();
	}, _layer->lifetime());
}

void SeparatePanel::destroyLayer() {
	if (!_layer) {
		return;
	}

	auto layer = base::take(_layer);
	const auto resetFocus = Ui::InFocusChain(layer);
	if (resetFocus) {
		setFocus();
	}
	layer = nullptr;
}

void SeparatePanel::showInner(base::unique_qptr<Ui::RpWidget> inner) {
	Expects(!size().isEmpty());

	_inner = std::move(inner);
	_inner->setParent(_body);
	_inner->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_inner->resize(size);
	}, _inner->lifetime());
	_inner->show();

	if (_layer) {
		_layer->raise();
	}

	showAndActivate();
}

void SeparatePanel::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		if (_layer) {
			_layer->setInnerFocus();
		} else if (_inner && !_inner->isHidden()) {
			_inner->setFocus();
		}
	});
}

void SeparatePanel::setInnerSize(QSize size) {
	Expects(!size.isEmpty());

	if (rect().isEmpty()) {
		initGeometry(size);
	} else {
		updateGeometry(size);
	}
}

QRect SeparatePanel::innerGeometry() const {
	return _body->geometry();
}

void SeparatePanel::initGeometry(QSize size) {
	const auto active = QApplication::activeWindow();
	const auto available = !active
		? QGuiApplication::primaryScreen()->availableGeometry()
		: active->windowHandle()->screen()->availableGeometry();
	const auto parentGeometry = (active
		&& active->isVisible()
		&& active->isActiveWindow())
		? active->geometry()
		: available;

	auto center = parentGeometry.center();
	if (size.height() > available.height()) {
		size = QSize(size.width(), available.height());
	}
	if (center.x() + size.width() / 2
		> available.x() + available.width()) {
		center.setX(
			available.x() + available.width() - size.width() / 2);
	}
	if (center.x() - size.width() / 2 < available.x()) {
		center.setX(available.x() + size.width() / 2);
	}
	if (center.y() + size.height() / 2
		> available.y() + available.height()) {
		center.setY(
			available.y() + available.height() - size.height() / 2);
	}
	if (center.y() - size.height() / 2 < available.y()) {
		center.setY(available.y() + size.height() / 2);
	}
	_useTransparency = Ui::Platform::TranslucentWindowsSupported(center);
	_padding = _useTransparency
		? st::callShadow.extend
		: style::margins(
			st::lineWidth,
			st::lineWidth,
			st::lineWidth,
			st::lineWidth);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	const auto rect = [&] {
		const QRect initRect(QPoint(), size);
		return initRect.translated(center - initRect.center()).marginsAdded(_padding);
	}();
	setGeometry(rect);
	setMinimumSize(rect.size());
	setMaximumSize(rect.size());
	updateControlsGeometry();
}

void SeparatePanel::updateGeometry(QSize size) {
	const auto rect = QRect(
		x(),
		y(),
		_padding.left() + size.width() + _padding.right(),
		_padding.top() + size.height() + _padding.bottom());
	setGeometry(rect);
	setMinimumSize(rect.size());
	setMaximumSize(rect.size());
	updateControlsGeometry();
	update();
}

void SeparatePanel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void SeparatePanel::updateControlsGeometry() {
	const auto top = _padding.top() + st::separatePanelTitleHeight;
	_body->setGeometry(
		_padding.left(),
		top,
		width() - _padding.left() - _padding.right(),
		height() - top - _padding.bottom());
}

void SeparatePanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.value(_visible ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimating();
			if (isHidden()) return;
		} else {
			p.setOpacity(opacity);

			PainterHighQualityEnabler hq(p);
			auto marginRatio = (1. - opacity) / 5;
			auto marginWidth = qRound(width() * marginRatio);
			auto marginHeight = qRound(height() * marginRatio);
			p.drawPixmap(
				rect().marginsRemoved(
					QMargins(
						marginWidth,
						marginHeight,
						marginWidth,
						marginHeight)),
				_animationCache,
				QRect(QPoint(0, 0), _animationCache.size()));
			return;
		}
	}

	if (_useTransparency) {
		paintShadowBorder(p);
	} else {
		paintOpaqueBorder(p);
	}
}

void SeparatePanel::paintShadowBorder(Painter &p) const {
	const auto factor = style::DevicePixelRatio();
	const auto size = st::separatePanelBorderCacheSize;
	const auto part1 = size / 3;
	const auto part2 = size - part1;
	const auto corner = QSize(part1, part1) * factor;

	const auto topleft = QRect(QPoint(0, 0), corner);
	p.drawPixmap(QRect(0, 0, part1, part1), _borderParts, topleft);

	const auto topright = QRect(QPoint(part2, 0) * factor, corner);
	p.drawPixmap(
		QRect(width() - part1, 0, part1, part1),
		_borderParts,
		topright);

	const auto bottomleft = QRect(QPoint(0, part2) * factor, corner);
	p.drawPixmap(
		QRect(0, height() - part1, part1, part1),
		_borderParts,
		bottomleft);

	const auto bottomright = QRect(QPoint(part2, part2) * factor, corner);
	p.drawPixmap(
		QRect(width() - part1, height() - part1, part1, part1),
		_borderParts,
		bottomright);

	const auto left = QRect(
		QPoint(0, part1) * factor,
		QSize(_padding.left(), part2 - part1) * factor);
	p.drawPixmap(
		QRect(0, part1, _padding.left(), height() - 2 * part1),
		_borderParts,
		left);

	const auto top = QRect(
		QPoint(part1, 0) * factor,
		QSize(part2 - part1, _padding.top() + st::callRadius) * factor);
	p.drawPixmap(
		QRect(
			part1,
			0,
			width() - 2 * part1,
			_padding.top() + st::callRadius),
		_borderParts,
		top);

	const auto right = QRect(
		QPoint(size - _padding.right(), part1) * factor,
		QSize(_padding.right(), part2 - part1) * factor);
	p.drawPixmap(
		QRect(
			width() - _padding.right(),
			part1,
			_padding.right(),
			height() - 2 * part1),
		_borderParts,
		right);

	const auto bottom = QRect(
		QPoint(part1, size - _padding.bottom() - st::callRadius) * factor,
		QSize(part2 - part1, _padding.bottom() + st::callRadius) * factor);
	p.drawPixmap(
		QRect(
			part1,
			height() - _padding.bottom() - st::callRadius,
			width() - 2 * part1,
			_padding.bottom() + st::callRadius),
		_borderParts,
		bottom);

	p.fillRect(
		_padding.left(),
		_padding.top() + st::callRadius,
		width() - _padding.left() - _padding.right(),
		height() - _padding.top() - _padding.bottom() - 2 * st::callRadius,
		st::windowBg);
}

void SeparatePanel::paintOpaqueBorder(Painter &p) const {
	const auto border = st::windowShadowFgFallback;
	p.fillRect(0, 0, width(), _padding.top(), border);
	p.fillRect(
		myrtlrect(
			0,
			_padding.top(),
			_padding.left(),
			height() - _padding.top()),
		border);
	p.fillRect(
		myrtlrect(
			width() - _padding.right(),
			_padding.top(),
			_padding.right(),
			height() - _padding.top()),
		border);
	p.fillRect(
		_padding.left(),
		height() - _padding.bottom(),
		width() - _padding.left() - _padding.right(),
		_padding.bottom(),
		border);

	p.fillRect(
		_padding.left(),
		_padding.top(),
		width() - _padding.left() - _padding.right(),
		height() - _padding.top() - _padding.bottom(),
		st::windowBg);
}

void SeparatePanel::closeEvent(QCloseEvent *e) {
	e->ignore();
	_userCloseRequests.fire({});
}

void SeparatePanel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(
		_padding.left(),
		_padding.top(),
		width() - _padding.left() - _padding.right(),
		st::separatePanelTitleHeight);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			const auto dragViaSystem = [&] {
				if (windowHandle()->startSystemMove()) {
					return true;
				}
				return false;
			}();
			if (!dragViaSystem) {
				_dragging = true;
				_dragStartMousePosition = e->globalPos();
				_dragStartMyPosition = QPoint(x(), y());
			}
		} else if (!rect().contains(e->pos()) && _hideOnDeactivate) {
			LOG(("Export Info: Panel Hide On Click."));
			hideGetDuration();
		}
	}
}

void SeparatePanel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition
				+ (e->globalPos() - _dragStartMousePosition));
		}
	}
}

void SeparatePanel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _dragging) {
		_dragging = false;
	}
}

void SeparatePanel::leaveEventHook(QEvent *e) {
	Ui::Tooltip::Hide();
}

void SeparatePanel::leaveToChildEvent(QEvent *e, QWidget *child) {
	Ui::Tooltip::Hide();
}

} // namespace Ui
