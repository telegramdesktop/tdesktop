/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_form.h"
#include "passport/passport_panel_password.h"
#include "window/main_window.h"
#include "platform/platform_specific.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "window/layer_widget.h"
#include "messenger.h"
#include "styles/style_passport.h"
#include "styles/style_widgets.h"
#include "styles/style_calls.h"

namespace Passport {

Panel::Panel(not_null<PanelController*> controller)
: _controller(controller)
, _close(this, st::passportPanelClose)
, _title(
	this,
	lang(lng_passport_title),
	Ui::FlatLabel::InitType::Simple,
	st::passportPanelTitle)
, _back(this, object_ptr<Ui::IconButton>(this, st::passportPanelBack))
, _body(this) {
	setMouseTracking(true);
	setWindowIcon(Window::CreateIcon());
	initControls();
	initLayout();
}

void Panel::initControls() {
	widthValue(
	) | rpl::start_with_next([=](int width) {
		_back->moveToLeft(_padding.left(), _padding.top());
		_close->moveToRight(_padding.right(), _padding.top());
		_title->resizeToWidth(width
			- _padding.left() - _back->width()
			- _padding.right() - _close->width());
		updateTitlePosition();
	}, lifetime());

	_close->addClickHandler([=] {
		hideAndDestroy();
	});

	_back->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_titleLeft.start(
			[=] { updateTitlePosition(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _title->lifetime());
	_back->hide(anim::type::instant);
	_titleLeft.finish();

	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Panel::updateTitlePosition() {
	const auto progress = _titleLeft.current(_back->toggled() ? 1. : 0.);
	const auto left = anim::interpolate(
		st::passportPanelTitleLeft,
		_back->width() + st::passportPanelTitleSkip,
		progress);
	_title->moveToLeft(
		_padding.left() + left,
		_padding.top() + st::passportPanelTitleTop);

}

rpl::producer<> Panel::backRequests() const {
	return rpl::merge(
		_back->entity()->clicks(),
		_synteticBackRequests.events());
}

void Panel::setBackAllowed(bool allowed) {
	if (allowed != _back->toggled()) {
		_back->toggle(allowed, anim::type::normal);
	}
}

void Panel::showAndActivate() {
	toggleOpacityAnimation(true);
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void Panel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && _back->toggled()) {
		_synteticBackRequests.fire({});
	}
	return RpWidget::keyPressEvent(e);
}

void Panel::initLayout() {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::BypassWindowManagerHint
		| Qt::NoDropShadowWindowHint
		| Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	initGeometry();
	createBorderImage();

	Platform::InitOnTopPanel(this);
}

void Panel::createBorderImage() {
	if (!_useTransparency || !_borderParts.isNull()) {
		return;
	}
	const auto cacheSize = st::passportPanelBorderCacheSize;
	auto cache = QImage(
		cacheSize * cIntRetinaFactor(),
		cacheSize * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = QRect(0, 0, cacheSize, cacheSize).marginsRemoved(
			_padding);
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::windowBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(
			myrtlrect(inner),
			st::callRadius,
			st::callRadius);
	}
	_borderParts = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::toggleOpacityAnimation(bool visible) {
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

void Panel::opacityCallback() {
	update();
	if (!_visible && !_opacityAnimation.animating()) {
		finishAnimating();
	}
}

void Panel::finishAnimating() {
	_animationCache = QPixmap();
	if (_visible) {
		showControls();
		_inner->setFocus();
	} else {
		destroyDelayed();
	}
}

void Panel::showControls() {
	showChildren();
	if (!_back->toggled()) {
		_back->setVisible(false);
	}
}

void Panel::destroyDelayed() {
	hide();
	_controller->cancelAuth();
}

void Panel::hideAndDestroy() {
	toggleOpacityAnimation(false);
	if (_animationCache.isNull()) {
		destroyDelayed();
	}
}

void Panel::showAskPassword() {
	showInner(base::make_unique_q<PanelAskPassword>(_body, _controller));
	setBackAllowed(false);
}

void Panel::showNoPassword() {
	showInner(base::make_unique_q<PanelNoPassword>(_body, _controller));
	setBackAllowed(false);
}

void Panel::showPasswordUnconfirmed() {
	showInner(
		base::make_unique_q<PanelPasswordUnconfirmed>(_body, _controller));
	setBackAllowed(false);
}

void Panel::showForm() {
	showInner(base::make_unique_q<PanelForm>(_body, _controller));
	setBackAllowed(false);
}

void Panel::showEditValue(object_ptr<Ui::RpWidget> from) {
	showInner(base::unique_qptr<Ui::RpWidget>(from.data()));
}

void Panel::showBox(object_ptr<BoxContent> box) {
	ensureLayerCreated();
	_layer->showBox(
		std::move(box),
		LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer.create(_body);
	_layer->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_layer->resize(size);
	}, _layer->lifetime());
	_layer->hideFinishEvents(
	) | rpl::start_with_next([=, pointer = _layer.data()]{
		if (_layer != pointer) {
			return;
		}
		auto saved = std::exchange(_layer, nullptr);
		if (Ui::InFocusChain(saved)) {
			setFocus();
		}
		saved.destroyDelayed();
	}, _layer->lifetime());
}

void Panel::showInner(base::unique_qptr<Ui::RpWidget> inner) {
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

void Panel::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		if (_layer) {
			_layer->setInnerFocus();
		} else if (!_inner->isHidden()) {
			_inner->setFocus();
		}
	});
}

void Panel::initGeometry() {
	const auto center = Messenger::Instance().getPointForCallPanelCenter();
	_useTransparency = Platform::TranslucentWindowsSupported(center);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	_padding = _useTransparency
		? st::callShadow.extend
		: style::margins(
			st::lineWidth,
			st::lineWidth,
			st::lineWidth,
			st::lineWidth);
	const auto screen = QApplication::desktop()->screenGeometry(center);
	const auto rect = QRect(
		0,
		0,
		st::passportPanelWidth,
		st::passportPanelHeight);
	setGeometry(
		rect.translated(center - rect.center()).marginsAdded(_padding));
	updateControlsGeometry();
}

void Panel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Panel::updateControlsGeometry() {
	const auto top = _padding.top() + st::passportPanelTitleHeight;
	_body->setGeometry(
		_padding.left(),
		top,
		width() - _padding.left() - _padding.right(),
		height() - top - _padding.bottom());
}

void Panel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.current(
			getms(),
			_visible ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimating();
			if (isHidden()) return;
		} else {
			Platform::StartTranslucentPaint(p, e);
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
		Platform::StartTranslucentPaint(p, e);
		paintShadowBorder(p);
	} else {
		paintOpaqueBorder(p);
	}
}

void Panel::paintShadowBorder(Painter &p) const {
	const auto factor = cIntRetinaFactor();
	const auto size = st::passportPanelBorderCacheSize;
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

void Panel::paintOpaqueBorder(Painter &p) const {
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

void Panel::closeEvent(QCloseEvent *e) {
	// #TODO
}

void Panel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(
		_padding.left(),
		_padding.top(),
		width() - _padding.left() - _padding.right(),
		st::passportPanelTitleHeight);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			_dragging = true;
			_dragStartMousePosition = e->globalPos();
			_dragStartMyPosition = QPoint(x(), y());
		} else if (!rect().contains(e->pos())) {
		}
	}
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition
				+ (e->globalPos() - _dragStartMousePosition));
		}
	}
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = false;
	}
}

void Panel::leaveEventHook(QEvent *e) {
}

void Panel::leaveToChildEvent(QEvent *e, QWidget *child) {
}

} // namespace Passport
