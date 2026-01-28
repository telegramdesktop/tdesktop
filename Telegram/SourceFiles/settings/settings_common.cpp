/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "base/timer.h"
#include "lottie/lottie_icon.h"
#include "ui/effects/animations.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QAction>

namespace Settings {
namespace {

[[nodiscard]] HighlightArgs MaybeFillFromRipple(
		not_null<QWidget*> target,
		HighlightArgs args) {
	if (!args.rippleShape) {
		return args;
	}
	const auto widget = target.get();
	if (const auto icon = dynamic_cast<Ui::IconButton*>(widget)) {
		const auto &st = icon->st();
		args.shape = HighlightShape::Ellipse;
		args.margin = QMargins(
			st.rippleAreaPosition.x(),
			st.rippleAreaPosition.y(),
			st.width - st.rippleAreaPosition.x() - st.rippleAreaSize,
			st.height - st.rippleAreaPosition.y() - st.rippleAreaSize);
	} else if (const auto round = dynamic_cast<Ui::RoundButton*>(widget)) {
		const auto &st = round->st();
		args.shape = HighlightShape::Rect;
		args.radius = st.radius ? st.radius : st::buttonRadius;
	}
	return args;
}

class HighlightOverlay final : public Ui::RpWidget {
public:
	HighlightOverlay(not_null<QWidget*> target, HighlightArgs &&args);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	enum class Phase {
		Wait,
		FadeIn,
		Shown,
		FadeOut,
	};

	void updateGeometryFromTarget();
	void updateZOrder();
	void startFadeIn();
	void startFadeOut();
	void nextPhase();
	void nextPhaseIn(crl::time delay);
	void finish();

	const QPointer<QWidget> _target;
	const HighlightArgs _args;
	const style::color &_color;

	Ui::Animations::Simple _animation;
	base::Timer _phaseTimer;
	Phase _phase = Phase::Wait;
	bool _finishing = false;

};

HighlightOverlay::HighlightOverlay(
	not_null<QWidget*> target,
	HighlightArgs &&args)
: RpWidget(target->parentWidget())
, _target(target.get())
, _args(MaybeFillFromRipple(target, std::move(args)))
, _color(_args.color ? *_args.color : st::windowBgActive)
, _phaseTimer([=] { nextPhase(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	target->installEventFilter(this);

	QObject::connect(
		target.get(),
		&QObject::destroyed,
		this,
		[this] { finish(); });

	updateGeometryFromTarget();
	updateZOrder();

	paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(this);

		const auto progress = _animation.value(
			(_phase == Phase::Wait || _phase == Phase::FadeOut) ? 0. : 1.);
		const auto alpha = _args.opacity * progress;

		auto color = _color->c;
		color.setAlphaF(color.alphaF() * alpha);

		p.setPen(Qt::NoPen);
		p.setBrush(color);

		const auto r = rect();
		switch (_args.shape) {
		case HighlightShape::Rect:
			if (_args.radius > 0) {
				PainterHighQualityEnabler hq(p);
				p.drawRoundedRect(r, _args.radius, _args.radius);
			} else {
				p.drawRect(r);
			}
			break;
		case HighlightShape::Ellipse:
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(r);
			break;
		}
	}, lifetime());

	hide();
	nextPhaseIn(_args.showDelay);
}

bool HighlightOverlay::eventFilter(QObject *o, QEvent *e) {
	if (o != _target.data()) {
		return false;
	}
	switch (e->type()) {
	case QEvent::Move:
	case QEvent::Resize:
		updateGeometryFromTarget();
		break;
	case QEvent::ZOrderChange:
		updateZOrder();
		break;
	case QEvent::Show:
	case QEvent::Hide:
		setVisible(!_target->isHidden());
		break;
	default:
		break;
	}
	return false;
}

void HighlightOverlay::updateGeometryFromTarget() {
	if (_target) {
		setGeometry(_target->geometry().marginsRemoved(_args.margin));
	}
}

void HighlightOverlay::updateZOrder() {
	if (!_target) {
		return;
	}
	if (_args.below) {
		stackUnder(_target);
	} else {
		const auto parent = _target->parentWidget();
		const auto siblings = parent ? parent->children() : QObjectList();
		const auto it = ranges::find(siblings, _target.data());
		const auto next = (it != siblings.end()) ? (it + 1) : it;
		if (next != siblings.end()) {
			if (const auto widget = qobject_cast<QWidget*>(*next)) {
				stackUnder(widget);
				return;
			}
		}
		raise();
	}
}

void HighlightOverlay::startFadeIn() {
	show();

	_phase = Phase::FadeIn;
	_animation.start([=] {
		update();
		if (!_animation.animating()) {
			_phase = Phase::Shown;
			nextPhaseIn(_args.shownDuration);
		}
	}, 0., 1., _args.showDuration, anim::easeOutCubic);
}

void HighlightOverlay::startFadeOut() {
	_phase = Phase::FadeOut;
	_animation.start([=] {
		update();
		if (!_animation.animating()) {
			finish();
		}
	}, 1., 0., _args.hideDuration, anim::easeInCubic);
}

void HighlightOverlay::nextPhase() {
	switch (_phase) {
	case Phase::Wait:
		startFadeIn();
		break;
	case Phase::Shown:
		startFadeOut();
		break;
	default:
		Unexpected("Next phase called during Fade phase.");
	}
}

void HighlightOverlay::nextPhaseIn(crl::time delay) {
	if (!delay) {
		_phaseTimer.cancel();
		nextPhase();
	} else {
		_phaseTimer.callOnce(delay);
	}
}

void HighlightOverlay::finish() {
	if (_finishing) {
		return;
	}
	_finishing = true;
	_phaseTimer.cancel();
	_animation.stop();
	deleteLater();
}

} // namespace

void HighlightWidget(QWidget *target, HighlightArgs &&args) {
	if (!target) {
		return;
	}
	if (args.scroll) {
		ScrollToWidget(target);
	}
	new HighlightOverlay(target, std::move(args));
}

void ScrollToWidget(not_null<QWidget*> target) {
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto globalPosition = target->mapToGlobal(QPoint(0, 0));
			const auto localPosition = inner->mapFromGlobal(globalPosition);
			const auto localTop = localPosition.y();
			const auto targetHeight = target->height();
			const auto scrollHeight = scroll->height();
			const auto centered = localTop - (scrollHeight - targetHeight) / 2;
			const auto top = std::clamp(centered, 0, scroll->scrollTopMax());
			scroll->scrollToY(top);
		}
	};
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

HighlightArgs SubsectionTitleHighlight() {
	const auto radius = st::roundRadiusSmall;
	return { .margin = { -radius, 0, -radius, 0 }, .radius = radius };
}

AbstractSection::AbstractSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void AbstractSection::build(
		not_null<Ui::VerticalLayout*> container,
		SectionBuildMethod method) {
	method(
		container,
		_controller,
		showOtherMethod(),
		_showFinished.events());
}

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
	icon->widget.show();
	button->sizeValue(
	) | rpl::on_next([=, left = st.iconLeft](QSize size) {
		icon->widget.moveToLeft(
			left,
			(size.height() - icon->widget.height()) / 2,
			size.width());
	}, icon->widget.lifetime());
	icon->widget.paintRequest(
	) | rpl::on_next([=] {
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
		CreateButtonWithIcon(
			container,
			std::move(text),
			st,
			std::move(descriptor)));
}

void CreateRightLabel(
		not_null<Button*> button,
		v::text::data &&label,
		const style::SettingsButton &st,
		rpl::producer<QString> buttonText,
		Ui::Text::MarkedContext context) {
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st.rightLabel);
	name->show();
	if (v::text::is_plain(label)) {
		rpl::combine(
			button->widthValue(),
			std::move(buttonText),
			v::text::take_plain(std::move(label))
		) | rpl::on_next([=, &st](
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
	} else if (v::text::is_marked(label)) {
		rpl::combine(
			button->widthValue(),
			std::move(buttonText),
			v::text::take_marked(std::move(label))
		) | rpl::on_next([=, &st](
				int width,
				const QString &button,
				const TextWithEntities &text) {
			const auto available = width
				- st.padding.left()
				- st.padding.right()
				- st.style.font->width(button)
				- st::settingsButtonRightSkip;
			name->setMarkedText(text, context);
			name->resizeToNaturalWidth(available);
			name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
		}, name->lifetime());
	}
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
		st::defaultDividerBar,
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
		) | rpl::on_next([animate = std::move(icon.animate), repeat] {
			animate(repeat);
		}, verticalLayout->lifetime());
	}
	verticalLayout->add(std::move(icon.widget));

	if (descriptor.about) {
		verticalLayout->add(
			object_ptr<Ui::FlatLabel>(
				verticalLayout,
				std::move(descriptor.about),
				st::settingsFilterDividerLabel),
			descriptor.aboutMargins.value_or(
				st::settingsFilterDividerLabelPadding),
			style::al_top)->setTryMakeSimilarLines(true);
	}

	verticalLayout->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
}

LottieIcon CreateLottieIcon(
		not_null<QWidget*> parent,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding,
		Fn<QColor()> colorOverride) {
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
		icon->animate([=] {
			raw->update();
		}, 0, icon->framesCount() - 1);
	};
	const auto animate = [=](anim::repeat repeat) {
		*looped = (repeat == anim::repeat::loop);
		start();
	};
	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top(), colorOverride
			? colorOverride()
			: std::optional<QColor>());
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
		int minLabelWidth,
		bool ignoreWheel) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto height = std::max(
		sliderSt.seekSize.height(),
		labelSt.style.font->height);
	raw->resize(sliderSt.seekSize.width(), height);
	const auto slider = ignoreWheel
		? Ui::CreateChild<Ui::MediaSliderWheelless>(raw, sliderSt)
		: Ui::CreateChild<Ui::MediaSlider>(raw, sliderSt);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(raw, labelSt);
	slider->resize(slider->width(), sliderSt.seekSize.height());
	rpl::combine(
		raw->sizeValue(),
		label->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize size) {
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

void AddLottieIconWithCircle(
		not_null<Ui::VerticalLayout*> layout,
		object_ptr<Ui::RpWidget> icon,
		QMargins iconPadding,
		QSize circleSize) {
	const auto iconRow = layout->add(
		std::move(icon),
		iconPadding,
		style::al_top);

	const auto circle = Ui::CreateChild<Ui::RpWidget>(
		iconRow->parentWidget());
	circle->lower();
	circle->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (circle->width() - circleSize.width()) / 2;
		const auto top = (circle->height() - circleSize.height()) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawEllipse(QRect(QPoint(left, top), circleSize));
	});

	iconRow->geometryValue() | rpl::on_next([=](const QRect &g) {
		circle->setGeometry(g);
	}, circle->lifetime());
}

void AddPremiumStar(
		not_null<Button*> button,
		bool credits,
		Fn<bool()> isPaused) {
	const auto stops = credits
		? Ui::Premium::CreditsIconGradientStops()
		: Ui::Premium::ButtonGradientStops();

	const auto ministarsContainer = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &buttonSt = button->st();
	const auto fullHeight = buttonSt.height
		+ rect::m::sum::v(buttonSt.padding);
	using MiniStars = Ui::Premium::ColoredMiniStars;
	const auto ministars = button->lifetime().make_state<MiniStars>(
		ministarsContainer,
		false);
	ministars->setColorOverride(stops);

	const auto isPausedValue
		= button->lifetime().make_state<rpl::variable<bool>>(isPaused());
	isPausedValue->value() | rpl::on_next([=](bool value) {
		ministars->setPaused(value);
	}, ministarsContainer->lifetime());

	ministarsContainer->paintRequest(
	) | rpl::on_next([=] {
		(*isPausedValue) = isPaused();
		auto p = QPainter(ministarsContainer);
		{
			constexpr auto kScale = 0.35;
			const auto r = ministarsContainer->rect();
			p.translate(r.center());
			p.scale(kScale, kScale);
			p.translate(-r.center());
		}
		ministars->paint(p);
	}, ministarsContainer->lifetime());

	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());

	auto star = [&] {
		const auto factor = style::DevicePixelRatio();
		const auto size = Size(st::settingsButtonNoIcon.style.font->ascent);
		auto image = QImage(
			size * factor,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(factor);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			auto star = QSvgRenderer(Ui::Premium::ColorizedSvg(stops));
			star.render(&p, Rect(size));
		}
		return image;
	}();
	badge->resize(star.size() / style::DevicePixelRatio());
	badge->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(badge);
		p.drawImage(0, 0, star);
	}, badge->lifetime());

	button->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		badge->moveToLeft(
			button->st().iconLeft
				+ (st::menuIconShop.width() - badge->width()) / 2,
			(s.height() - badge->height()) / 2);
		ministarsContainer->moveToLeft(
			badge->x() - (fullHeight - badge->height()) / 2,
			0);
	}, badge->lifetime());

	ministarsContainer->resize(fullHeight, fullHeight);
	ministars->setCenter(ministarsContainer->rect());
}

} // namespace Settings
