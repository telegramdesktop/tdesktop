/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/toggle_topics_box.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Ui {
namespace {

enum class LayoutType {
	Tabs,
	List
};

class LayoutButton final : public Ui::RippleButton {
public:
	LayoutButton(
		QWidget *parent,
		LayoutType type,
		std::shared_ptr<Ui::RadioenumGroup<LayoutType>> group);

private:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;

	Ui::FlatLabel _text;
	Ui::Animations::Simple _activeAnimation;
	bool _active = false;

};

LayoutButton::LayoutButton(
	QWidget *parent,
	LayoutType type,
	std::shared_ptr<Ui::RadioenumGroup<LayoutType>> group)
: RippleButton(parent, st::defaultRippleAnimationBgOver)
, _text(this, st::topicsLayoutButtonLabel)
, _active(group->current() == type) {
	_text.setText(type == LayoutType::Tabs
		? tr::lng_edit_topics_tabs(tr::now)
		: tr::lng_edit_topics_list(tr::now));
	const auto iconColorOverride = [=] {
		return anim::color(
			st::windowSubTextFg,
			st::windowActiveTextFg,
			_activeAnimation.value(_active ? 1. : 0.));
	};
	const auto iconSize = st::topicsLayoutButtonIconSize;
	auto [iconWidget, iconAnimate] = Settings::CreateLottieIcon(
		this,
		{
			.name = (type == LayoutType::Tabs
				? u"topics_tabs"_q
				: u"topics_list"_q),
			.color = &st::windowSubTextFg,
			.sizeOverride = { iconSize, iconSize },
			.colorizeUsingAlpha = true,
		},
		st::topicsLayoutButtonIconPadding,
		iconColorOverride);
	const auto icon = iconWidget.release();
	setClickedCallback([=] {
		group->setValue(type);
		iconAnimate(anim::repeat::once);
	});
	group->value() | rpl::start_with_next([=](LayoutType value) {
		const auto active = (value == type);
		_text.setTextColorOverride(active
			? st::windowFgActive->c
			: std::optional<QColor>());

		if (_active == active) {
			return;
		}
		_active = active;
		_text.update();
		_activeAnimation.start([=] {
			icon->update();
		}, _active ? 0. : 1., _active ? 0. : 1., st::fadeWrapDuration);
	}, lifetime());

	_text.paintRequest() | rpl::start_with_next([=](QRect clip) {
		if (_active) {
			auto p = QPainter(&_text);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = _text.height() / 2.;
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(_text.rect(), radius, radius);
		}
	}, _text.lifetime());

	const auto padding = st::topicsLayoutButtonPadding;
	const auto skip = st::topicsLayoutButtonSkip;
	const auto text = _text.height();

	resize(
		padding.left() + icon->width() + padding.right(),
		padding.top() + icon->height() + skip + text + padding.bottom());
	icon->move(padding.left(), padding.top());
	_text.move(
		(width() - _text.width()) / 2,
		padding.top() + icon->height() + skip);
}

void LayoutButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto rippleBg = anim::color(
		st::windowBgOver,
		st::lightButtonBgOver,
		_activeAnimation.value(_active ? 1. : 0.));
	paintRipple(p, QPoint(), &rippleBg);
}

QImage LayoutButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(), st::boxRadius);
}

} // namespace

void ToggleTopicsBox(
		not_null<Ui::GenericBox*> box,
		bool enabled,
		bool tabs,
		Fn<void(bool enabled, bool tabs)> callback) {
	box->setTitle(tr::lng_forum_topics_switch());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();

	Settings::AddDividerTextWithLottie(container, {
		.lottie = u"topics"_q,
		.lottieSize = st::settingsFilterIconSize,
		.lottieMargins = st::settingsFilterIconPadding,
		.showFinished = box->showFinishes(),
		.about = tr::lng_edit_topics_about(
			Ui::Text::RichLangValue
		),
		.aboutMargins = st::settingsFilterDividerLabelPadding,
	});

	Ui::AddSkip(container);

	const auto toggle = container->add(
		object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_edit_topics_enable(),
			st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(enabled));

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto group = std::make_shared<Ui::RadioenumGroup<LayoutType>>(tabs
		? LayoutType::Tabs
		: LayoutType::List);

	const auto layoutWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto layout = layoutWrap->entity();

	Ui::AddSubsectionTitle(layout, tr::lng_edit_topics_layout());
	const auto buttons = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		QMargins(0, 0, 0, st::defaultVerticalListSkip * 2));

	const auto tabsButton = Ui::CreateChild<LayoutButton>(
		buttons,
		LayoutType::Tabs,
		group);
	const auto listButton = Ui::CreateChild<LayoutButton>(
		buttons,
		LayoutType::List,
		group);

	buttons->resize(container->width(), tabsButton->height());
	buttons->widthValue() | rpl::start_with_next([=](int outer) {
		const auto skip = st::boxRowPadding.left() - st::boxRadius;
		tabsButton->moveToLeft(skip, 0, outer);
		listButton->moveToRight(skip, 0, outer);
	}, buttons->lifetime());

	Ui::AddDividerText(
		layout,
		tr::lng_edit_topics_layout_about(Ui::Text::RichLangValue));

	layoutWrap->toggle(enabled, anim::type::instant);
	toggle->toggledChanges(
	) | rpl::start_with_next([=](bool checked) {
		layoutWrap->toggle(checked, anim::type::normal);
	}, layoutWrap->lifetime());

	box->addButton(tr::lng_settings_save(), [=] {
		const auto enabledValue = toggle->toggled();
		const auto tabsValue = (group->current() == LayoutType::Tabs);
		callback(enabledValue, tabsValue);
		box->closeBox();
	});

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace Ui
