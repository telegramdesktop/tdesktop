/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits.h"

#include "api/api_credits.h"
#include "core/click_handler_types.h"
#include "data/data_user.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;

class Credits : public Section<Credits> {
public:
	Credits(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

	void showFinished() override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();
	void setupOptions(not_null<Ui::VerticalLayout*> container);

	const not_null<Window::SessionController*> _controller;

	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	Fn<void(bool)> _setPaused;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;
	rpl::variable<QString> _buttonText;

};

Credits::Credits(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> Credits::title() {
	return tr::lng_premium_summary_title();
}

bool Credits::hasFlexibleTopBar() const {
	return true;
}

rpl::producer<> Credits::sectionShowBack() {
	return _showBack.events();
}

void Credits::setStepDataReference(std::any &data) {
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Credits::setupOptions(not_null<Ui::VerticalLayout*> container) {
}

void Credits::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Credits::createPinnedToTop(
		not_null<QWidget*> parent) {

	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		const auto weak = base::make_weak(_controller);
		const auto clickContextOther = [=] {
			return QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
				.botStartAutoSubmit = true,
			});
		};
		return Ui::CreateChild<Ui::Premium::TopBar>(
			parent.get(),
			st::creditsPremiumCover,
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = clickContextOther,
				.title = tr::lng_credits_summary_title(),
				.about = tr::lng_credits_summary_about(
					TextWithEntities::Simple),
				.light = true,
				.gradientStops = Ui::Premium::CreditsIconGradientStops(),
			});
	}();
	_setPaused = [=](bool paused) {
		content->setPaused(paused);
	};

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	content->setMaximumHeight(st::settingsPremiumTopHeight);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());
	content->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		const auto wasMax = (content->height() == content->maximumHeight());
		content->setMaximumHeight(st::settingsPremiumTopHeight
			+ additionalHeight);
		if (wasMax) {
			content->resize(content->width(), content->maximumHeight());
		}
	}, content->lifetime());

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				(isLayer ? st::infoTopBarBack : st::infoLayerTopBarBack)),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(isLayer
			? _backToggles.value() | rpl::type_erased()
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			_showBack.fire({});
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			content->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				content,
				st::infoTopBarClose);
			_close->addClickHandler([=] {
				_controller->parentController()->hideLayer();
				_controller->parentController()->hideSpecialLayer();
			});
			content->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, content->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

void Credits::showFinished() {
	_showFinished.fire({});
}

} // namespace

template <>
struct SectionFactory<Credits> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Credits>(parent, controller);
	}
	bool hasCustomTopBar() const final override {
		return true;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

Type CreditsId() {
	return Credits::Id();
}

} // namespace Settings
