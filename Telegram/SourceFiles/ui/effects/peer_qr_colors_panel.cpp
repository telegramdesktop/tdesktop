/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_colors_panel.h"

#include "ui/effects/premium_3d_support.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/top_background_gradient.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kColumns = 6;
constexpr auto kRows = 3;
constexpr auto kPeek = 0.4;
constexpr auto kOpenDuration = crl::time(420);
constexpr auto kCloseDuration = crl::time(360);
constexpr auto kCurveX1 = 0.16;
constexpr auto kCurveY1 = 1.;
constexpr auto kCurveX2 = 0.3;
constexpr auto kCurveY2 = 1.;
constexpr auto kRowInDelay = crl::time(60);
constexpr auto kRowInDuration = crl::time(220);
constexpr auto kRowOutDelay = crl::time(20);
constexpr auto kRowOutDuration = crl::time(90);
constexpr auto kContentFadeDuration = crl::time(220);
constexpr auto kRowMinScale = 0.85;
constexpr auto kRingDuration = crl::time(220);
constexpr auto kRingMinScale = 0.8;
constexpr auto kGlassAlpha = 0.25;
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
	if (size <= 0) {
		return QImage();
	} else if (appearance.center.isValid()) {
		return Images::Circle(CreateTopBgGradient(
			QSize(size, size),
			appearance.center,
			appearance.edge,
			false));
	} else if (appearance.colors.empty()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = Images::Circle(Images::GenerateGradient(
		QSize(size, size) * ratio,
		appearance.colors));
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] bool NeedsHairline(const QImage &image, float64 radius) {
	if (image.isNull()) {
		return false;
	}
	const auto ratio = image.devicePixelRatio();
	const auto center = QPointF(image.width(), image.height()) / 2.;
	const auto white = QColor(255, 255, 255);
	for (auto i = 0; i != kHairlineSamples; ++i) {
		const auto angle = 2. * M_PI * i / kHairlineSamples;
		const auto point = center
			+ QPointF(std::cos(angle), std::sin(angle)) * radius * ratio;
		const auto x = std::clamp(int(point.x()), 0, image.width() - 1);
		const auto y = std::clamp(int(point.y()), 0, image.height() - 1);
		if (CountContrast(white, image.pixelColor(x, y))
			< kHairlineContrast) {
			return true;
		}
	}
	return false;
}

} // namespace

class PeerQrColorsPanel::Inner final : public RpWidget {
public:
	Inner(QWidget *parent, not_null<PeerQrColorsPanel*> panel);

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
	void paintRing(QPainter &p, int index, QPointF center) const;
	void paintFades(QPainter &p, QSize viewport) const;

	const not_null<PeerQrColorsPanel*> _panel;
	std::vector<Circle> _circles;
	std::optional<PeerQrThemeKey> _selected;
	Animations::Simple _ring;
	QImage _canvas;
	float64 _size = 0.;
	int _selectedIndex = -1;
	int _wasIndex = -1;
	int _over = -1;
	int _pressed = -1;

};

PeerQrColorsPanel::Inner::Inner(
	QWidget *parent,
	not_null<PeerQrColorsPanel*> panel)
: RpWidget(parent)
, _panel(panel) {
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
	_selectedIndex = indexOf(_selected);
	_wasIndex = -1;
	_ring.stop();
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
	const auto index = indexOf(key);
	if (_selectedIndex == index) {
		return;
	}
	_wasIndex = std::exchange(_selectedIndex, index);
	if (animated == anim::type::normal
		&& !anim::Disabled()
		&& !_panel->isHidden()) {
		_ring.start(
			[=] { update(); },
			0.,
			1.,
			kRingDuration,
			anim::easeOutCubic);
	} else {
		_ring.stop();
	}
	update();
}

void PeerQrColorsPanel::Inner::scrollToSelected() {
	if (_selectedIndex < 0) {
		return;
	}
	const auto rect = circleRect(_selectedIndex);
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
		p.setClipPath(
			_panel->glassPath().translated(-QPointF(scroll->pos())));
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
	paintRing(p, index, center);
}

void PeerQrColorsPanel::Inner::paintRing(
		QPainter &p,
		int index,
		QPointF center) const {
	const auto selected = (index == _selectedIndex);
	if (!selected && (index != _wasIndex || !_ring.animating())) {
		return;
	}
	const auto progress = _ring.value(1.);
	const auto outer = _size / 2.;
	const auto line = float64(st::peerQrSwatchRing);
	const auto rest = outer - st::peerQrSwatchRingInset;
	const auto radius = selected
		? (rest * (kRingMinScale + (1. - kRingMinScale) * progress))
		: (rest + (outer - line / 2. - rest) * progress);
	const auto width = selected ? line : (line * (1. - progress));
	const auto opacity = selected ? progress : 1.;
	if (width <= 0. || opacity <= 0.) {
		return;
	}
	const auto was = p.opacity();
	p.setOpacity(was * opacity);
	p.setBrush(Qt::NoBrush);
	if (_circles[index].hairline) {
		auto hairline = QColor(0, 0, 0);
		hairline.setAlphaF(kHairlineAlpha);
		p.setPen(QPen(hairline, width * kHairlineWidthRatio));
		p.drawEllipse(center, radius, radius);
	}
	p.setPen(QPen(QColor(255, 255, 255), width));
	p.drawEllipse(center, radius, radius);
	p.setOpacity(was);
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
	setOver((_panel->_shown && insideGlass(e->pos()))
		? circleAt(e->pos())
		: -1);
}

void PeerQrColorsPanel::Inner::mousePressEvent(QMouseEvent *e) {
	if (!insideGlass(e->pos())) {
		_pressed = -1;
		_panel->_outsidePresses.fire({});
	} else if (e->button() == Qt::LeftButton && _panel->_shown) {
		_pressed = circleAt(e->pos());
	}
}

void PeerQrColorsPanel::Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0
		|| e->button() != Qt::LeftButton
		|| !_panel->_shown
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
	return _panel->insideGlass(mapTo(_panel.get(), point));
}

RowState PeerQrColorsPanel::Inner::rowState(
		int fromTop,
		int fromBottom) const {
	if (!_panel->_animation.animating()) {
		return RowState();
	}
	const auto elapsed = crl::now() - _panel->_toggledAt;
	if (_panel->_shown) {
		return RowIn(fromBottom, elapsed);
	}
	auto result = RowOut(fromTop, elapsed);
	if (const auto opened = _panel->_openedAtClose) {
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

PeerQrColorsPanel::PeerQrColorsPanel(not_null<QWidget*> parent)
: RpWidget(parent)
, _scroll(CreateChild<ElasticScroll>(this, st::peerQrPanelScroll))
, _inner(_scroll->setOwnedWidget(
	object_ptr<Inner>(_scroll.get(), this)).data())
, _animation([=](crl::time now) { return animationCallback(now); }) {
	_scroll->scrollTopValue() | rpl::on_next([=] {
		_inner->update();
	}, lifetime());
	hide();
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

void PeerQrColorsPanel::setIslandRect(QRect island) {
	if (_island == island) {
		return;
	}
	_island = island;
	if (_progress.current() < 1.) {
		updateAll();
	}
}

void PeerQrColorsPanel::setColors(const PeerQrColors &colors) {
	if (_islandColor == colors.control) {
		return;
	}
	_islandColor = colors.control;
	if (_progress.current() < 1.) {
		update();
	}
}

void PeerQrColorsPanel::toggle(bool shown, anim::type animated) {
	if (_shown == shown) {
		return;
	}
	const auto now = crl::now();
	_openedAtClose = (!shown && _animation.animating())
		? std::make_optional(now - _toggledAt)
		: std::nullopt;
	_shown = shown;
	_toggledAt = now;
	_progressFrom = _progress.current();
	if (shown) {
		show();
		_inner->scrollToSelected();
	}
	if (animated == anim::type::normal && !anim::Disabled()) {
		_animation.start();
		updateAll();
	} else {
		_animation.stop();
		finishToggle();
	}
}

bool PeerQrColorsPanel::toggled() const {
	return _shown;
}

rpl::producer<float64> PeerQrColorsPanel::progressValue() const {
	return _progress.value();
}

rpl::producer<PeerQrThemeKey> PeerQrColorsPanel::picks() const {
	return _picks.events();
}

rpl::producer<> PeerQrColorsPanel::outsidePresses() const {
	return _outsidePresses.events();
}

void PeerQrColorsPanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.fillPath(glassPath(), glassColor());
}

void PeerQrColorsPanel::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect());
	_inner->resizeToWidth(width());
}

void PeerQrColorsPanel::mousePressEvent(QMouseEvent *e) {
	if (!insideGlass(e->pos())) {
		_outsidePresses.fire({});
	}
}

bool PeerQrColorsPanel::animationCallback(crl::time now) {
	const auto duration = _shown ? kOpenDuration : kCloseDuration;
	const auto progress = (now - _toggledAt) / float64(duration);
	if (progress >= 1.) {
		finishToggle();
		return false;
	}
	const auto eased = Premium::CubicBezier(
		kCurveX1,
		kCurveY1,
		kCurveX2,
		kCurveY2,
		std::max(progress, 0.));
	const auto target = _shown ? 1. : 0.;
	_progress = _progressFrom + (target - _progressFrom) * eased;
	updateAll();
	return true;
}

void PeerQrColorsPanel::finishToggle() {
	_openedAtClose = std::nullopt;
	_progress = _shown ? 1. : 0.;
	if (!_shown) {
		hide();
	}
	updateAll();
}

void PeerQrColorsPanel::updateAll() {
	update();
	_inner->update();
}

QRectF PeerQrColorsPanel::glassRect() const {
	const auto progress = _progress.current();
	const auto from = QRectF(_island.translated(-pos()));
	const auto till = QRectF(rect());
	const auto lerp = [&](float64 a, float64 b) {
		return a + (b - a) * progress;
	};
	return QRectF(
		lerp(from.x(), till.x()),
		lerp(from.y(), till.y()),
		lerp(from.width(), till.width()),
		lerp(from.height(), till.height()));
}

QPainterPath PeerQrColorsPanel::glassPath() const {
	const auto glass = glassRect();
	const auto radius = std::min(
		_island.height() / 2.
			+ (st::peerQrPanelRadius - _island.height() / 2.)
				* _progress.current(),
		std::min(glass.width(), glass.height()) / 2.);
	auto result = QPainterPath();
	result.addRoundedRect(glass, radius, radius);
	return result;
}

QColor PeerQrColorsPanel::glassColor() const {
	auto glass = QColor(255, 255, 255);
	glass.setAlphaF(kGlassAlpha);
	return _islandColor.isValid()
		? anim::color(_islandColor, glass, _progress.current())
		: glass;
}

bool PeerQrColorsPanel::insideGlass(QPoint point) const {
	return glassPath().contains(QPointF(point));
}

} // namespace Ui
