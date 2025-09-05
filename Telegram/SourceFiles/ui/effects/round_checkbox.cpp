/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/round_checkbox.h"

#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "ui/effects/outline_segments.h"
#include "styles/style_widgets.h"

#include <QtCore/QCoreApplication>

namespace Ui {
namespace {

constexpr auto kAnimationTimerDelta = crl::time(7);
constexpr auto kWideScale = 3;

[[nodiscard]] int CountFramesCount(const style::RoundCheckbox *st) {
	return (st->duration / kAnimationTimerDelta) + 1;
}

class CheckCaches : public QObject {
public:
	CheckCaches(QObject *parent) : QObject(parent) {
		Expects(parent != nullptr);

		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_data.clear();
		}, _lifetime);
	}

	QPixmap frame(
		const style::RoundCheckbox *st,
		bool displayInactive,
		float64 progress);

private:
	struct Frames {
		bool displayInactive = false;
		std::vector<QPixmap> list;
		QPixmap outerWide;
		QPixmap inner;
		QPixmap check;
	};

	Frames &framesForStyle(
		const style::RoundCheckbox *st,
		bool displayInactive);
	void prepareFramesData(
		const style::RoundCheckbox *st,
		bool displayInactive,
		Frames &frames);
	QPixmap paintFrame(
		const style::RoundCheckbox *st,
		const Frames &frames,
		float64 progress);

	std::map<const style::RoundCheckbox *, Frames> _data;
	rpl::lifetime _lifetime;

};

QPixmap PrepareOuterWide(const style::RoundCheckbox *st) {
	const auto size = st->size;
	const auto wideSize = size * kWideScale;
	auto result = QImage(
		QSize(wideSize, wideSize) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(st->border);
		const auto half = st->width / 2.;
		p.drawEllipse(QRectF(
			(wideSize - size) / 2 - half,
			(wideSize - size) / 2 - half,
			size + 2. * half,
			size + 2. * half));
	}
	return Ui::PixmapFromImage(std::move(result));
}

QPixmap PrepareInner(const style::RoundCheckbox *st, bool displayInactive) {
	const auto size = st->size;
	auto result = QImage(
		QSize(size, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(st->bgActive);
		const auto half = st->width / 2.;
		p.drawEllipse(QRectF(
			displayInactive ? 0. : half,
			displayInactive ? 0. : half,
			size - (displayInactive ? 0. : 2. * half),
			size - (displayInactive ? 0. : 2. * half)));
	}
	return Ui::PixmapFromImage(std::move(result));
}

QPixmap PrepareCheck(const style::RoundCheckbox *st) {
	const auto size = st->size;
	auto result = QImage(
		QSize(size, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		st->check.paint(p, 0, 0, size);
	}
	return Ui::PixmapFromImage(std::move(result));
}

QRect WideDestRect(
		const style::RoundCheckbox *st,
		int x,
		int y,
		float64 scale) {
	auto iconSizeFull = kWideScale * st->size;
	auto iconSize = qRound(iconSizeFull * scale);
	if (iconSize % 2 != iconSizeFull % 2) {
		++iconSize;
	}
	auto iconShift = (iconSizeFull - iconSize) / 2;
	auto iconLeft = x - (kWideScale - 1) * st->size / 2 + iconShift;
	auto iconTop = y - (kWideScale - 1) * st->size / 2 + iconShift;
	return QRect(iconLeft, iconTop, iconSize, iconSize);
}

CheckCaches::Frames &CheckCaches::framesForStyle(
		const style::RoundCheckbox *st,
		bool displayInactive) {
	auto i = _data.find(st);
	if (i == _data.end()) {
		i = _data.emplace(st, Frames()).first;
		prepareFramesData(st, displayInactive, i->second);
	} else if (i->second.displayInactive != displayInactive) {
		i->second = Frames();
		prepareFramesData(st, displayInactive, i->second);
	}
	return i->second;
}

void CheckCaches::prepareFramesData(
		const style::RoundCheckbox *st,
		bool displayInactive,
		Frames &frames) {
	frames.list.resize(CountFramesCount(st));
	frames.displayInactive = displayInactive;

	if (!frames.displayInactive) {
		frames.outerWide = PrepareOuterWide(st);
	}
	frames.inner = PrepareInner(st, frames.displayInactive);
	frames.check = PrepareCheck(st);
}

QPixmap CheckCaches::frame(
		const style::RoundCheckbox *st,
		bool displayInactive,
		float64 progress) {
	auto &frames = framesForStyle(st, displayInactive);

	const auto frameCount = int(frames.list.size());
	const auto frameIndex = int(base::SafeRound(progress * (frameCount - 1)));
	Assert(frameIndex >= 0 && frameIndex < frameCount);

	if (!frames.list[frameIndex]) {
		const auto frameProgress = frameIndex / float64(frameCount - 1);
		frames.list[frameIndex] = paintFrame(st, frames, frameProgress);
	}
	return frames.list[frameIndex];
}

QPixmap CheckCaches::paintFrame(
		const style::RoundCheckbox *st,
		const Frames &frames,
		float64 progress) {
	const auto size = st->size;
	const auto wideSize = size * kWideScale;
	const auto skip = (wideSize - size) / 2;
	auto result = QImage(wideSize * style::DevicePixelRatio(), wideSize * style::DevicePixelRatio(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);

	const auto roundProgress = (progress >= st->bgDuration)
		? 1.
		: (progress / st->bgDuration);
	const auto checkProgress = (1. - progress >= st->fgDuration)
		? 0.
		: (1. - (1. - progress) / st->fgDuration);
	{
		auto p = QPainter(&result);
		PainterHighQualityEnabler hq(p);

		if (!frames.displayInactive) {
			const auto outerMaxScale = (size - st->width) / float64(size);
			const auto outerScale = roundProgress
				+ (1. - roundProgress) * outerMaxScale;
			const auto outerTo = WideDestRect(st, skip, skip, outerScale);
			const auto outerFrom = QRect(
				QPoint(0, 0),
				QSize(wideSize, wideSize) * style::DevicePixelRatio());
			p.drawPixmap(outerTo, frames.outerWide, outerFrom);
		}
		p.drawPixmap(skip, skip, frames.inner);

		const auto divider = checkProgress * st->size;
		const auto checkTo = QRect(skip, skip, divider, st->size);
		const auto checkFrom = QRect(
			QPoint(0, 0),
			QSize(divider, st->size) * style::DevicePixelRatio());
		p.drawPixmap(checkTo, frames.check, checkFrom);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		const auto remove = size * (1. - roundProgress);
		p.drawEllipse(QRectF(
			(wideSize - remove) / 2.,
			(wideSize - remove) / 2.,
			remove,
			remove));
	}
	return Ui::PixmapFromImage(std::move(result));
}

CheckCaches *FrameCaches() {
	static QPointer<CheckCaches> Instance;

	if (const auto instance = Instance.data()) {
		return instance;
	}
	const auto result = new CheckCaches(QCoreApplication::instance());
	Instance = result;
	return result;
}

} // namespace

RoundCheckbox::RoundCheckbox(const style::RoundCheckbox &st, Fn<void()> updateCallback)
: _st(st)
, _updateCallback(updateCallback) {
}

void RoundCheckbox::paint(QPainter &p, int x, int y, int outerWidth, float64 masterScale) const {
	if (!_st.size
		|| (!_checkedProgress.animating()
			&& !_checked
			&& !_displayInactive)) {
		return;
	}

	auto cacheSize = kWideScale * _st.size * style::DevicePixelRatio();
	auto cacheFrom = QRect(0, 0, cacheSize, cacheSize);
	auto inactiveTo = WideDestRect(&_st, x, y, masterScale);

	PainterHighQualityEnabler hq(p);
	if (!_inactiveCacheBg.isNull()) {
		p.drawPixmap(inactiveTo, _inactiveCacheBg, cacheFrom);
	}

	const auto progress = _checkedProgress.value(_checked ? 1. : 0.);
	if (progress > 0.) {
		auto frame = FrameCaches()->frame(&_st, _displayInactive, progress);
		p.drawPixmap(inactiveTo, frame, cacheFrom);
	}

	if (!_inactiveCacheFg.isNull()) {
		p.drawPixmap(inactiveTo, _inactiveCacheFg, cacheFrom);
	}
}

void RoundCheckbox::setChecked(bool newChecked, anim::type animated) {
	if (_checked == newChecked) {
		if (animated == anim::type::instant) {
			_checkedProgress.stop();
		}
		return;
	}
	_checked = newChecked;
	if (animated == anim::type::normal) {
		_checkedProgress.start(
			_updateCallback,
			_checked ? 0. : 1.,
			_checked ? 1. : 0.,
			_st.duration,
			anim::linear);
	} else {
		_checkedProgress.stop();
	}
}

void RoundCheckbox::finishAnimating() {
	_checkedProgress.stop();
}

void RoundCheckbox::invalidateCache() {
	if (!_inactiveCacheBg.isNull() || !_inactiveCacheFg.isNull()) {
		prepareInactiveCache();
	}
}

void RoundCheckbox::setDisplayInactive(bool displayInactive) {
	if (_displayInactive != displayInactive) {
		_displayInactive = displayInactive;
		if (_displayInactive) {
			prepareInactiveCache();
		} else {
			_inactiveCacheBg = _inactiveCacheFg = QPixmap();
		}
	}
}

void RoundCheckbox::prepareInactiveCache() {
	auto wideSize = _st.size * kWideScale;
	auto ellipse = QRect((wideSize - _st.size) / 2, (wideSize - _st.size) / 2, _st.size, _st.size);

	auto cacheBg = QImage(wideSize * style::DevicePixelRatio(), wideSize * style::DevicePixelRatio(), QImage::Format_ARGB32_Premultiplied);
	cacheBg.setDevicePixelRatio(style::DevicePixelRatio());
	cacheBg.fill(Qt::transparent);
	auto cacheFg = cacheBg;
	if (_st.bgInactive) {
		auto p = QPainter(&cacheBg);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(_st.bgInactive);
		p.drawEllipse(ellipse);
	}
	_inactiveCacheBg = Ui::PixmapFromImage(std::move(cacheBg));

	{
		auto p = QPainter(&cacheFg);
		PainterHighQualityEnabler hq(p);

		auto pen = _st.border->p;
		pen.setWidth(_st.width);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(ellipse);
	}
	_inactiveCacheFg = Ui::PixmapFromImage(std::move(cacheFg));
}

RoundImageCheckbox::RoundImageCheckbox(
	const style::RoundImageCheckbox &st,
	Fn<void()> updateCallback,
	PaintRoundImage &&paintRoundImage,
	Fn<std::optional<int>(int size)> roundingRadius)
: _st(st)
, _updateCallback(updateCallback)
, _paintRoundImage(std::move(paintRoundImage))
, _roundingRadius(std::move(roundingRadius))
, _check(_st.check, _updateCallback) {
}

RoundImageCheckbox::RoundImageCheckbox(RoundImageCheckbox&&) = default;

RoundImageCheckbox::~RoundImageCheckbox() = default;

void RoundImageCheckbox::paint(Painter &p, int x, int y, int outerWidth) const {
	auto selectionLevel = _selection.value(checked() ? 1. : 0.);
	if (_selection.animating()) {
		auto userpicRadius = qRound(kWideScale * (_st.imageRadius + (_st.imageSmallRadius - _st.imageRadius) * selectionLevel));
		auto userpicShift = kWideScale * _st.imageRadius - userpicRadius;
		auto userpicLeft = x - (kWideScale - 1) * _st.imageRadius + userpicShift;
		auto userpicTop = y - (kWideScale - 1) * _st.imageRadius + userpicShift;
		auto to = QRect(userpicLeft, userpicTop, userpicRadius * 2, userpicRadius * 2);
		auto from = QRect(QPoint(0, 0), _wideCache.size());

		PainterHighQualityEnabler hq(p);
		p.drawPixmapLeft(to, outerWidth, _wideCache, from);
	} else {
		auto userpicRadius = checked() ? _st.imageSmallRadius : _st.imageRadius;
		auto userpicShift = _st.imageRadius - userpicRadius;
		auto userpicLeft = x + userpicShift;
		auto userpicTop = y + userpicShift;
		_paintRoundImage(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	}

	if (selectionLevel > 0) {
		PainterHighQualityEnabler hq(p);
		p.setOpacity(std::clamp(selectionLevel, 0., 1.));
		p.setBrush(Qt::NoBrush);
		const auto segments = int(_segments.size());
		const auto rect = style::rtlrect(
			x,
			y,
			_st.imageRadius * 2,
			_st.imageRadius * 2,
			outerWidth);
		const auto add = _st.selectExtendTwice / 2.;
		const auto outline = QRectF(rect).marginsAdded({
			add, add, add, add });
		if (segments < 2) {
			const auto radius = _roundingRadius
				? _roundingRadius(_st.imageRadius * 2)
				: std::optional<int>();
			const auto pen = QPen(
				segments ? _segments.front().brush : _st.selectFg->b,
				segments ? _segments.front().width : _st.selectWidth);
			p.setPen(pen);
			if (!radius) {
				p.drawEllipse(outline);
			} else {
				p.drawRoundedRect(outline, *radius, *radius);
			}
		} else {
			PaintOutlineSegments(p, outline, _segments);
		}
		p.setOpacity(1.);
	}
	if (_st.check.size > 0) {
		auto iconLeft = x + 2 * _st.imageRadius + _st.selectWidth - _st.check.size;
		auto iconTop = y + 2 * _st.imageRadius + _st.selectWidth - _st.check.size;
		_check.paint(p, iconLeft, iconTop, outerWidth);
	}
}

float64 RoundImageCheckbox::checkedAnimationRatio() const {
	return std::clamp(_selection.value(checked() ? 1. : 0.), 0., 1.);
}

void RoundImageCheckbox::setChecked(bool newChecked, anim::type animated) {
	auto changed = (checked() != newChecked);
	_check.setChecked(newChecked, animated);
	if (!changed) {
		if (animated == anim::type::instant) {
			_selection.stop();
			_wideCache = QPixmap();
		}
		return;
	}
	if (animated == anim::type::normal) {
		prepareWideCache();
		const auto from = checked() ? 0. : 1.;
		const auto to = checked() ? 1. : 0.;
		_selection.start(
			[=](float64 value) {
				if (_updateCallback) {
					_updateCallback();
				}
				if (value == to) {
					_wideCache = QPixmap();
				}
			},
			from,
			to,
			_st.selectDuration,
			anim::bumpy(1.25));
	} else {
		_selection.stop();
		_wideCache = QPixmap();
	}
}

void RoundImageCheckbox::prepareWideCache() {
	if (_wideCache.isNull()) {
		auto size = _st.imageRadius * 2;
		auto wideSize = size * kWideScale;
		QImage cache(wideSize * style::DevicePixelRatio(), wideSize * style::DevicePixelRatio(), QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(style::DevicePixelRatio());
		{
			auto p = Painter(&cache);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, wideSize, wideSize, Qt::transparent);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			_paintRoundImage(p, (wideSize - size) / 2, (wideSize - size) / 2, wideSize, size);
		}
		_wideCache = Ui::PixmapFromImage(std::move(cache));
	}
}

void RoundImageCheckbox::setColorOverride(std::optional<QBrush> fg) {
	if (fg) {
		setCustomizedSegments({
			{ .brush = *fg, .width = float64(_st.selectWidth) }
		});
	} else {
		setCustomizedSegments({});
	}
}

void RoundImageCheckbox::setCustomizedSegments(
		std::vector<Ui::OutlineSegment> segments) {
	_segments = std::move(segments);
}

} // namespace Ui
