/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_colors_palette_chooser.h"

#include "settings/settings_chat.h" // Settings::PaintRoundColorButton.
#include "ui/abstract_button.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "styles/style_info_userpic_builder.h"

namespace UserpicBuilder {
namespace {

[[nodiscard]] QLinearGradient VerticalGradient(
		float64 size,
		const QColor &c1,
		const QColor &c2) {
	auto gradient = QLinearGradient(0, 0, size / 2., size);
	gradient.setStops({ { 0., c1 }, { 1., c2 } });
	return gradient;
}

[[nodiscard]] QLinearGradient GradientByIndex(int index, float64 size) {
	const auto colors = Ui::EmptyUserpic::UserpicColor(
		Ui::EmptyUserpic::ColorIndex(index));
	return VerticalGradient(size, colors.color1->c, colors.color2->c);
}

} // namespace

class ColorsPalette::CircleButton final : public Ui::AbstractButton {
public:
	using Ui::AbstractButton::AbstractButton;

	void setIndex(int index);
	[[nodiscard]] int index() const;
	void setBrush(QBrush brush);
	void setSelectedProgress(float64 progress);

private:
	void paintEvent(QPaintEvent *event) override;

	int _index = 0;
	float64 _selectedProgress = 0.;
	QBrush _brush;

};

void ColorsPalette::CircleButton::setIndex(int index) {
	_index = index;
}

int ColorsPalette::CircleButton::index() const {
	return _index;
}

void ColorsPalette::CircleButton::setBrush(QBrush brush) {
	_brush = brush;
	update();
}

void ColorsPalette::CircleButton::setSelectedProgress(float64 progress) {
	if (_selectedProgress != progress) {
		_selectedProgress = progress;
		update();
	}
}

void ColorsPalette::CircleButton::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto h = height();
	Settings::PaintRoundColorButton(p, h, _brush, _selectedProgress);
}

ColorsPalette::ColorsPalette(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent) {
	rebuildButtons();
}

rpl::producer<QGradientStops> ColorsPalette::stopsValue() {
	return _currentIndex.value() | rpl::map([=](int index) {
		return GradientByIndex(index, resizeGetHeight(0)).stops();
	});
}

void ColorsPalette::rebuildButtons() {
	constexpr auto kColorsCount = 7;
	const auto size = resizeGetHeight(0);
	for (auto i = 0; i < kColorsCount; i++) {
		_buttons.emplace_back(base::make_unique_q<CircleButton>(this));
		_buttons.back()->resize(size, size);
		_buttons.back()->setIndex(i);
		_buttons.back()->setBrush(GradientByIndex(i, size));
		_buttons.back()->setClickedCallback([=] {
			const auto was = _currentIndex.current();
			const auto now = i;
			if (was == now) {
				return;
			}
			_animation.stop();
			_animation.start([=](float64 progress) {
				_buttons[was]->setSelectedProgress(1. - progress);
				_buttons[now]->setSelectedProgress(progress);
			}, 0., 1., st::slideDuration);
			_currentIndex = now;
		});
	}
	_buttons[_currentIndex.current()]->setSelectedProgress(1.);

}

void ColorsPalette::resizeEvent(QResizeEvent *event) {
	const auto fullWidth = event->size().width();
	const auto buttonWidth = _buttons.front()->width();
	const auto buttonsCount = _buttons.size();
	const auto buttonsWidth = buttonWidth * buttonsCount;
	const auto step = (fullWidth - buttonsWidth) / (buttonsCount - 1);
	for (auto i = 0; i < buttonsCount; i++) {
		_buttons[i]->moveToLeft(i * (buttonWidth + step), 0);
	}
}

int ColorsPalette::resizeGetHeight(int newWidth) {
	return st::userpicBuilderEmojiAccentColorSize;
}

} // namespace UserpicBuilder
