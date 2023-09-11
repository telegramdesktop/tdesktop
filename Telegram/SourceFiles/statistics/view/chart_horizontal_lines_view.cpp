/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/chart_horizontal_lines_view.h"

#include "data/data_statistics.h"
#include "statistics/statistics_common.h"
#include "styles/style_statistics.h"

namespace Statistic {

ChartHorizontalLinesView::ChartHorizontalLinesView() = default;

void ChartHorizontalLinesView::setChartData(
		const Data::StatisticalChart &chartData,
		ChartViewType type) {
	_horizontalLines.clear();
	_isDouble = (type == ChartViewType::DoubleLinear);
	if (_isDouble && (chartData.lines.size() == 2)) {
		_leftPen = QPen(chartData.lines.front().color);
		_rightPen = QPen(chartData.lines.back().color);

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

void ChartHorizontalLinesView::paintHorizontalLines(
		QPainter &p,
		const QRect &r) {
	for (auto &horizontalLine : _horizontalLines) {
		const auto alpha = p.opacity();
		p.setOpacity(horizontalLine.alpha);
		for (const auto &line : horizontalLine.lines) {
			const auto lineRect = QRect(
				0,
				r.y() + r.height() * line.relativeValue,
				r.x() + r.width(),
				st::lineWidth);
			p.fillRect(lineRect, st::windowSubTextFg);
		}
		p.setOpacity(alpha);
	}
}

void ChartHorizontalLinesView::paintCaptionsToHorizontalLines(
		QPainter &p,
		const QRect &r) {
	const auto offset = r.y() - st::statisticsChartHorizontalLineCaptionSkip;
	p.setFont(st::statisticsDetailsBottomCaptionStyle.font);
	for (auto &horizontalLine : _horizontalLines) {
		const auto alpha = p.opacity();
		p.setOpacity(horizontalLine.alpha);
		for (const auto &line : horizontalLine.lines) {
			const auto y = offset + r.height() * line.relativeValue;
			p.setPen(_isDouble ? _leftPen : st::windowSubTextFg);
			p.drawText(
				0,
				y,
				(!_isDouble)
					? line.caption
					: _isLeftLineScaled
					? line.scaledLineCaption
					: line.caption);
			if (_isDouble) {
				p.setPen(_rightPen);
				p.drawText(
					r.width() - line.rightCaptionWidth,
					y,
					_isLeftLineScaled
						? line.caption
						: line.scaledLineCaption);
			}
		}
		p.setOpacity(alpha);
	}
}

void ChartHorizontalLinesView::computeRelative(
		int newMaxHeight,
		int newMinHeight) {
	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.computeRelative(newMaxHeight, newMinHeight);
	}
}

void ChartHorizontalLinesView::setAlpha(float64 value) {
	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.alpha = horizontalLine.fixedAlpha * (1. - value);
	}
	_horizontalLines.back().alpha = value;
	if (value == 1.) {
		while (_horizontalLines.size() > 1) {
			const auto startIt = begin(_horizontalLines);
			if (!startIt->alpha) {
				_horizontalLines.erase(startIt);
			} else {
				break;
			}
		}
	}
}

void ChartHorizontalLinesView::add(Limits newHeight, bool animated) {
	auto newLinesData = ChartHorizontalLinesData(
		newHeight.max,
		newHeight.min,
		true,
		_isDouble ? _scaledLineRatio : 0.);
	if (_isDouble) {
		const auto &font = st::statisticsDetailsBottomCaptionStyle.font;
		for (auto &line : newLinesData.lines) {
			line.rightCaptionWidth = font->width(_isLeftLineScaled
				? line.caption
				: line.scaledLineCaption);
		}
	}
	if (!animated) {
		_horizontalLines.clear();
	}
	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.fixedAlpha = horizontalLine.alpha;
	}
	_horizontalLines.push_back(newLinesData);
	if (!animated) {
		_horizontalLines.back().alpha = 1.;
	}
}

} // namespace Statistic
