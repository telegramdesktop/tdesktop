/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/widgets/point_details_widget.h"

#include "info/channel_statistics/earn/earn_format.h"
#include "lang/lang_keys.h"
#include "statistics/statistics_common.h"
#include "statistics/statistics_format_values.h"
#include "statistics/statistics_graphics.h"
#include "statistics/view/stack_linear_chart_common.h"
#include "ui/cached_round_corners.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

#include <QtCore/QDateTime>
#include <QtCore/QLocale>

namespace Statistic {
namespace {

[[nodiscard]] QString FormatWeek(float64 timestamp) {
	constexpr auto kSevenDays = 3600 * 24 * 7;
	timestamp /= 1000;
	return LangDayMonth(timestamp)
		+ ' '
		+ QChar(8212)
		+ ' '
		+ LangDayMonthYear(timestamp + kSevenDays);
}

void PaintShadow(QPainter &p, int radius, const QRect &r) {
	constexpr auto kHorizontalOffset = 1;
	constexpr auto kHorizontalOffset2 = 2;
	constexpr auto kVerticalOffset = 2;
	constexpr auto kVerticalOffset2 = 3;
	constexpr auto kOpacityStep = 0.2;
	constexpr auto kOpacityStep2 = 0.4;
	const auto hOffset = style::ConvertScale(kHorizontalOffset);
	const auto hOffset2 = style::ConvertScale(kHorizontalOffset2);
	const auto vOffset = style::ConvertScale(kVerticalOffset);
	const auto vOffset2 = style::ConvertScale(kVerticalOffset2);
	const auto opacity = p.opacity();
	auto hq = PainterHighQualityEnabler(p);

	p.setOpacity(opacity);
	p.drawRoundedRect(r + QMarginsF(0, hOffset, 0, hOffset), radius, radius);

	p.setOpacity(opacity * kOpacityStep);
	p.drawRoundedRect(r + QMarginsF(hOffset, 0, hOffset, 0), radius, radius);
	p.setOpacity(opacity * kOpacityStep2);
	p.drawRoundedRect(
		r + QMarginsF(hOffset2, 0, hOffset2, 0),
		radius,
		radius);

	p.setOpacity(opacity * kOpacityStep);
	p.drawRoundedRect(r + QMarginsF(0, 0, 0, vOffset), radius, radius);
	p.setOpacity(opacity * kOpacityStep2);
	p.drawRoundedRect(r + QMarginsF(0, 0, 0, vOffset2), radius, radius);

	p.setOpacity(opacity);
}

} // namespace

void PaintDetails(
		QPainter &p,
		const Data::StatisticalChart::Line &line,
		int absoluteValue,
		const QRect &rect) {
	auto name = Ui::Text::String(
		st::statisticsDetailsPopupStyle,
		line.name);
	auto value = Ui::Text::String(
		st::statisticsDetailsPopupStyle,
		Lang::FormatCountDecimal(absoluteValue));
	const auto nameWidth = name.maxWidth();
	const auto valueWidth = value.maxWidth();

	const auto width = valueWidth
		+ rect::m::sum::h(st::statisticsDetailsPopupMargins)
		+ rect::m::sum::h(st::statisticsDetailsPopupPadding)
		+ st::statisticsDetailsPopupPadding.left() // Between strings.
		+ nameWidth;

	const auto height = st::statisticsDetailsPopupStyle.font->height
		+ rect::m::sum::v(st::statisticsDetailsPopupMargins)
		+ rect::m::sum::v(st::statisticsDetailsPopupPadding);

	const auto fullRect = QRect(
		rect.x() + rect.width() - width,
		rect.y(),
		width,
		height);

	const auto innerRect = fullRect - st::statisticsDetailsPopupPadding;
	const auto textRect = innerRect - st::statisticsDetailsPopupMargins;

	p.setBrush(st::shadowFg);
	p.setPen(Qt::NoPen);
	PaintShadow(p, st::boxRadius, innerRect);
	Ui::FillRoundRect(p, innerRect, st::boxBg, Ui::BoxCorners);

	const auto lineY = textRect.y();
	const auto valueContext = Ui::Text::PaintContext{
		.position = QPoint(rect::right(textRect) - valueWidth, lineY),
		.outerWidth = textRect.width(),
		.availableWidth = valueWidth,
	};
	const auto nameContext = Ui::Text::PaintContext{
		.position = QPoint(textRect.x(), lineY),
		.outerWidth = textRect.width(),
		.availableWidth = textRect.width() - valueWidth,
	};
	p.setPen(st::boxTextFg);
	name.draw(p, nameContext);
	p.setPen(line.color);
	value.draw(p, valueContext);
}

PointDetailsWidget::PointDetailsWidget(
	not_null<Ui::RpWidget*> parent,
	const Data::StatisticalChart &chartData,
	bool zoomEnabled)
: Ui::AbstractButton(parent)
, _zoomEnabled(zoomEnabled)
, _chartData(chartData)
, _textStyle(st::statisticsDetailsPopupStyle)
, _headerStyle(st::statisticsDetailsPopupHeaderStyle) {
	if (zoomEnabled) {
		rpl::single(rpl::empty_value()) | rpl::then(
			style::PaletteChanged()
		) | rpl::start_with_next([=] {
			const auto w = st::statisticsDetailsArrowShift;
			const auto stroke = style::ConvertScaleExact(
				st::statisticsDetailsArrowStroke);
			_arrow = QImage(
				QSize(w + stroke, w * 2 + stroke) * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			_arrow.setDevicePixelRatio(style::DevicePixelRatio());
			_arrow.fill(Qt::transparent);
			{
				auto p = QPainter(&_arrow);

				const auto hq = PainterHighQualityEnabler(p);
				const auto s = stroke / 2.;

				p.setPen(QPen(st::windowSubTextFg, stroke));
				p.drawLine(QLineF(s, s, w, w + s));
				p.drawLine(QLineF(s, s + w * 2, w, w + s));
			}
			invalidateCache();
		}, lifetime());
	}

	_maxPercentageWidth = [&] {
		if (_chartData.hasPercentages) {
			const auto maxPercentageText = Ui::Text::String(
				_textStyle,
				u"10000%"_q);
			return maxPercentageText.maxWidth();
		}
		return 0;
	}();

	const auto hasUsdLine = (_chartData.currencyRate != 0)
		&& (_chartData.currency != Data::StatisticalCurrency::None)
		&& (_chartData.lines.size() == 1);

	const auto maxValueTextWidth = [&] {
		if (hasUsdLine) {
			auto maxValueWidth = 0;
			const auto multiplier = float64(kOneStarInNano);
			for (const auto &value : _chartData.lines.front().y) {
				const auto valueText = Ui::Text::String(
					_textStyle,
					Lang::FormatExactCountDecimal(value / multiplier));
				const auto usdText = Ui::Text::String(
					_textStyle,
					Info::ChannelEarn::ToUsd(
						value / multiplier,
						_chartData.currencyRate,
						0));
				const auto width = std::max(
					usdText.maxWidth(),
					valueText.maxWidth());
				if (width > maxValueWidth) {
					maxValueWidth = width;
				}
			}
			return maxValueWidth;
		}
		const auto maxAbsoluteValue = [&] {
			auto maxValue = ChartValue(0);
			for (const auto &l : _chartData.lines) {
				maxValue = std::max(l.maxValue, maxValue);
			}
			return maxValue;
		}();
		const auto maxValueText = Ui::Text::String(
			_textStyle,
			Lang::FormatCountDecimal(maxAbsoluteValue));
		return maxValueText.maxWidth();
	}();

	const auto calculatedWidth = [&]{
		auto maxNameTextWidth = 0;
		const auto isCredits
			= _chartData.currency == Data::StatisticalCurrency::Credits;
		for (const auto &dataLine : _chartData.lines) {
			const auto maxNameText = Ui::Text::String(
				_textStyle,
				dataLine.name);
			maxNameTextWidth = std::max(
				maxNameText.maxWidth(),
				maxNameTextWidth);
			if (hasUsdLine) {
				const auto text = isCredits
					? tr::lng_channel_earn_chart_overriden_detail_credits
					: tr::lng_channel_earn_chart_overriden_detail_currency;
				const auto currency = Ui::Text::String(
					_textStyle,
					text(tr::now));
				const auto usd = Ui::Text::String(
					_textStyle,
					tr::lng_channel_earn_chart_overriden_detail_usd(
						tr::now));
				maxNameTextWidth = std::max(
					std::max(currency.maxWidth(), usd.maxWidth()),
					maxNameTextWidth);
			}
		}
		{
			const auto maxHeaderText = Ui::Text::String(
				_headerStyle,
				_chartData.weekFormat
					? FormatWeek(_chartData.x.front())
					: LangDetailedDayMonth(_chartData.x.front() / 1000));
			maxNameTextWidth = std::max(
				maxHeaderText.maxWidth()
					+ st::statisticsDetailsPopupPadding.left(),
				maxNameTextWidth);
		}
		return maxValueTextWidth
			+ rect::m::sum::h(st::statisticsDetailsPopupMargins)
			+ rect::m::sum::h(st::statisticsDetailsPopupPadding)
			+ st::statisticsDetailsPopupPadding.left() // Between strings.
			+ maxNameTextWidth
			+ (_valueIcon.isNull()
				? 0
				: _valueIcon.width() / style::DevicePixelRatio())
			+ _maxPercentageWidth;
	}();
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto fullRect = s.isNull()
			? Rect(Size(calculatedWidth))
			: Rect(s);
		_innerRect = fullRect - st::statisticsDetailsPopupPadding;
		_textRect = _innerRect - st::statisticsDetailsPopupMargins;
		invalidateCache();
	}, lifetime());

	resize(calculatedWidth, height());
	resizeHeight();
}

void PointDetailsWidget::setLineAlpha(int lineId, float64 alpha) {
	for (auto &line : _lines) {
		if (line.id == lineId) {
			if (line.alpha != alpha) {
				line.alpha = alpha;
				resizeHeight();
				invalidateCache();
				update();
			}
			return;
		}
	}
}

void PointDetailsWidget::resizeHeight() {
	resize(
		width(),
		lineYAt(_chartData.lines.size() + (_chartData.currencyRate ? 1 : 0))
			+ st::statisticsDetailsPopupMargins.bottom());
}

int PointDetailsWidget::xIndex() const {
	return _xIndex;
}

void PointDetailsWidget::setXIndex(int xIndex) {
	_xIndex = xIndex;
	if (xIndex < 0) {
		return;
	}
	Assert(xIndex < _chartData.x.size());
	{
		constexpr auto kOneDay = 3600 * 24 * 1000;
		const auto timestamp = _chartData.x[xIndex];
		_header.setText(
			_headerStyle,
			(timestamp < kOneDay)
				? _chartData.getDayString(xIndex)
				: _chartData.weekFormat
				? FormatWeek(timestamp)
				: LangDetailedDayMonth(timestamp / 1000));
	}

	_lines.clear();
	_lines.reserve(_chartData.lines.size());
	auto hasPositiveValues = false;
	const auto parts = _maxPercentageWidth
		? PiePartsPercentageByIndices(
			_chartData,
			nullptr,
			{ float64(xIndex), float64(xIndex) }).parts
		: std::vector<PiePartData::Part>();
	const auto isCredits
		= (_chartData.currency == Data::StatisticalCurrency::Credits);
	for (auto i = 0; i < _chartData.lines.size(); i++) {
		const auto &dataLine = _chartData.lines[i];
		Assert(xIndex < dataLine.y.size());
		auto textLine = Line();
		textLine.id = dataLine.id;
		if (_maxPercentageWidth) {
			textLine.percentage.setText(_textStyle, parts[i].percentageText);
		}
		textLine.name.setText(_textStyle, dataLine.name);
		textLine.value.setText(
			_textStyle,
			Lang::FormatCountDecimal(dataLine.y[xIndex]));
		hasPositiveValues |= (dataLine.y[xIndex] > 0);
		textLine.valueColor = QColor(dataLine.color);
		if (_chartData.currencyRate) {
			auto copy = Line();
			copy.id = dataLine.id * 100;
			copy.valueColor = QColor(dataLine.color);
			copy.name.setText(
				_textStyle,
				(isCredits
					? tr::lng_channel_earn_chart_overriden_detail_credits
					: tr::lng_channel_earn_chart_overriden_detail_currency)(
						tr::now));
			const auto provided = dataLine.y[xIndex];
			const auto value = isCredits
				? CreditsAmount(provided, CreditsType::Stars)
				: CreditsAmount(
					provided / kOneStarInNano,
					provided % kOneStarInNano,
					CreditsType::Ton);
			copy.value.setText(
				_textStyle,
				Lang::FormatCreditsAmountDecimal(value));
			_lines.push_back(std::move(copy));
			textLine.name.setText(
				_textStyle,
				tr::lng_channel_earn_chart_overriden_detail_usd(tr::now));
			textLine.value.setText(
				_textStyle,
				Info::ChannelEarn::ToUsd(value, _chartData.currencyRate, 0));
		}
		_lines.push_back(std::move(textLine));
	}
	if (_chartData.currencyRate && _valueIcon.isNull()) {
		_valueIcon = ChartCurrencyIcon(_chartData, _lines.front().valueColor);
	}
	const auto clickable = _zoomEnabled && hasPositiveValues;
	_hasPositiveValues = hasPositiveValues;
	QWidget::setAttribute(
		Qt::WA_TransparentForMouseEvents,
		!clickable);
	invalidateCache();
}

void PointDetailsWidget::setAlpha(float64 alpha) {
	_alpha = alpha;
	update();
}

float64 PointDetailsWidget::alpha() const {
	return _alpha;
}

int PointDetailsWidget::lineYAt(int index) const {
	auto linesHeight = 0.;
	for (auto i = 0; i < index; i++) {
		const auto alpha = (i >= _lines.size()) ? 1. : _lines[i].alpha;
		linesHeight += alpha
			* (_textStyle.font->height
				+ st::statisticsDetailsPopupMidLineSpace);
	}

	return _textRect.y()
		+ _headerStyle.font->height
		+ st::statisticsDetailsPopupMargins.bottom() / 2
		+ std::ceil(linesHeight);
}

void PointDetailsWidget::invalidateCache() {
	_cache = QImage();
}

void PointDetailsWidget::mousePressEvent(QMouseEvent *e) {
	AbstractButton::mousePressEvent(e);
	const auto position = e->pos() - _innerRect.topLeft();
	if (!_ripple) {
		_ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			Ui::RippleAnimation::RoundRectMask(
				_innerRect.size(),
				st::boxRadius),
			[=] { update(); });
	}
	_ripple->add(position);
}

void PointDetailsWidget::mouseReleaseEvent(QMouseEvent *e) {
	AbstractButton::mouseReleaseEvent(e);
	if (_ripple) {
		_ripple->lastStop();
	}
}

void PointDetailsWidget::paintEvent(QPaintEvent *e) {
	auto painter = QPainter(this);

	if (_cache.isNull()) {
		_cache = QImage(
			size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		_cache.setDevicePixelRatio(style::DevicePixelRatio());
		_cache.fill(Qt::transparent);

		auto p = QPainter(&_cache);

		p.setBrush(st::shadowFg);
		p.setPen(Qt::NoPen);
		PaintShadow(p, st::boxRadius, _innerRect);
		Ui::FillRoundRect(p, _innerRect, st::boxBg, Ui::BoxCorners);

		if (_ripple) {
			_ripple->paint(p, _innerRect.left(), _innerRect.top(), width());
			if (_ripple->empty()) {
				_ripple.reset();
			}
		}

		p.setPen(st::boxTextFg);
		const auto headerContext = Ui::Text::PaintContext{
			.position = _textRect.topLeft(),
			.availableWidth = _textRect.width(),
		};
		_header.draw(p, headerContext);
		for (auto i = 0; i < _lines.size(); i++) {
			const auto &line = _lines[i];
			const auto lineY = lineYAt(i);
			const auto valueWidth = line.value.maxWidth();
			const auto valueContext = Ui::Text::PaintContext{
				.position = QPoint(
					rect::right(_textRect) - valueWidth,
					lineY),
				.outerWidth = _textRect.width(),
				.availableWidth = valueWidth,
			};
			if (!i && !_valueIcon.isNull()) {
				p.drawImage(
					valueContext.position.x()
						- _valueIcon.width() / style::DevicePixelRatio(),
					lineY + st::lineWidth,
					_valueIcon);
			}
			const auto nameContext = Ui::Text::PaintContext{
				.position = QPoint(
					_textRect.x() + _maxPercentageWidth,
					lineY),
				.outerWidth = _textRect.width(),
				.availableWidth = _textRect.width() - valueWidth,
			};
			p.setOpacity(line.alpha * line.alpha);
			p.setPen(st::boxTextFg);
			if (_maxPercentageWidth) {
				const auto percentageContext = Ui::Text::PaintContext{
					.position = QPoint(_textRect.x(), lineY),
					.outerWidth = _textRect.width(),
					.availableWidth = _textRect.width() - valueWidth,
				};
				line.percentage.draw(p, percentageContext);
			}
			line.name.draw(p, nameContext);
			p.setPen(line.valueColor);
			line.value.draw(p, valueContext);
		}

		if (_zoomEnabled && _hasPositiveValues) {
			const auto s = _arrow.size() / style::DevicePixelRatio();
			const auto x = rect::right(_textRect) - s.width();
			const auto y = _textRect.y()
				+ (_headerStyle.font->ascent - s.height());
			p.drawImage(x, y, _arrow);
		}
	}
	if (_alpha < 1.) {
		painter.setOpacity(_alpha);
	}
	painter.drawImage(0, 0, _cache);
	if (_ripple) {
		invalidateCache();
	}
}

} // namespace Statistic
