/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/point_details_widget.h"

#include "ui/cached_round_corners.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/shadow.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

namespace Statistic {
namespace {

[[nodiscard]] QString FormatTimestamp(
		float64 timestamp,
		const QString &longFormat,
		const QString &shortFormat) {
	const auto dateTime = QDateTime::fromSecsSinceEpoch(timestamp / 1000);
	if (dateTime.toUTC().time().hour() || dateTime.toUTC().time().minute()) {
		return QLocale().toString(dateTime, longFormat);
	} else {
		return QLocale().toString(dateTime.date(), shortFormat);
	}
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
		QString("%L1").arg(absoluteValue));
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

	Ui::Shadow::paint(p, innerRect, rect.width(), st::boxRoundShadow);
	Ui::FillRoundRect(p, innerRect, st::boxBg, Ui::BoxCorners);

	const auto lineY = textRect.y();
	const auto valueContext = Ui::Text::PaintContext{
		.position = QPoint(rect::right(textRect) - valueWidth, lineY),
	};
	const auto nameContext = Ui::Text::PaintContext{
		.position = QPoint(textRect.x(), lineY),
		.outerWidth = textRect.width() - valueWidth,
		.availableWidth = textRect.width(),
	};
	p.setPen(st::boxTextFg);
	name.draw(p, nameContext);
	p.setPen(line.color);
	value.draw(p, valueContext);
}

PointDetailsWidget::PointDetailsWidget(
	not_null<Ui::RpWidget*> parent,
	const Data::StatisticalChart &chartData,
	float64 maxAbsoluteValue,
	bool zoomEnabled)
: Ui::RippleButton(parent, st::defaultRippleAnimation)
, _zoomEnabled(zoomEnabled)
, _chartData(chartData)
, _textStyle(st::statisticsDetailsPopupStyle)
, _headerStyle(st::statisticsDetailsPopupHeaderStyle)
, _longFormat(u"ddd, MMM d hh:mm"_q)
, _shortFormat(u"ddd, MMM d"_q) {

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
			_arrow.fill(Qt::transparent);
			{
				auto p = QPainter(&_arrow);

				const auto hq = PainterHighQualityEnabler(p);
				const auto s = stroke / 2.;

				p.setPen(QPen(st::windowSubTextFg, stroke));
				p.drawLine(QLineF(s, s, w, w + s));
				p.drawLine(QLineF(s, s + w * 2, w, w + s));
			}
		}, lifetime());
	}

	const auto calculatedWidth = [&]{
		const auto maxValueText = Ui::Text::String(
			_textStyle,
			QString("%L1").arg(maxAbsoluteValue));
		const auto maxValueTextWidth = maxValueText.maxWidth();

		auto maxNameTextWidth = 0;
		for (const auto &dataLine : _chartData.lines) {
			const auto maxNameText = Ui::Text::String(
				_textStyle,
				dataLine.name);
			maxNameTextWidth = std::max(
				maxNameText.maxWidth(),
				maxNameTextWidth);
		}
		{
			const auto maxHeaderText = Ui::Text::String(
				_headerStyle,
				FormatTimestamp(
					_chartData.x.front(),
					_longFormat,
					_shortFormat));
			maxNameTextWidth = std::max(
				maxHeaderText.maxWidth()
					+ st::statisticsDetailsPopupPadding.left(),
				maxNameTextWidth);
		}
		return maxValueTextWidth
			+ rect::m::sum::h(st::statisticsDetailsPopupMargins)
			+ rect::m::sum::h(st::statisticsDetailsPopupPadding)
			+ st::statisticsDetailsPopupPadding.left() // Between strings.
			+ maxNameTextWidth;
	}();
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto fullRect = s.isNull()
			? Rect(Size(calculatedWidth))
			: Rect(s);
		_innerRect = fullRect - st::statisticsDetailsPopupPadding;
		_textRect = _innerRect - st::statisticsDetailsPopupMargins;
	}, lifetime());

	resize(calculatedWidth, height());
	resizeHeight();
}

void PointDetailsWidget::setLineAlpha(int lineId, float64 alpha) {
	for (auto &line : _lines) {
		if (line.id == lineId) {
			line.alpha = alpha;
		}
	}
	update();
	resizeHeight();
}

void PointDetailsWidget::resizeHeight() {
	resize(
		width(),
		lineYAt(_chartData.lines.size())
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
	_header.setText(
		_headerStyle,
		FormatTimestamp(_chartData.x[xIndex], _longFormat, _shortFormat));

	_lines.clear();
	_lines.reserve(_chartData.lines.size());
	auto hasPositiveValues = false;
	for (const auto &dataLine : _chartData.lines) {
		auto textLine = Line();
		textLine.id = dataLine.id;
		textLine.name.setText(_textStyle, dataLine.name);
		textLine.value.setText(
			_textStyle,
			QString("%L1").arg(dataLine.y[xIndex]));
		hasPositiveValues |= (dataLine.y[xIndex] > 0);
		textLine.valueColor = QColor(dataLine.color);
		_lines.push_back(std::move(textLine));
	}
	const auto clickable = _zoomEnabled && hasPositiveValues;
	setAttribute(
		Qt::WA_TransparentForMouseEvents,
		!clickable);
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

void PointDetailsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.setOpacity(_alpha);

	const auto fullRect = rect();

	Ui::Shadow::paint(p, _innerRect, width(), st::boxRoundShadow);
	Ui::FillRoundRect(p, _innerRect, st::boxBg, Ui::BoxCorners);
	Ui::RippleButton::paintRipple(p, _innerRect.topLeft());

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
			.position = QPoint(rect::right(_textRect) - valueWidth, lineY),
			.outerWidth = _textRect.width(),
			.availableWidth = valueWidth,
		};
		const auto nameContext = Ui::Text::PaintContext{
			.position = QPoint(_textRect.x(), lineY),
			.outerWidth = _textRect.width(),
			.availableWidth = _textRect.width() - valueWidth,
		};
		p.setOpacity(line.alpha * line.alpha * _alpha);
		p.setPen(st::boxTextFg);
		line.name.draw(p, nameContext);
		p.setPen(line.valueColor);
		line.value.draw(p, valueContext);
	}

	if (_zoomEnabled) {
		const auto s = _arrow.size() / style::DevicePixelRatio();
		const auto x = rect::right(_textRect) - s.width();
		const auto y = _textRect.y()
			+ (_headerStyle.font->height - s.height()) / 2.;
		p.drawImage(x, y, _arrow);
	}
}

QPoint PointDetailsWidget::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _innerRect.topLeft();
}

QImage PointDetailsWidget::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		_innerRect.size(),
		st::boxRadius);
}

} // namespace Statistic
