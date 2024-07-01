/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/chart_rulers_view.h"

#include "data/data_channel_earn.h" // Data::kEarnMultiplier.
#include "info/channel_statistics/earn/earn_format.h"
#include "lang/lang_keys.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/statistics_common.h"
#include "statistics/statistics_graphics.h"
#include "styles/style_basic.h"
#include "styles/style_statistics.h"

namespace Statistic {
namespace {

[[nodiscard]] QString FormatF(float64 absoluteValue) {
	constexpr auto kTooMuch = int(10'000);
	return (absoluteValue >= kTooMuch)
		? Lang::FormatCountToShort(absoluteValue).string
		: QString::number(absoluteValue);
}

} // namespace

ChartRulersView::ChartRulersView() = default;

void ChartRulersView::setChartData(
		const Data::StatisticalChart &chartData,
		ChartViewType type,
		std::shared_ptr<LinesFilterController> linesFilter) {
	_rulers.clear();
	_isDouble = (type == ChartViewType::DoubleLinear)
		|| chartData.currencyRate;
	if (chartData.currencyRate) {
		_currencyIcon = ChartCurrencyIcon(chartData, {});
		_leftCustomCaption = [=](float64 value) {
			return FormatF(value / float64(Data::kEarnMultiplier));
		};
		_rightCustomCaption = [=, rate = chartData.currencyRate](float64 v) {
			return Info::ChannelEarn::ToUsd(v, rate);
		};
		_rightPen = QPen(st::windowSubTextFg);
	}
	if (_isDouble && (chartData.lines.size() == 2)) {
		_linesFilter = std::move(linesFilter);
		_leftPen = QPen(chartData.lines.front().color);
		_rightPen = QPen(chartData.lines.back().color);
		_leftLineId = chartData.lines.front().id;
		_rightLineId = chartData.lines.back().id;

		const auto firstMax = chartData.lines.front().maxValue;
		const auto secondMax = chartData.lines.back().maxValue;
		if (firstMax > secondMax) {
			_isLeftLineScaled = false;
			_scaledLineRatio = firstMax / float64(secondMax);
		} else {
			_isLeftLineScaled = true;
			_scaledLineRatio = secondMax / float64(firstMax);
		}
	}
}

void ChartRulersView::paintRulers(
		QPainter &p,
		const QRect &r) {
	const auto alpha = p.opacity();
	for (auto &ruler : _rulers) {
		p.setOpacity(alpha * ruler.alpha * kRulerLineAlpha);
		for (const auto &line : ruler.lines) {
			const auto lineRect = QRect(
				0,
				r.y() + r.height() * line.relativeValue,
				r.x() + r.width(),
				st::lineWidth);
			p.fillRect(lineRect, st::boxTextFg);
		}
	}
	p.setOpacity(alpha);
}

void ChartRulersView::paintCaptionsToRulers(
		QPainter &p,
		const QRect &r) {
	const auto offset = r.y() - st::statisticsChartRulerCaptionSkip;
	p.setFont(st::statisticsDetailsBottomCaptionStyle.font);
	const auto alpha = p.opacity();
	for (auto &ruler : _rulers) {
		const auto rulerAlpha = alpha * ruler.alpha;
		p.setOpacity(rulerAlpha);
		const auto left = _currencyIcon.isNull()
			 ? 0
			 : _currencyIcon.width() / style::DevicePixelRatio();
		for (const auto &line : ruler.lines) {
			const auto y = offset + r.height() * line.relativeValue;
			const auto hasLinesFilter = _isDouble && _linesFilter;
			if (hasLinesFilter) {
				p.setPen(_leftPen);
				p.setOpacity(rulerAlpha * _linesFilter->alpha(_leftLineId));
			} else {
				p.setPen(st::windowSubTextFg);
			}
			if (!_currencyIcon.isNull()) {
				const auto iconTop = y
					- _currencyIcon.height() / style::DevicePixelRatio()
					+ st::statisticsChartRulerCaptionSkip;
				p.drawImage(0, iconTop, _currencyIcon);
			}
			p.drawText(
				left,
				y,
				(!_isDouble)
					? line.caption
					: _isLeftLineScaled
					? line.scaledLineCaption
					: line.caption);
			if (hasLinesFilter || _rightCustomCaption) {
				if (_linesFilter) {
					p.setOpacity(rulerAlpha * _linesFilter->alpha(_rightLineId));
				}
				p.setPen(_rightPen);
				p.drawText(
					r.width() - line.rightCaptionWidth,
					y,
					_isLeftLineScaled
						? line.caption
						: line.scaledLineCaption);
			}
		}
	}
	p.setOpacity(alpha);
}

void ChartRulersView::computeRelative(
		int newMaxHeight,
		int newMinHeight) {
	for (auto &ruler : _rulers) {
		ruler.computeRelative(newMaxHeight, newMinHeight);
	}
}

void ChartRulersView::setAlpha(float64 value) {
	for (auto &ruler : _rulers) {
		ruler.alpha = ruler.fixedAlpha * (1. - value);
	}
	_rulers.back().alpha = value;
	if (value == 1.) {
		while (_rulers.size() > 1) {
			const auto startIt = begin(_rulers);
			if (!startIt->alpha) {
				_rulers.erase(startIt);
			} else {
				break;
			}
		}
	}
}

void ChartRulersView::add(Limits newHeight, bool animated) {
	auto newLinesData = ChartRulersData(
		newHeight.max,
		newHeight.min,
		true,
		_isDouble ? _scaledLineRatio : 0.,
		_leftCustomCaption,
		_rightCustomCaption);
	if (_isDouble) {
		const auto &font = st::statisticsDetailsBottomCaptionStyle.font;
		for (auto &line : newLinesData.lines) {
			line.rightCaptionWidth = font->width(_isLeftLineScaled
				? line.caption
				: line.scaledLineCaption);
		}
	}
	if (!animated) {
		_rulers.clear();
	}
	for (auto &ruler : _rulers) {
		ruler.fixedAlpha = ruler.alpha;
	}
	_rulers.push_back(newLinesData);
	if (!animated) {
		_rulers.back().alpha = 1.;
	}
}

} // namespace Statistic
