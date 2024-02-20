/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "lottie/lottie_icon.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

#include <QAction>

namespace Settings {

Icon::Icon(IconDescriptor descriptor) : _icon(descriptor.icon) {
	const auto background = [&]() -> const style::color* {
		if (descriptor.type == IconType::Simple) {
			return nullptr;
		}
		return descriptor.background;
	}();
	if (background) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_background.emplace(radius, *background);
	} else if (const auto brush = descriptor.backgroundBrush) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_backgroundBrush.emplace(radius, std::move(*brush));
	}
}

void Icon::paint(QPainter &p, QPoint position) const {
	paint(p, position.x(), position.y());
}

void Icon::paint(QPainter &p, int x, int y) const {
	if (_background) {
		_background->paint(p, { { x, y }, _icon->size() });
	} else if (_backgroundBrush) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_backgroundBrush->second);
		p.drawRoundedRect(
			QRect(QPoint(x, y), _icon->size()),
			_backgroundBrush->first,
			_backgroundBrush->first);
	}
	_icon->paint(p, { x, y }, 2 * x + _icon->width());
}

int Icon::width() const {
	return _icon->width();
}

int Icon::height() const {
	return _icon->height();
}

QSize Icon::size() const {
	return _icon->size();
}

void AddButtonIcon(
		not_null<Ui::AbstractButton*> button,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	Expects(descriptor.icon != nullptr);

	struct IconWidget {
		IconWidget(QWidget *parent, IconDescriptor &&descriptor)
		: widget(parent)
		, icon(std::move(descriptor)) {
		}
		Ui::RpWidget widget;
		Icon icon;
	};
	const auto icon = button->lifetime().make_state<IconWidget>(
		button,
		std::move(descriptor));
	icon->widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->widget.resize(icon->icon.size());
	button->sizeValue(
	) | rpl::start_with_next([=, left = st.iconLeft](QSize size) {
		icon->widget.moveToLeft(
			left,
			(size.height() - icon->widget.height()) / 2,
			size.width());
	}, icon->widget.lifetime());
	icon->widget.paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(&icon->widget);
		icon->icon.paint(p, 0, 0);
	}, icon->widget.lifetime());
}

object_ptr<Button> CreateButtonWithIcon(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	auto result = object_ptr<Button>(parent, std::move(text), st);
	const auto button = result.data();
	if (descriptor) {
		AddButtonIcon(button, st, std::move(descriptor));
	}
	return result;
}

not_null<Button*> AddButtonWithIcon(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	return container->add(
		CreateButtonWithIcon(container, std::move(text), st, std::move(descriptor)));
}

void CreateRightLabel(
		not_null<Button*> button,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		rpl::producer<QString> buttonText) {
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st.rightLabel);
	name->show();
	rpl::combine(
		button->widthValue(),
		std::move(buttonText),
		std::move(label)
	) | rpl::start_with_next([=, &st](
			int width,
			const QString &button,
			const QString &text) {
		const auto available = width
			- st.padding.left()
			- st.padding.right()
			- st.style.font->width(button)
			- st::settingsButtonRightSkip;
		name->setText(text);
		name->resizeToNaturalWidth(available);
		name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
	}, name->lifetime());
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
}

not_null<Button*> AddButtonWithLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	const auto button = AddButtonWithIcon(
		container,
		rpl::duplicate(text),
		st,
		std::move(descriptor));
	CreateRightLabel(button, std::move(label), st, std::move(text));
	return button;
}

void AddDividerTextWithLottie(
		not_null<Ui::VerticalLayout*> container,
		DividerWithLottieDescriptor &&descriptor) {
	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(
		container.get(),
		0,
		st::boxDividerBg,
		descriptor.parts);
	const auto verticalLayout = container->add(
		object_ptr<Ui::VerticalLayout>(container.get()));
	const auto size = descriptor.lottieSize.value_or(
		st::settingsFilterIconSize);
	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = descriptor.lottie,
			.sizeOverride = { size, size },
		},
		descriptor.lottieMargins.value_or(st::settingsFilterIconPadding));
	if (descriptor.showFinished) {
		const auto repeat = descriptor.lottieRepeat.value_or(
			anim::repeat::once);
		std::move(
			descriptor.showFinished
		) | rpl::start_with_next([animate = std::move(icon.animate), repeat] {
			animate(repeat);
		}, verticalLayout->lifetime());
	}
	verticalLayout->add(std::move(icon.widget));

	if (descriptor.about) {
		verticalLayout->add(
			object_ptr<Ui::CenterWrap<>>(
				verticalLayout,
				object_ptr<Ui::FlatLabel>(
					verticalLayout,
					std::move(descriptor.about),
					st::settingsFilterDividerLabel)),
			descriptor.aboutMargins.value_or(
				st::settingsFilterDividerLabelPadding));
	}

	verticalLayout->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
}

LottieIcon CreateLottieIcon(
		not_null<QWidget*> parent,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding) {
	Expects(!descriptor.frame); // I'm not sure it considers limitFps.

	descriptor.limitFps = true;

	auto object = object_ptr<Ui::RpWidget>(parent);
	const auto raw = object.data();

	const auto width = descriptor.sizeOverride.width();
	raw->resize(QRect(
		QPoint(),
		descriptor.sizeOverride).marginsAdded(padding).size());

	auto owned = Lottie::MakeIcon(std::move(descriptor));
	const auto icon = owned.get();

	raw->lifetime().add([kept = std::move(owned)]{});
	const auto looped = raw->lifetime().make_state<bool>(true);

	const auto start = [=] {
		icon->animate([=] { raw->update(); }, 0, icon->framesCount() - 1);
	};
	const auto animate = [=](anim::repeat repeat) {
		*looped = (repeat == anim::repeat::loop);
		start();
	};
	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top());
		if (!icon->animating() && icon->frameIndex() > 0 && *looped) {
			start();
		}

	}, raw->lifetime());

	return { .widget = std::move(object), .animate = std::move(animate) };
}

SliderWithLabel MakeSliderWithLabel(
		QWidget *parent,
		const style::MediaSlider &sliderSt,
		const style::FlatLabel &labelSt,
		int skip,
		int minLabelWidth) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto height = std::max(
		sliderSt.seekSize.height(),
		labelSt.style.font->height);
	raw->resize(sliderSt.seekSize.width(), height);
	const auto slider = Ui::CreateChild<Ui::MediaSlider>(raw, sliderSt);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(raw, labelSt);
	slider->resize(slider->width(), sliderSt.seekSize.height());
	rpl::combine(
		raw->sizeValue(),
		label->sizeValue()
	) | rpl::start_with_next([=](QSize outer, QSize size) {
		const auto right = std::max(size.width(), minLabelWidth) + skip;
		label->moveToRight(0, (outer.height() - size.height()) / 2);
		const auto width = std::max(
			sliderSt.seekSize.width(),
			outer.width() - right);
		slider->resizeToWidth(width);
		slider->moveToLeft(0, (outer.height() - slider->height()) / 2);
	}, label->lifetime());
	return {
		.widget = std::move(result),
		.slider = slider,
		.label = label,
	};
}

} // namespace Settings
