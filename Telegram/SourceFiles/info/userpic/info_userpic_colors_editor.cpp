/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_colors_editor.h"

#include "base/random.h"
#include "ui/wrap/fade_wrap.h"
#include "info/userpic/info_userpic_emoji_builder_preview.h"
#include "info/userpic/info_userpic_color_circle_button.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/color_editor.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/vertical_list.h"
#include "ui/rect.h"
#include "styles/style_info_userpic_builder.h"
#include "styles/style_boxes.h"

namespace UserpicBuilder {
namespace {

constexpr auto kMaxColors = int(4);

[[nodiscard]] QColor RandomColor(const QColor &c) {
	auto random = bytes::vector(2);
	base::RandomFill(random.data(), random.size());
	auto result = QColor();
	result.setHslF(
		(uchar(random[0]) % 100) / 100.,
		(uchar(random[1]) % 50) / 100. + 0.5,
		c.lightnessF());
	return result;
}

class ColorsLine final : public Ui::RpWidget {
public:
	using Chosen = CircleButton;
	using Success = bool;
	ColorsLine(
		not_null<Ui::RpWidget*> parent,
		not_null<std::vector<QColor>*> colors);

	void init();
	void fillButtons();

	[[nodiscard]] Chosen *chosen() const;
	[[nodiscard]] rpl::producer<Chosen*> chosenChanges() const;

private:
	struct ButtonState {
		bool shown = false;
		int left = 0;
	};
	[[nodiscard]] std::vector<ButtonState> calculatePositionFor(int count);
	void processChange(
		const std::vector<QColor> wasColors,
		const std::vector<QColor> nowColors);
	void setLastChosen() const;

	const not_null<std::vector<QColor>*> _colors;

	base::unique_qptr<Ui::RpWidget> _container;

	std::vector<not_null<CircleButton*>> _colorButtons;
	std::vector<not_null<Ui::FadeWrap<Ui::RpWidget>*>> _wraps;

	Ui::Animations::Simple _chooseAnimation;
	Ui::Animations::Simple _positionAnimation;
	Chosen *_chosen = nullptr;

	rpl::event_stream<Chosen*> _chosenChanges;

};

ColorsLine::ColorsLine(
	not_null<Ui::RpWidget*> parent,
	not_null<std::vector<QColor>*> colors)
: Ui::RpWidget(parent)
, _colors(colors) {
}

void ColorsLine::init() {
	fillButtons();
	processChange(*_colors, *_colors);
	setLastChosen();
}

void ColorsLine::fillButtons() {
	_container = base::make_unique_q<Ui::RpWidget>(this);
	const auto container = _container.get();
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		container->setGeometry(Rect(s));
	}, container->lifetime());

	const auto minus = Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		container,
		object_ptr<Ui::IconButton>(
			container,
			st::userpicBuilderEmojiColorMinus));
	_wraps.push_back(minus);
	minus->toggle(_colors->size() > 1, anim::type::instant);
	minus->entity()->setClickedCallback([=] {
		if (_colors->size() < 2) {
			return;
		}
		const auto wasColors = *_colors;
		_colors->erase(_colors->end() - 1);
		const auto nowColors = *_colors;
		processChange(wasColors, nowColors);
		setLastChosen();
	});

	for (auto i = 0; i < kMaxColors; i++) {
		const auto wrap = Ui::CreateChild<Ui::FadeWrap<CircleButton>>(
			container,
			object_ptr<CircleButton>(container));
		const auto button = wrap->entity();
		button->resize(height(), height());
		button->setIndex(i);
		_wraps.push_back(wrap);
		_colorButtons.push_back(button);
		button->setClickedCallback([=] {
			const auto wasChosen = _chosen;
			_chosen = button;
			const auto nowChosen = _chosen;
			_chosenChanges.fire_copy(_chosen);

			_chooseAnimation.stop();
			_chooseAnimation.start([=](float64 progress) {
				if (wasChosen) {
					wasChosen->setSelectedProgress(1. - progress);
				}
				nowChosen->setSelectedProgress(progress);
			}, 0., 1., st::universalDuration);
		});
		if (i < _colors->size()) {
			button->setBrush((*_colors)[i]);
		} else {
			wrap->hide(anim::type::instant);
		}
	}

	const auto plus = Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		container,
		object_ptr<Ui::IconButton>(
			container,
			st::userpicBuilderEmojiColorPlus));
	_wraps.push_back(plus);
	plus->toggle(_colors->size() < kMaxColors, anim::type::instant);
	plus->entity()->setClickedCallback([=] {
		if (_colors->size() >= kMaxColors) {
			return;
		}
		const auto wasColors = *_colors;
		_colors->push_back(RandomColor(_colors->back()));
		const auto nowColors = *_colors;
		processChange(wasColors, nowColors);
		setLastChosen();
	});
	for (const auto &wrap : _wraps) {
		wrap->setDuration(st::universalDuration);
	}
}

std::vector<ColorsLine::ButtonState> ColorsLine::calculatePositionFor(
		int count) {
	// Minus - Color - Color - Color - Color - Plus.
	auto result = std::vector<ButtonState>(6);
	const auto fullWidth = _container->width();
	const auto width = _container->height();
	const auto colorsWidth = width * count + width * (count - 1);
	const auto left = (fullWidth - colorsWidth) / 2;
	for (auto i = 0; i < _colorButtons.size(); i++) {
		result[i + 1] = {
			.shown = (i < count),
			.left = left + (i * width * 2),
		};
	}
	result[0] = {
		.shown = (count > 1),
		.left = (left - width * 2),
	};
	result[result.size() - 1] = {
		.shown = (count < kMaxColors),
		.left = (left + colorsWidth + width),
	};
	return result;
}

void ColorsLine::processChange(
		const std::vector<QColor> wasColors,
		const std::vector<QColor> nowColors) {
	const auto wasPosition = calculatePositionFor(wasColors.size());
	const auto nowPosition = calculatePositionFor(nowColors.size());
	for (auto i = 0; i < nowPosition.size(); i++) {
		const auto colorIndex = i - 1;
		if ((colorIndex > 0) && (colorIndex < _colors->size())) {
			_colorButtons[colorIndex]->setBrush((*_colors)[colorIndex]);
		}
		_wraps[i]->toggle(nowPosition[i].shown, anim::type::normal);
	}
	_positionAnimation.stop();
	_positionAnimation.start([=](float64 value) {
		for (auto i = 0; i < nowPosition.size(); i++) {
			const auto wasLeft = wasPosition[i].left;
			const auto nowLeft = nowPosition[i].left;
			const auto left = anim::interpolate(wasLeft, nowLeft, value);
			_wraps[i]->moveToLeft(left, 0);
		}
	}, 0., 1., st::universalDuration);
}

void ColorsLine::setLastChosen() const {
	for (auto i = 0; i < _colorButtons.size(); i++) {
		if (i == (_colors->size() - 1)) {
			_colorButtons[i]->clicked({}, Qt::LeftButton);
		}
	}
}

ColorsLine::Chosen *ColorsLine::chosen() const {
	return _chosen;
}

rpl::producer<ColorsLine::Chosen*> ColorsLine::chosenChanges() const {
	return _chosenChanges.events();
}

} // namespace

object_ptr<Ui::RpWidget> CreateGradientEditor(
		not_null<Ui::RpWidget*> parent,
		DocumentData *document,
		std::vector<QColor> startColors,
		BothWayCommunication<std::vector<QColor>> communication) {
	auto container = object_ptr<Ui::VerticalLayout>(parent.get());

	struct State {
		std::vector<QColor> colors;
	};
	const auto preview = container->add(
		object_ptr<Ui::CenterWrap<EmojiUserpic>>(
			container,
			object_ptr<EmojiUserpic>(
				container,
				Size(st::defaultUserpicButton.photoSize),
				false)))->entity();
	preview->setDuration(0);
	if (document) {
		preview->setDocument(document);
	}

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto state = container->lifetime().make_state<State>();
	state->colors = std::move(startColors);
	const auto buttonsContainer = container->add(object_ptr<ColorsLine>(
		container,
		&state->colors));
	buttonsContainer->resize(0, st::userpicBuilderEmojiAccentColorSize);

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto editor = container->add(object_ptr<ColorEditor>(
		container,
		ColorEditor::Mode::HSL,
		state->colors.back()));

	buttonsContainer->chosenChanges(
	) | rpl::start_with_next([=](ColorsLine::Chosen *chosen) {
		if (chosen) {
			const auto color = state->colors[chosen->index()];
			editor->showColor(color);
			editor->setCurrent(color);
		}
	}, editor->lifetime());

	const auto save = crl::guard(container.data(), [=] {
		communication.result(state->colors);
	});
	// editor->submitRequests(
	// ) | rpl::start_with_next([=] {
	// }, editor->lifetime());
	editor->colorValue(
	) | rpl::start_with_next([=](QColor c) {
		if (const auto chosen = buttonsContainer->chosen()) {
			chosen->setBrush(c);
			state->colors[chosen->index()] = c;
		}
		preview->setGradientColors(state->colors);
	}, preview->lifetime());

	base::take(
		communication.triggers
	) | rpl::start_with_next([=] {
		save();
	}, container->lifetime());

	container->resizeToWidth(editor->width());
	buttonsContainer->init();

	return container;
}

} // namespace UserpicBuilder

