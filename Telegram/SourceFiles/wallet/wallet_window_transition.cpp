/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_window_transition.h"

#include "base/event_filter.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/widgets/separate_panel.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/window_palette.h"
#include "wallet/wallet_card_flight.h"
#include "wallet/wallet_palette.h"

#include <QtGui/QWindow>

namespace Wallet {
namespace {

constexpr auto kWindowWaveDuration = crl::time(640);
constexpr auto kSnapEpsilon = 1e-3;

struct Capture {
	QImage image;
	QRect pixels;
};

[[nodiscard]] float64 WindowWaveRadius(
		const QRectF &area,
		QPointF origin,
		float64 cardWidth,
		float64 start,
		float64 progress) {
	const auto corners = std::array{
		area.topLeft(),
		area.topRight(),
		area.bottomLeft(),
		area.bottomRight(),
	};
	auto reach = 0.;
	for (const auto &corner : corners) {
		accumulate_max(reach, QLineF(origin, corner).length());
	}
	reach += CardLookRimInside(cardWidth);
	return CardLookWaveRadius(start, reach, progress);
}

[[nodiscard]] crl::time WindowRevealDuration(CardLookReveal reveal) {
	return (reveal == CardLookReveal::Wave)
		? kWindowWaveDuration
		: CardLookFadeDuration();
}

[[nodiscard]] Capture CaptureWindow(not_null<QWidget*> window) {
	const auto ratio = window->devicePixelRatio();
	const auto pixels = QRectF(
		QPointF(),
		QSizeF(window->size()) * ratio).toAlignedRect();
	if (pixels.isEmpty()) {
		return {};
	}
	auto image = QImage(pixels.size(), QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) {
		return {};
	}
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	Ui::RenderWidget(
		p,
		window,
		QPoint(),
		QRegion(window->rect()),
		QWidget::RenderFlags());
	const auto children = window->children();
	for (const auto child : children) {
		if (!child->isWidgetType()) {
			continue;
		}
		const auto widget = static_cast<QWidget*>(child);
		if (widget->isWindow()
			|| widget->isHidden()
			|| IsTravelLayer(widget)) {
			continue;
		}
		const auto mask = widget->mask();
		const auto region = mask.isEmpty()
			? QRegion(widget->rect())
			: (mask & widget->rect());
		if (region.isEmpty()) {
			continue;
		}
		Ui::RenderWidget(
			p,
			widget,
			widget->pos() + region.boundingRect().topLeft(),
			region,
			QWidget::DrawChildren);
	}
	p.end();
	return { .image = std::move(image), .pixels = pixels };
}

[[nodiscard]] bool Painting(not_null<QWidget*> window) {
	const auto painting = [](const QWidget *widget) {
		return widget->testAttribute(Qt::WA_WState_InPaintEvent);
	};
	return painting(window)
		|| ranges::any_of(window->findChildren<QWidget*>(), painting);
}

[[nodiscard]] auto SuppressUpdates(not_null<QWidget*> window) {
	const auto enabled = window->updatesEnabled();
	if (enabled) {
		window->setUpdatesEnabled(false);
	}
	return gsl::finally([=] {
		if (enabled) {
			window->setUpdatesEnabled(true);
		}
	});
}

[[nodiscard]] std::optional<QPoint> SnappedOffset(const QTransform &device) {
	const auto nearly = [](float64 value, float64 target) {
		return std::abs(value - target) < kSnapEpsilon;
	};
	const auto x = std::round(device.dx());
	const auto y = std::round(device.dy());
	if (!device.isAffine()
		|| !nearly(device.m11(), 1.)
		|| !nearly(device.m22(), 1.)
		|| !nearly(device.m12(), 0.)
		|| !nearly(device.m21(), 0.)
		|| !nearly(device.dx(), x)
		|| !nearly(device.dy(), y)) {
		return std::nullopt;
	}
	return QPoint(int(x), int(y));
}

void DrawMapped(QPainter &p, const QTransform &map, const QImage &image) {
	const auto snapped = SnappedOffset(map * p.deviceTransform());
	if (snapped) {
		DrawDevicePixels(p, QRect(*snapped, image.size()), image);
		return;
	}
	const auto source = QRect(QPoint(), image.size());
	p.save();
	p.setTransform(map, true);
	p.drawImage(source, image, source);
	p.restore();
}

} // namespace

WindowLookReveal::WindowLookReveal(WindowLookRevealArgs &&args)
: _panel(args.panel)
, _body(args.body)
, _palette(std::move(args.palette))
, _fold(std::move(args.fold))
, _apply(std::move(args.apply))
, _animation([=](crl::time now) { return tick(now); }) {
	base::install_event_filter(_panel, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::ChildAdded:
			if (_overlay
				&& watch(static_cast<QChildEvent*>(e.get())->child())) {
				restack();
			}
			break;
		case QEvent::Hide:
		case QEvent::WindowStateChange:
			if (hidden()) {
				settle();
			}
			break;
		case QEvent::Resize:
			if (_overlay) {
				updateGeometry();
			}
			break;
		default: break;
		}
		return base::EventFilterResult::Continue;
	}, _lifetime);
	base::install_event_filter(_body, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Show && _pending) {
			crl::on_main(this, [=] { flush(); });
		}
		return base::EventFilterResult::Continue;
	}, _lifetime);
	style::PaletteChanged() | rpl::on_next([=] {
		finish();
	}, _lifetime);
	anim::Disables() | rpl::filter([](bool disabled) {
		return disabled;
	}) | rpl::on_next([=] {
		settle();
	}, _lifetime);
}

WindowLookReveal::~WindowLookReveal() {
	_lifetime.destroy();
	_animation.stop();
	dropOverlay();
}

void WindowLookReveal::apply(CardLook look, CardLookChange change) {
	if (!_palette->resolve(false)) {
		_pending.reset();
		finish();
		_palette->setLook(look);
		_apply(std::move(look), change);
	} else if (change.reveal == CardLookReveal::Instant
		|| anim::Disabled()
		|| hidden()) {
		instant(std::move(look));
	} else if (!live()) {
		_pending = Pending{ .look = std::move(look), .change = change };
	} else {
		_pending.reset();
		animate(std::move(look), change);
	}
}

rpl::producer<bool> WindowLookReveal::runningValue() const {
	return _running.value();
}

QRect WindowLookReveal::area() const {
	const auto full = _panel->rect();
	if (_panel->testAttribute(Qt::WA_OpaquePaintEvent)) {
		return full;
	}
	const auto client = full.marginsRemoved(_panel->computePadding());
	// Scaled border parts reach a device pixel past a fractional padding.
	return client.marginsAdded({ 1, 1, 1, 1 }).intersected(full);
}

bool WindowLookReveal::hidden() const {
	return !_panel->isVisible() || _panel->isMinimized();
}

bool WindowLookReveal::live() const {
	return !hidden() && _body->isVisible() && !area().isEmpty();
}

std::optional<WindowLookReveal::Source> WindowLookReveal::source(
		const CardLookChange &change) const {
	const auto fold = _fold();
	const auto &rest = fold.rest;
	if (rest.isEmpty()) {
		return std::nullopt;
	}
	const auto point = CardFoldLimitPoint(fold, QPointF(
		change.origin.x() * rest.width(),
		change.origin.y() * rest.height()));
	if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
		return std::nullopt;
	}
	return Source{
		.origin = point + QPointF(Ui::MapFrom(_panel, _body, QPoint())),
		.width = float64(rest.width()),
		.start = CardLookWaveStart(change, rest.size()),
	};
}

QTransform WindowLookReveal::reprojection() const {
	const auto now = QRectF(area());
	const auto was = QRectF(_area);
	if (now == was || was.isEmpty()) {
		return QTransform();
	}
	return QTransform::fromTranslate(-was.x(), -was.y())
		* QTransform::fromScale(
			now.width() / was.width(),
			now.height() / was.height())
		* QTransform::fromTranslate(now.x(), now.y());
}

QTransform WindowLookReveal::imageMap(QPointF shift) const {
	const auto ratio = _image.devicePixelRatio();
	return QTransform::fromScale(1. / ratio, 1. / ratio)
		* QTransform::fromTranslate(_imageRect.x(), _imageRect.y())
		* reprojection()
		* QTransform::fromTranslate(-shift.x(), -shift.y());
}

float64 WindowLookReveal::progress() const {
	const auto duration = WindowRevealDuration(_reveal);
	return std::clamp((_now - _started) / float64(duration), 0., 1.);
}

std::optional<QRect> WindowLookReveal::hole() const {
	if (_reveal != CardLookReveal::Wave
		|| !reprojection().isIdentity()
		|| _image.devicePixelRatio() != _panel->devicePixelRatio()) {
		return std::nullopt;
	}
	const auto area = this->area();
	const auto radius = WindowWaveRadius(
		QRectF(area),
		_source.origin,
		_source.width,
		_source.start,
		progress());
	const auto outer = radius + CardLookRimOutside(_source.width) + 1.;
	const auto result = QRectF(
		_source.origin - QPointF(outer, outer),
		QSizeF(2. * outer, 2. * outer)
	).toAlignedRect().intersected(area);
	return (result == area) ? std::nullopt : std::make_optional(result);
}

void WindowLookReveal::instant(CardLook look) {
	_pending.reset();
	finish();
	_palette->setLook(look);
	_apply(std::move(look), {});
	if (!_panel->isHidden()) {
		Ui::ForceFullPaletteRepaint(_panel);
	}
	Ui::UpdateWindowPaletteChildren(_panel);
}

void WindowLookReveal::animate(CardLook look, CardLookChange change) {
	const auto started = crl::now();
	const auto area = this->area();
	const auto wave = (change.reveal == CardLookReveal::Wave)
		? source(change)
		: std::nullopt;
	auto captured = CaptureWindow(_panel);
	if (captured.image.isNull()) {
		_pending = Pending{ .look = std::move(look), .change = change };
		return;
	}
	_palette->holdChildren();
	{
		const auto updates = SuppressUpdates(_panel);
		install(
			std::move(captured.image),
			captured.pixels,
			area,
			wave,
			started);
		_palette->setLook(look);
		_apply(std::move(look), {});
	}
	if (tick(crl::now())) {
		_animation.start();
		_firstFramePending = true;
		if (!std::exchange(_firstFrameQueued, true)) {
			Ui::PostponeCall(this, [=] {
				_firstFrameQueued = false;
				paintFirstFrame();
			});
		}
	}
}

void WindowLookReveal::paintFirstFrame() {
	const auto window = _panel->windowHandle();
	if (!base::take(_firstFramePending)
		|| !_overlay
		|| !live()
		|| !window
		|| !window->isExposed()
		|| Painting(_panel)) {
		return;
	}
	const auto hole = _band->geometry();
	_panel->repaint(hole.isEmpty() ? _panel->rect() : hole);
}

void WindowLookReveal::install(
		QImage image,
		QRect pixels,
		QRect area,
		std::optional<Source> source,
		crl::time started) {
	const auto ratio = image.devicePixelRatio();
	_image = std::move(image);
	_imageRect = QRectF(
		QPointF(pixels.topLeft()) / ratio,
		QSizeF(pixels.size()) / ratio);
	_area = area;
	_source = source.value_or(Source());
	_reveal = source ? CardLookReveal::Wave : CardLookReveal::Fade;
	_started = _now = started;
	if (!_overlay) {
		_band = base::make_unique_q<Ui::RpWidget>(_panel);
		_band->setAttribute(Qt::WA_TransparentForMouseEvents);
		_band->paintRequest() | rpl::on_next([=] {
			_firstFramePending = false;
			auto p = QPainter(_band.get());
			compose(p, _band.get());
		}, _overlayLifetime);
		_overlay = base::make_unique_q<Ui::RpWidget>(_panel);
		_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
		_overlay->paintRequest() | rpl::on_next([=] {
			_firstFramePending = false;
			auto p = QPainter(_overlay.get());
			paint(p);
		}, _overlayLifetime);
		for (const auto child : _panel->children()) {
			watch(child);
		}
	}
	updateGeometry();
	_band->show();
	_overlay->show();
	restack();
	_running = true;
}

void WindowLookReveal::settle() {
	if (auto pending = base::take(_pending)) {
		instant(std::move(pending->look));
	} else {
		finish();
	}
}

void WindowLookReveal::flush() {
	if (_pending && live()) {
		auto pending = *base::take(_pending);
		apply(std::move(pending.look), pending.change);
	}
}

void WindowLookReveal::finish() {
	if (!_overlay) {
		return;
	}
	_animation.stop();
	dropOverlay();
	_image = QImage();
	_running = false;
	_panel->update();
	_palette->releaseChildren();
}

void WindowLookReveal::dropOverlay() {
	_overlayLifetime.destroy();
	for (const auto widget : { _overlay.release(), _band.release() }) {
		if (widget) {
			widget->hide();
			widget->deleteLater();
		}
	}
}

void WindowLookReveal::updateGeometry() {
	_overlay->setGeometry(reprojection().isIdentity()
		? _panel->rect()
		: area());
	_overlay->update();
	refresh();
}

void WindowLookReveal::refresh() {
	const auto hole = this->hole();
	if (_overlay->testAttribute(Qt::WA_OpaquePaintEvent) != bool(hole)) {
		_overlay->setAttribute(Qt::WA_OpaquePaintEvent, bool(hole));
		_overlay->update();
	}
	if (!hole) {
		_overlay->clearMask();
		_overlay->update();
		_band->setGeometry(QRect());
		return;
	}
	_band->setGeometry(*hole);
	_band->update();
	_overlay->setMask(QRegion(_overlay->rect()) - *hole);
}

void WindowLookReveal::restack() {
	if (!_overlay || _restacking) {
		return;
	}
	_restacking = true;
	_overlay->raise();
	_band->raise();
	const auto children = _panel->children();
	for (const auto child : children) {
		if (child->isWidgetType()) {
			const auto widget = static_cast<QWidget*>(child);
			if (!widget->isWindow() && IsTravelLayer(widget)) {
				widget->raise();
			}
		}
	}
	_restacking = false;
}

bool WindowLookReveal::watch(not_null<QObject*> child) {
	if (!child->isWidgetType()
		|| child.get() == _overlay.get()
		|| child.get() == _band.get()) {
		return false;
	}
	const auto widget = static_cast<QWidget*>(child.get());
	if (widget->isWindow()) {
		return false;
	}
	base::install_event_filter(widget, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		// WHY: the panel's own menu is added as a plain child and made a
		// window later, outside Wallet code; prepare its show animation
		// again under the held palette before its first frame.
		if (widget->isWindow()) {
			if (type == QEvent::Show) {
				Ui::RefreshWindowPaletteChild(widget);
			}
		} else if (type == QEvent::Show || type == QEvent::ZOrderChange) {
			restack();
		}
		return base::EventFilterResult::Continue;
	}, _overlayLifetime);
	return true;
}

bool WindowLookReveal::tick(crl::time now) {
	_now = now;
	if (!_overlay || progress() >= 1.) {
		finish();
		return false;
	}
	refresh();
	return true;
}

void WindowLookReveal::paint(QPainter &p) const {
	if (!_overlay->testAttribute(Qt::WA_OpaquePaintEvent)) {
		compose(p, _overlay.get());
	} else if (!_image.isNull()) {
		p.setCompositionMode(QPainter::CompositionMode_Source);
		DrawMapped(p, imageMap(QPointF(_overlay->pos())), _image);
	}
}

void WindowLookReveal::compose(
		QPainter &p,
		not_null<QWidget*> widget) const {
	if (_image.isNull()) {
		return;
	}
	const auto map = reprojection();
	const auto shift = QPointF(widget->pos());
	const auto image = imageMap(shift);
	const auto progress = this->progress();
	const auto wave = (_reveal == CardLookReveal::Wave);
	const auto origin = map.map(_source.origin);
	const auto radius = wave
		? WindowWaveRadius(
			QRectF(area()),
			origin,
			_source.width,
			_source.start,
			progress)
		: 0.;
	const auto opacity = wave ? 1. : (1. - CardLookFadeValue(progress));
	if (opacity <= 0.) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	if (radius > 0.) {
		auto path = QPainterPath();
		path.setFillRule(Qt::OddEvenFill);
		const auto rect = QRectF(widget->rect());
		path.addRect(rect.marginsAdded({ 1., 1., 1., 1. }));
		path.addEllipse(origin - shift, radius, radius);
		p.setClipPath(path);
	}
	p.setOpacity(opacity);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	DrawMapped(p, image, _image);
	p.restore();
	if (radius > 0.) {
		const auto bounds = QRectF(QPointF(), QSizeF(_image.size()));
		p.save();
		p.setClipRect(image.mapRect(bounds));
		PaintCardLookRim(p, origin - shift, radius, _source.width);
		p.restore();
	}
}

WindowLookTransition::WindowLookTransition(WindowLookTransitionArgs &&args)
: _card(args.apply)
, _reveal(std::make_unique<WindowLookReveal>(WindowLookRevealArgs{
	.panel = args.panel,
	.body = args.body,
	.palette = args.palette,
	.fold = std::move(args.fold),
	.apply = args.apply,
})) {
	OwnCardLookValue(
		args.session
	) | rpl::on_next([palette = args.palette](const CardLook &look) {
		palette->prepare(look);
	}, _lifetime);
	args.session->data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=] {
		_lifetime.destroy();
	}, _lifetime);
	_follower = std::make_unique<CardLookFollower>(CardLookFollowerArgs{
		.session = args.session,
		.stickers = args.stickers,
		.apply = [=](CardLook look, CardLookChange change) {
			if (_reveal) {
				_reveal->apply(std::move(look), change);
			} else {
				_card(std::move(look), {});
			}
		},
	});
}

WindowLookTransition::~WindowLookTransition() {
	_lifetime.destroy();
	_reveal = nullptr;
	_follower = nullptr;
}

rpl::producer<bool> WindowLookTransition::runningValue() const {
	return _reveal->runningValue();
}

} // namespace Wallet
