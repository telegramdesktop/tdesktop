/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_colors_panel.h"

#include "ui/effects/peer_qr_panel.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kColumns = 6;
constexpr auto kRows = 3;
constexpr auto kPeek = 0.4;
constexpr auto kRowInDelay = crl::time(60);
constexpr auto kRowInDuration = crl::time(220);
constexpr auto kRowOutDelay = crl::time(20);
constexpr auto kRowOutDuration = crl::time(90);
constexpr auto kContentFadeDuration = crl::time(220);
constexpr auto kRowMinScale = 0.85;
constexpr auto kRingDuration = crl::time(220);
constexpr auto kRingMinScale = 0.8;
constexpr auto kHairlineContrast = 1.5;
constexpr auto kHairlineAlpha = 0.15;
constexpr auto kHairlineWidthRatio = 2.;
constexpr auto kHairlineSamples = 8;

struct RowState {
	float64 opacity = 1.;
	float64 scale = 1.;
	float64 rise = 0.;
};

struct Circle {
	PeerQrSwatch swatch;
	QImage image;
	bool hairline = false;
};

[[nodiscard]] float64 CircleSize(int width) {
	const auto padding = st::peerQrPanelPadding;
	const auto skip = st::peerQrSwatchSkip;
	return std::max(
		(width - 2 * padding - (kColumns - 1) * skip) / float64(kColumns),
		0.);
}

[[nodiscard]] float64 EasedPart(
		crl::time elapsed,
		crl::time delay,
		crl::time duration) {
	const auto progress = (elapsed - delay) / float64(duration);
	return anim::easeOutCubic(1., std::clamp(progress, 0., 1.));
}

[[nodiscard]] RowState RowIn(int fromBottom, crl::time elapsed) {
	const auto shown = EasedPart(
		elapsed,
		fromBottom * kRowInDelay,
		kRowInDuration);
	return {
		.opacity = shown,
		.scale = kRowMinScale + (1. - kRowMinScale) * shown,
		.rise = st::peerQrSwatchRise * (1. - shown),
	};
}

[[nodiscard]] RowState RowOut(int fromTop, crl::time elapsed) {
	const auto hidden = EasedPart(
		elapsed,
		fromTop * kRowOutDelay,
		kRowOutDuration);
	const auto content = 1. - std::clamp(
		elapsed / float64(kContentFadeDuration),
		0.,
		1.);
	return {
		.opacity = (1. - hidden) * content,
		.scale = 1. - (1. - kRowMinScale) * hidden,
	};
}

[[nodiscard]] QImage PrepareCircle(
		const PeerQrAppearance &appearance,
		int size) {
	if (size <= 0 || !appearance.center.isValid()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	{
		auto p = QPainter(&result);
		auto gradient = QLinearGradient(QPointF(), QPointF(size, size));
		gradient.setStops({
			{ 0., appearance.center },
			{ 1., appearance.edge },
		});
		p.fillRect(QRect(0, 0, size, size), gradient);
	}
	return Images::Circle(std::move(result));
}

[[nodiscard]] bool NeedsHairline(const QImage &image, float64 radius) {
	if (image.isNull()) {
		return false;
	}
	const auto ratio = image.devicePixelRatio();
	const auto center = QPointF(image.width(), image.height()) / 2.;
	for (auto i = 0; i != kHairlineSamples; ++i) {
		const auto angle = 2. * M_PI * i / kHairlineSamples;
		const auto point = center
			+ QPointF(std::cos(angle), std::sin(angle)) * radius * ratio;
		const auto x = std::clamp(int(point.x()), 0, image.width() - 1);
		const auto y = std::clamp(int(point.y()), 0, image.height() - 1);
		if (PeerQrRingNeedsHairline(image.pixelColor(x, y))) {
			return true;
		}
	}
	return false;
}

} // namespace

class PeerQrColorsPanel::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<PeerQrColorsPanel*> panel,
		not_null<PeerQrPanel*> container);

	void setSwatches(std::vector<PeerQrSwatch> swatches);
	void setSelected(std::optional<PeerQrThemeKey> key, anim::type animated);
	void scrollToSelected();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int indexOf(
		const std::optional<PeerQrThemeKey> &key) const;
	[[nodiscard]] QRectF circleRect(int index) const;
	[[nodiscard]] int circleAt(QPoint point) const;
	[[nodiscard]] bool insideGlass(QPoint point) const;
	[[nodiscard]] RowState rowState(int fromTop, int fromBottom) const;
	void setOver(int index);
	void validateCircle(Circle &circle);
	void paintRows(QPainter &p, int from, int till);
	void paintCircle(QPainter &p, int index);
	void paintRing(QPainter &p, int index) const;
	void paintFades(QPainter &p, QSize viewport) const;

	const not_null<PeerQrColorsPanel*> _panel;
	const not_null<PeerQrPanel*> _container;
	std::vector<Circle> _circles;
	std::optional<PeerQrThemeKey> _selected;
	PeerQrSelectionRing _ring;
	QImage _canvas;
	float64 _size = 0.;
	int _over = -1;
	int _pressed = -1;

};

bool PeerQrRingNeedsHairline(QColor color) {
	return CountContrast(QColor(255, 255, 255), color) < kHairlineContrast;
}

void PeerQrSelectionRing::select(
		int index,
		anim::type animated,
		bool visible,
		Fn<void()> repaint) {
	if (_selectedIndex == index) {
		return;
	}
	_wasIndex = std::exchange(_selectedIndex, index);
	if (animated == anim::type::normal && !anim::Disabled() && visible) {
		_animation.start(repaint, 0., 1., kRingDuration, anim::easeOutCubic);
	} else {
		_animation.stop();
	}
	repaint();
}

void PeerQrSelectionRing::reset(int index) {
	_selectedIndex = index;
	_wasIndex = -1;
	_animation.stop();
}

int PeerQrSelectionRing::selectedIndex() const {
	return _selectedIndex;
}

void PeerQrSelectionRing::paint(
		QPainter &p,
		int index,
		QRectF rect,
		float64 minScale,
		bool hairline,
		std::optional<float64> radius) const {
	const auto selected = (index == _selectedIndex);
	if (!selected && (index != _wasIndex || !_animation.animating())) {
		return;
	}
	const auto progress = _animation.value(1.);
	const auto line = float64(st::peerQrSwatchRing);
	const auto rest = float64(st::peerQrSwatchRingInset);
	const auto inset = selected ? rest : (rest + (line / 2. - rest) * progress);
	const auto width = selected ? line : (line * (1. - progress));
	const auto opacity = selected ? progress : 1.;
	if (width <= 0. || opacity <= 0.) {
		return;
	}
	auto ring = rect.marginsRemoved({ inset, inset, inset, inset });
	auto corner = radius ? (*radius - inset) : 0.;
	if (selected) {
		const auto scale = minScale + (1. - minScale) * progress;
		const auto size = ring.size() * scale;
		ring = QRectF(
			ring.center() - QPointF(size.width(), size.height()) / 2.,
			size);
		corner *= scale;
	}
	const auto stroke = [&] {
		if (radius) {
			p.drawRoundedRect(ring, corner, corner);
		} else {
			p.drawEllipse(ring);
		}
	};
	const auto was = p.opacity();
	p.setOpacity(was * opacity);
	p.setBrush(Qt::NoBrush);
	if (hairline) {
		p.setPen(QPen(
			anim::with_alpha(QColor(0, 0, 0), kHairlineAlpha),
			width * kHairlineWidthRatio));
		stroke();
	}
	p.setPen(QPen(QColor(255, 255, 255), width));
	stroke();
	p.setOpacity(was);
}

PeerQrColorsPanel::Inner::Inner(
	QWidget *parent,
	not_null<PeerQrColorsPanel*> panel,
	not_null<PeerQrPanel*> container)
: RpWidget(parent)
, _panel(panel)
, _container(container) {
	setMouseTracking(true);
}

void PeerQrColorsPanel::Inner::setSwatches(
		std::vector<PeerQrSwatch> swatches) {
	const auto same = ranges::equal(
		swatches,
		_circles,
		ranges::equal_to(),
		ranges::identity(),
		&Circle::swatch);
	if (same) {
		return;
	}
	auto circles = std::vector<Circle>();
	circles.reserve(swatches.size());
	for (auto &swatch : swatches) {
		const auto i = ranges::find_if(_circles, [&](const Circle &circle) {
			return (circle.swatch.appearance == swatch.appearance);
		});
		auto circle = Circle{ .swatch = std::move(swatch) };
		if (i != end(_circles)) {
			circle.image = base::take(i->image);
			circle.hairline = i->hairline;
		}
		circles.push_back(std::move(circle));
	}
	_circles = std::move(circles);
	_ring.reset(indexOf(_selected));
	_pressed = -1;
	setOver(-1);
	resizeToWidth(width());
	update();
}

void PeerQrColorsPanel::Inner::setSelected(
		std::optional<PeerQrThemeKey> key,
		anim::type animated) {
	if (_selected == key) {
		return;
	}
	_selected = key;
	_ring.select(indexOf(key), animated, !_container->isHidden(), [=] {
		update();
	});
}

void PeerQrColorsPanel::Inner::scrollToSelected() {
	const auto index = _ring.selectedIndex();
	if (index < 0) {
		return;
	}
	const auto rect = circleRect(index);
	const auto padding = st::peerQrPanelPadding;
	_panel->_scroll->scrollToY(
		int(std::floor(rect.y())) - padding,
		int(std::ceil(rect.y() + rect.height())) + padding);
}

int PeerQrColorsPanel::Inner::resizeGetHeight(int newWidth) {
	const auto padding = st::peerQrPanelPadding;
	const auto skip = st::peerQrSwatchSkip;
	_size = CircleSize(newWidth);
	const auto rows = (int(_circles.size()) + kColumns - 1) / kColumns;
	return rows
		? int(std::ceil(2 * padding + rows * _size + (rows - 1) * skip))
		: 0;
}

void PeerQrColorsPanel::Inner::paintEvent(QPaintEvent *e) {
	const auto scroll = _panel->_scroll.get();
	const auto viewport = scroll->size();
	if (viewport.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	if (_canvas.size() != viewport * ratio) {
		_canvas = QImage(
			viewport * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_canvas.setDevicePixelRatio(ratio);
	}
	_canvas.fill(Qt::transparent);
	const auto top = scroll->scrollTop();
	{
		auto p = QPainter(&_canvas);
		auto hq = PainterHighQualityEnabler(p);
		p.setClipPath(_container->glassPath().translated(
			-QPointF(scroll->mapTo(_container.get(), QPoint()))));
		p.translate(0, -top);
		paintRows(p, top, top + viewport.height());
		p.resetTransform();
		p.setClipping(false);
		paintFades(p, viewport);
	}
	QPainter(this).drawImage(QRect(QPoint(0, top), viewport), _canvas);
}

void PeerQrColorsPanel::Inner::paintRows(QPainter &p, int from, int till) {
	const auto count = int(_circles.size());
	const auto rows = (count + kColumns - 1) / kColumns;
	const auto padding = st::peerQrPanelPadding;
	const auto pitch = _size + st::peerQrSwatchSkip;
	auto first = -1;
	auto last = -1;
	for (auto row = 0; row != rows; ++row) {
		const auto top = padding + row * pitch;
		if (top + _size > from && top < till) {
			if (first < 0) {
				first = row;
			}
			last = row;
		}
	}
	if (first < 0) {
		return;
	}
	for (auto row = first; row <= last; ++row) {
		const auto state = rowState(row - first, last - row);
		if (state.opacity <= 0.) {
			continue;
		}
		const auto center = QPointF(
			width() / 2.,
			padding + row * pitch + _size / 2.);
		p.save();
		p.setOpacity(state.opacity);
		p.translate(center + QPointF(0., state.rise));
		p.scale(state.scale, state.scale);
		p.translate(-center);
		const auto end = std::min(count, (row + 1) * kColumns);
		for (auto index = row * kColumns; index != end; ++index) {
			paintCircle(p, index);
		}
		p.restore();
	}
}

void PeerQrColorsPanel::Inner::paintCircle(QPainter &p, int index) {
	auto &circle = _circles[index];
	validateCircle(circle);
	const auto center = circleRect(index).center();
	if (!circle.image.isNull()) {
		const auto side = circle.image.width()
			/ circle.image.devicePixelRatio();
		p.drawImage(center - QPointF(side, side) / 2., circle.image);
	}
	paintRing(p, index);
}

void PeerQrColorsPanel::Inner::paintRing(QPainter &p, int index) const {
	_ring.paint(
		p,
		index,
		circleRect(index),
		kRingMinScale,
		_circles[index].hairline);
}

void PeerQrColorsPanel::Inner::paintFades(
		QPainter &p,
		QSize viewport) const {
	const auto scroll = _panel->_scroll.get();
	const auto top = scroll->scrollTop();
	const auto topFade = (top > 0);
	const auto bottomFade = (top < scroll->scrollTopMax());
	if (!topFade && !bottomFade) {
		return;
	}
	const auto fade = st::peerQrPanelFade;
	const auto transparent = QColor(255, 255, 255, 0);
	const auto opaque = QColor(255, 255, 255);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.setPen(Qt::NoPen);
	if (topFade) {
		auto gradient = QLinearGradient(0, 0, 0, fade);
		gradient.setStops({ { 0., transparent }, { 1., opaque } });
		p.setBrush(gradient);
		p.drawRect(0, 0, viewport.width(), fade);
	}
	if (bottomFade) {
		const auto from = viewport.height() - fade;
		auto gradient = QLinearGradient(0, from, 0, viewport.height());
		gradient.setStops({ { 0., opaque }, { 1., transparent } });
		p.setBrush(gradient);
		p.drawRect(0, from, viewport.width(), fade);
	}
}

void PeerQrColorsPanel::Inner::mouseMoveEvent(QMouseEvent *e) {
	setOver((_container->phase().shown && insideGlass(e->pos()))
		? circleAt(e->pos())
		: -1);
}

void PeerQrColorsPanel::Inner::mousePressEvent(QMouseEvent *e) {
	if (!insideGlass(e->pos())) {
		_pressed = -1;
		_container->pressOutside();
	} else if (e->button() == Qt::LeftButton
		&& _container->phase().shown) {
		_pressed = circleAt(e->pos());
	}
}

void PeerQrColorsPanel::Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0
		|| e->button() != Qt::LeftButton
		|| !_container->phase().shown
		|| !insideGlass(e->pos())
		|| circleAt(e->pos()) != pressed) {
		return;
	}
	_panel->_picks.fire_copy(_circles[pressed].swatch.key);
}

void PeerQrColorsPanel::Inner::leaveEventHook(QEvent *e) {
	setOver(-1);
}

int PeerQrColorsPanel::Inner::indexOf(
		const std::optional<PeerQrThemeKey> &key) const {
	if (!key) {
		return -1;
	}
	const auto i = ranges::find(
		_circles,
		*key,
		[](const Circle &circle) { return circle.swatch.key; });
	return (i != end(_circles)) ? int(i - begin(_circles)) : -1;
}

QRectF PeerQrColorsPanel::Inner::circleRect(int index) const {
	const auto padding = st::peerQrPanelPadding;
	const auto pitch = _size + st::peerQrSwatchSkip;
	return QRectF(
		padding + (index % kColumns) * pitch,
		padding + (index / kColumns) * pitch,
		_size,
		_size);
}

int PeerQrColorsPanel::Inner::circleAt(QPoint point) const {
	const auto padding = st::peerQrPanelPadding;
	const auto pitch = _size + st::peerQrSwatchSkip;
	if (_size <= 0.) {
		return -1;
	}
	const auto column = int(std::floor((point.x() - padding) / pitch));
	const auto row = int(std::floor((point.y() - padding) / pitch));
	const auto index = row * kColumns + column;
	if (column < 0
		|| column >= kColumns
		|| row < 0
		|| index >= int(_circles.size())) {
		return -1;
	}
	const auto delta = QPointF(point) - circleRect(index).center();
	const auto radius = _size / 2.;
	return (QPointF::dotProduct(delta, delta) <= radius * radius)
		? index
		: -1;
}

bool PeerQrColorsPanel::Inner::insideGlass(QPoint point) const {
	return _container->insideGlass(mapTo(_container.get(), point));
}

RowState PeerQrColorsPanel::Inner::rowState(
		int fromTop,
		int fromBottom) const {
	const auto phase = _container->phase();
	if (!phase.animating) {
		return RowState();
	} else if (phase.shown) {
		return RowIn(fromBottom, phase.elapsed);
	}
	auto result = RowOut(fromTop, phase.elapsed);
	if (const auto opened = phase.openedAtClose) {
		const auto in = RowIn(fromBottom, *opened);
		result.opacity *= in.opacity;
		result.scale *= in.scale;
		result.rise = in.rise;
	}
	return result;
}

void PeerQrColorsPanel::Inner::setOver(int index) {
	if (_over == index) {
		return;
	}
	_over = index;
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
}

void PeerQrColorsPanel::Inner::validateCircle(Circle &circle) {
	const auto size = int(std::round(_size));
	const auto ratio = style::DevicePixelRatio();
	if (!circle.image.isNull()
		&& circle.image.width() == size * ratio
		&& circle.image.devicePixelRatio() == ratio) {
		return;
	}
	circle.image = PrepareCircle(circle.swatch.appearance, size);
	circle.hairline = NeedsHairline(
		circle.image,
		_size / 2. - st::peerQrSwatchRingInset);
}

PeerQrColorsPanel::PeerQrColorsPanel(not_null<PeerQrPanel*> panel)
: RpWidget(panel)
, _scroll(CreateChild<ElasticScroll>(this, st::peerQrPanelScroll))
, _inner(_scroll->setOwnedWidget(
	object_ptr<Inner>(_scroll.get(), this, panel)).data()) {
	rpl::merge(
		_scroll->scrollTopValue() | rpl::to_empty,
		panel->progressValue() | rpl::to_empty
	) | rpl::on_next([=] {
		_inner->update();
	}, lifetime());
}

PeerQrColorsPanel::~PeerQrColorsPanel() = default;

int PeerQrColorsPanel::HeightForWidth(int width) {
	const auto circle = CircleSize(width);
	const auto pitch = circle + st::peerQrSwatchSkip;
	return st::peerQrPanelPadding
		+ int(std::round(kRows * pitch + kPeek * circle));
}

void PeerQrColorsPanel::setSwatches(std::vector<PeerQrSwatch> swatches) {
	_inner->setSwatches(std::move(swatches));
}

void PeerQrColorsPanel::setSelected(
		std::optional<PeerQrThemeKey> key,
		anim::type animated) {
	_inner->setSelected(key, animated);
}

void PeerQrColorsPanel::scrollToSelected() {
	_inner->scrollToSelected();
}

rpl::producer<PeerQrThemeKey> PeerQrColorsPanel::picks() const {
	return _picks.events();
}

void PeerQrColorsPanel::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect());
	_inner->resizeToWidth(width());
}

} // namespace Ui
