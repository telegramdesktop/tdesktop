/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/widgets/chart_lines_filter_widget.h"

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_basic.h"
#include "styles/style_statistics.h"
#include "styles/style_widgets.h"

namespace Statistic {
namespace {
constexpr auto kShiftDuration = crl::time(300);
} // namespace

class ChartLinesFilterWidget::FlatCheckbox final : public Ui::AbstractButton {
public:
	FlatCheckbox(
		not_null<Ui::RpWidget*> parent,
		const QString &text,
		QColor activeColor);

	void shake();
	void setChecked(bool value, bool animated);
	[[nodiscard]] bool checked() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const QColor _inactiveTextColor;
	const QColor _activeColor;
	const QColor _inactiveColor;
	Ui::Text::String _text;

	Ui::Animations::Simple _animation;

	struct {
		Ui::Animations::Simple animation;
		int shift = 0;
	} _shake;

	bool _checked = true;

};

ChartLinesFilterWidget::FlatCheckbox::FlatCheckbox(
	not_null<Ui::RpWidget*> parent,
	const QString &text,
	QColor activeColor)
: Ui::AbstractButton(parent)
, _inactiveTextColor(st::premiumButtonFg->c)
, _activeColor(activeColor)
, _inactiveColor(st::boxBg->c)
, _text(st::statisticsDetailsPopupStyle, text) {
	const auto &margins = st::statisticsChartFlatCheckboxMargins;
	const auto h = _text.minHeight() + rect::m::sum::v(margins) * 2;
	resize(
		_text.maxWidth()
			+ rect::m::sum::h(margins)
			+ h
			+ st::statisticsChartFlatCheckboxCheckWidth * 3
			- st::statisticsChartFlatCheckboxShrinkkWidth,
		h);
}

void ChartLinesFilterWidget::FlatCheckbox::setChecked(
		bool value,
		bool animated) {
	if (_checked == value) {
		return;
	}
	_checked = value;
	if (!animated) {
		_animation.stop();
	} else {
		const auto from = value ? 0. : 1.;
		const auto to = value ? 1. : 0.;
		_animation.start([=] { update(); }, from, to, kShiftDuration);
	}
}

bool ChartLinesFilterWidget::FlatCheckbox::checked() const {
	return _checked;
}

void ChartLinesFilterWidget::FlatCheckbox::shake() {
	if (_shake.animation.animating()) {
		return;
	}
	constexpr auto kShiftProgress = 6;
	constexpr auto kSegmentsCount = 5;
	const auto refresh = [=] {
		const auto fullProgress = _shake.animation.value(1.) * kShiftProgress;
		const auto segment = std::clamp(
			int(std::floor(fullProgress)),
			0,
			kSegmentsCount);
		const auto part = fullProgress - segment;
		const auto from = (segment == 0)
			? 0.
			: (segment == 1 || segment == 3 || segment == 5)
			? 1.
			: -1.;
		const auto to = (segment == 0 || segment == 2 || segment == 4)
			? 1.
			: (segment == 1 || segment == 3)
			? -1.
			: 0.;
		const auto shift = from * (1. - part) + to * part;
		_shake.shift = int(base::SafeRound(shift * st::shakeShift));
		update();
	};
	_shake.animation.start(refresh, 0., 1., kShiftDuration);
}

void ChartLinesFilterWidget::FlatCheckbox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto progress = _animation.value(_checked ? 1. : 0.);

	p.translate(_shake.shift, 0);

	const auto checkWidth = st::statisticsChartFlatCheckboxCheckWidth;
	const auto r = rect() - st::statisticsChartFlatCheckboxMargins;
	const auto heightHalf = r.height() / 2.;
	const auto textX = anim::interpolate(
		r.center().x() - _text.maxWidth() / 2.,
		r.x() + heightHalf + checkWidth * 5,
		progress);
	const auto textY = (r - st::statisticsChartFlatCheckboxMargins).y();
	p.fillRect(r, Qt::transparent);

	constexpr auto kCheckPartProgress = 0.5;
	const auto checkProgress = progress / kCheckPartProgress;
	const auto textColor = (progress <= kCheckPartProgress)
		? anim::color(_activeColor, _inactiveTextColor, checkProgress)
		: _inactiveTextColor;
	const auto fillColor = (progress <= kCheckPartProgress)
		? anim::color(_inactiveColor, _activeColor, checkProgress)
		: _activeColor;

	p.setPen(QPen(_activeColor, st::statisticsChartLineWidth));
	p.setBrush(fillColor);
	const auto radius = r.height() / 2.;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(r, radius, radius);
	}

	p.setPen(textColor);
	const auto textContext = Ui::Text::PaintContext{
		.position = QPoint(textX, textY),
		.availableWidth = width(),
	};
	_text.draw(p, textContext);

	if (progress > kCheckPartProgress) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(textColor, st::statisticsChartLineWidth));
		const auto bounceProgress = checkProgress - 1.;
		const auto start = QPoint(
			r.x() + heightHalf + checkWidth,
			textY + _text.style()->font->ascent);
		p.translate(start);
		p.drawLine({}, -QPoint(checkWidth, checkWidth) * bounceProgress);
		p.drawLine({}, QPoint(checkWidth, -checkWidth) * bounceProgress * 2);
	}
}

ChartLinesFilterWidget::ChartLinesFilterWidget(
	not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent) {
}

void ChartLinesFilterWidget::resizeToWidth(int outerWidth) {
	auto maxRight = 0;
	for (auto i = 0; i < _buttons.size(); i++) {
		const auto raw = _buttons[i].get();
		if (!i) {
			raw->move(0, 0);
		} else {
			const auto prevRaw = _buttons[i - 1].get();
			const auto prevLeft = rect::right(prevRaw);
			const auto isOut = (prevLeft + raw->width() > outerWidth);
			const auto left = isOut ? 0 : prevLeft;
			const auto top = isOut ? rect::bottom(prevRaw) : prevRaw->y();
			raw->move(left, top);
		}
		maxRight = std::max(maxRight, rect::right(raw));
	}
	if (!_buttons.empty()) {
		resize(maxRight, rect::bottom(_buttons.back().get()));
	}
}

void ChartLinesFilterWidget::fillButtons(
		const std::vector<ButtonData> &buttonsData) {
	_buttons.clear();

	_buttons.reserve(buttonsData.size());
	for (auto i = 0; i < buttonsData.size(); i++) {
		const auto &buttonData = buttonsData[i];
		auto button = base::make_unique_q<FlatCheckbox>(
			this,
			buttonData.text,
			buttonData.color);
		button->show();
		if (buttonData.disabled) {
			button->setChecked(false, false);
		}
		const auto id = buttonData.id;
		button->setClickedCallback([=, raw = button.get()] {
			const auto checked = !raw->checked();
			if (!checked) {
				const auto cancel = [&] {
					for (const auto &b : _buttons) {
						if (b.get() == raw) {
							continue;
						}
						if (b->checked()) {
							return false;
						}
					}
					return true;
				}();
				if (cancel) {
					raw->shake();
					return;
				}
			}
			raw->setChecked(checked, true);
			_buttonEnabledChanges.fire({ .id = id, .enabled = checked });
		});

		_buttons.push_back(std::move(button));
	}
}

auto ChartLinesFilterWidget::buttonEnabledChanges() const
-> rpl::producer<ChartLinesFilterWidget::Entry> {
	return _buttonEnabledChanges.events();
}

} // namespace Statistic
