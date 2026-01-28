/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/peer/color_sample.h"

#include "base/algorithm.h"
#include "ui/chat/chat_style.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "styles/style_settings.h"

#include <QtMath>

namespace Ui {
namespace {

constexpr auto kSelectAnimationDuration = crl::time(150);
constexpr auto kUnsetColorIndex = uint8(0xFF);
constexpr auto kProfileColorIndexCount = uint8(8);

} // namespace

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	Fn<Ui::Text::MarkedContext()> contextProvider,
	Fn<TextWithEntities(uint64)> emojiProvider,
	std::shared_ptr<Ui::ChatStyle> style,
	rpl::producer<uint8> colorIndex,
	rpl::producer<std::shared_ptr<Ui::ColorCollectible>> collectible,
	const QString &name)
: AbstractButton(parent)
, _style(style) {
	rpl::combine(
		std::move(colorIndex),
		std::move(collectible)
	) | rpl::on_next([=](
			uint8 index,
			std::shared_ptr<Ui::ColorCollectible> collectible) {
		_index = index;
		_collectible = std::move(collectible);
		if (const auto raw = _collectible.get()) {
			auto context = contextProvider();
			context.repaint = [=] { update(); };
			_name.setMarkedText(
				st::semiboldTextStyle,
				emojiProvider(raw->giftEmojiId),
				kMarkupTextOptions,
				std::move(context));
		} else {
			_name.setText(st::semiboldTextStyle, name);
		}
		setNaturalWidth([&] {
			if (_name.isEmpty() || _style->colorPatternIndex(_index)) {
				return st::settingsColorSampleSize;
			}
			const auto padding = st::settingsColorSamplePadding;
			return std::max(
				padding.left() + _name.maxWidth() + padding.right(),
				padding.top() + st::semiboldFont->height + padding.bottom());
		}());
		update();
	}, lifetime());
}

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	std::shared_ptr<Ui::ChatStyle> style,
	uint8 colorIndex,
	bool selected)
: AbstractButton(parent)
, _style(style)
, _index(colorIndex)
, _selected(selected)
, _simple(true) {
	setNaturalWidth(st::settingsColorSampleSize);
}

ColorSample::ColorSample(
	not_null<QWidget*> parent,
	Fn<Data::ColorProfileSet(uint8)> profileProvider,
	uint8 colorIndex,
	bool selected)
: AbstractButton(parent)
, _index(colorIndex)
, _selected(selected)
, _simple(true)
, _profileProvider(std::move(profileProvider)) {
	setNaturalWidth(st::settingsColorSampleSize);
}

void ColorSample::setSelected(bool selected) {
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	_selectAnimation.start(
		[=] { update(); },
		_selected ? 0. : 1.,
		_selected ? 1. : 0.,
		kSelectAnimationDuration);
}

void ColorSample::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	if (_profileProvider) {
		const auto size = float64(width());
		const auto half = size / 2.;
		const auto full = QRectF(-half, -half, size, size);
		p.translate(size / 2., size / 2.);
		p.setPen(Qt::NoPen);

		const auto profile = _profileProvider(_index);
		if (!profile.palette.empty()) {
			if (profile.palette.size() == 2) {
				p.rotate(-45.);
				p.setClipRect(-size, 0, 3 * size, size);
				p.setBrush(profile.palette[1]);
				p.drawEllipse(full);
				p.setClipRect(-size, -size, 3 * size, size);
			}
			p.setBrush(profile.palette[0]);
			p.drawEllipse(full);
		}
		p.setClipping(false);

		const auto selected = _selectAnimation.value(_selected ? 1. : 0.);
		if (selected > 0) {
			const auto line = st::settingsColorRadioStroke * 1.;
			const auto thickness = selected * line;
			auto pen = st::boxBg->p;
			pen.setWidthF(thickness);
			p.setBrush(Qt::NoBrush);
			p.setPen(pen);
			const auto skip = 1.5 * line;
			p.drawEllipse(full.marginsRemoved({ skip, skip, skip, skip }));
		}
		return;
	}

	const auto colors = _style->coloredValues(false, _index);
	if (!_forceCircle && !_simple && !colors.outlines[1].alpha()) {
		const auto radius = height() / 2;
		p.setPen(Qt::NoPen);
		if (const auto raw = _collectible.get()) {
			const auto withBg = [&](const QColor &color) {
				return Ui::CountContrast(st::windowBg->c, color);
			};
			const auto dark = (withBg({ 0, 0, 0 })
				< withBg({ 255, 255, 255 }));
			const auto name = (dark && raw->darkAccentColor.alpha() > 0)
				? raw->darkAccentColor
				: raw->accentColor;
			auto bg = name;
			bg.setAlpha(0.12 * 255);
			p.setBrush(bg);
		} else {
			p.setBrush(colors.bg);
		}
		p.drawRoundedRect(rect(), radius, radius);

		const auto padding = st::settingsColorSamplePadding;
		p.setPen(colors.name);
		p.setBrush(Qt::NoBrush);
		p.setFont(st::semiboldFont);
		_name.drawLeftElided(
			p,
			padding.left(),
			padding.top(),
			width() - padding.left() - padding.right(),
			width(),
			1,
			style::al_top);
	} else {
		const auto size = float64(width());
		const auto half = size / 2.;
		const auto full = QRectF(-half, -half, size, size);
		p.translate(size / 2., size / 2.);
		p.setPen(Qt::NoPen);

		auto combinedClip = QPainterPath();
		if (_cutoutPadding > 0) {
			combinedClip.addRect(QRectF(-half, -half, size, size));
			auto cutout = QPainterPath();
			const auto cutoutRadius = half;
			const auto cutoutX = -half - _cutoutPadding;
			const auto cutoutY = 0.0;
			if (colors.outlines[1].alpha()) {
				const auto angle = M_PI / (180. / 45.);
				const auto rotatedX = cutoutX * std::cos(angle)
					- cutoutY * std::sin(angle);
				const auto rotatedY = cutoutX * std::sin(angle)
					+ cutoutY * std::cos(angle);
				cutout.addEllipse(
					QPointF(rotatedX, rotatedY),
					cutoutRadius,
					cutoutRadius);
			} else {
				cutout.addEllipse(
					QPointF(cutoutX, cutoutY),
					cutoutRadius,
					cutoutRadius);
			}
			combinedClip = combinedClip.subtracted(cutout);
		}

		if (colors.outlines[1].alpha()) {
			p.rotate(-45.);
			if (_cutoutPadding > 0) {
				auto rectPath = QPainterPath();
				rectPath.addRect(QRectF(-size, 0, 3 * size, size));
				auto clipRect = combinedClip.intersected(rectPath);
				p.setClipPath(clipRect);
			} else {
				p.setClipRect(-size, 0, 3 * size, size);
			}
			p.setBrush(colors.outlines[1]);
			p.drawEllipse(full);
			if (_cutoutPadding > 0) {
				auto rectPath = QPainterPath();
				rectPath.addRect(QRectF(-size, -size, 3 * size, size));
				auto clipRect = combinedClip.intersected(rectPath);
				p.setClipPath(clipRect);
			} else {
				p.setClipRect(-size, -size, 3 * size, size);
			}
		} else if (_cutoutPadding > 0) {
			p.setClipPath(combinedClip);
		}
		p.setBrush(colors.outlines[0]);
		p.drawEllipse(full);
		p.setClipping(false);
		if (colors.outlines[2].alpha()) {
			const auto multiplier = size / st::settingsColorSampleSize;
			const auto center = st::settingsColorSampleCenter * multiplier;
			const auto radius = st::settingsColorSampleCenterRadius
				* multiplier;
			p.setBrush(colors.outlines[2]);
			p.drawRoundedRect(
				QRectF(-center / 2., -center / 2., center, center),
				radius,
				radius);
		}
		const auto selected = _selectAnimation.value(_selected ? 1. : 0.);
		if (selected > 0) {
			const auto line = st::settingsColorRadioStroke * 1.;
			const auto thickness = selected * line;
			auto pen = st::boxBg->p;
			pen.setWidthF(thickness);
			p.setBrush(Qt::NoBrush);
			p.setPen(pen);
			const auto skip = 1.5 * line;
			p.drawEllipse(full.marginsRemoved({ skip, skip, skip, skip }));
		}
	}
}

uint8 ColorSample::index() const {
	return _index;
}

void ColorSample::setCutoutPadding(int padding) {
	if (_cutoutPadding == padding) {
		return;
	}
	_cutoutPadding = padding;
	update();
}

void ColorSample::setForceCircle(bool force) {
	if (_forceCircle == force) {
		return;
	}
	_forceCircle = force;
	update();
}

ColorSelector::ColorSelector(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatStyle> style,
	rpl::producer<std::vector<uint8>> indices,
	rpl::producer<uint8> index,
	Fn<void(uint8)> callback)
: RpWidget(parent)
, _style(style)
, _callback(std::move(callback))
, _index(std::move(index))
, _isProfileMode(false) {
	std::move(
		indices
	) | rpl::on_next([=](std::vector<uint8> indices) {
		fillFrom(std::move(indices));
	}, lifetime());
	setupSelectionTracking();
}

ColorSelector::ColorSelector(
	not_null<QWidget*> parent,
	const std::vector<uint8> &indices,
	uint8 index,
	Fn<void(uint8)> callback,
	Fn<Data::ColorProfileSet(uint8)> profileProvider)
: RpWidget(parent)
, _callback(std::move(callback))
, _index(index)
, _profileProvider(std::move(profileProvider))
, _isProfileMode(true) {
	fillFrom(indices);
}

void ColorSelector::updateSelection(uint8 newIndex) {
	if (_index.current() == newIndex) {
		return;
	}
	for (const auto &sample : _samples) {
		if (sample->index() == _index.current()) {
			sample->setSelected(false);
			break;
		}
	}
	for (const auto &sample : _samples) {
		if (sample->index() == newIndex) {
			sample->setSelected(true);
			break;
		}
	}
	_index = newIndex;
}

void ColorSelector::fillFrom(std::vector<uint8> indices) {
	auto samples = std::vector<std::unique_ptr<ColorSample>>();
	const auto initial = _index.current();
	const auto add = [&](uint8 index) {
		auto i = ranges::find(_samples, index, &ColorSample::index);
		if (i != end(_samples)) {
			samples.push_back(std::move(*i));
			_samples.erase(i);
		} else {
			if (_isProfileMode) {
				samples.push_back(std::make_unique<ColorSample>(
					this,
					_profileProvider,
					index,
					index == initial));
			} else {
				samples.push_back(std::make_unique<ColorSample>(
					this,
					_style,
					index,
					index == initial));
			}
			samples.back()->show();
			samples.back()->setClickedCallback([=] {
				if (_isProfileMode) {
					updateSelection(index);
				}
				_callback(index);
			});
		}
	};
	for (const auto index : indices) {
		add(index);
	}
	if (!_isProfileMode
			&& initial != 0xFF
			&& !ranges::contains(indices, initial)) {
		add(initial);
	}
	_samples = std::move(samples);
	if (width() > 0) {
		resizeToWidth(width());
	}
}

void ColorSelector::setupSelectionTracking() {
	if (_isProfileMode) {
		return;
	}
	_index.value(
	) | rpl::combine_previous(
	) | rpl::on_next([=](uint8 was, uint8 now) {
		const auto i = ranges::find(_samples, was, &ColorSample::index);
		if (i != end(_samples)) {
			i->get()->setSelected(false);
		}
		const auto j = ranges::find(_samples, now, &ColorSample::index);
		if (j != end(_samples)) {
			j->get()->setSelected(true);
		}
	}, lifetime());
}

int ColorSelector::resizeGetHeight(int newWidth) {
	if (newWidth <= 0) {
		return 0;
	}
	const auto count = int(_samples.size());
	const auto columns = _isProfileMode
		? kProfileColorIndexCount
		: kSimpleColorIndexCount;
	const auto skip = st::settingsColorRadioSkip;
	const auto size = (newWidth - skip * (columns - 1)) / float64(columns);
	const auto isize = int(base::SafeRound(size));
	auto top = 0;
	auto left = 0.;
	for (auto i = 0; i != count; ++i) {
		_samples[i]->resize(isize, isize);
		_samples[i]->move(int(base::SafeRound(left)), top);
		left += size + skip;
		if (!((i + 1) % columns)) {
			top += isize + skip;
			left = 0.;
		}
	}
	return (top - skip) + ((count % columns) ? (isize + skip) : 0);
}

} // namespace Ui
