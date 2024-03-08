/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_business.h"

#include "boxes/premium_preview_box.h"
#include "core/click_handler_types.h"
#include "data/business/data_business_info.h"
#include "data/business/data_business_chatbots.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h" // AmPremiumValue.
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/info_wrap_widget.h" // Info::Wrap.
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/business/settings_away_message.h"
#include "settings/business/settings_chatbots.h"
#include "settings/business/settings_greeting.h"
#include "settings/business/settings_location.h"
#include "settings/business/settings_quick_replies.h"
#include "settings/business/settings_working_hours.h"
#include "settings/settings_common_session.h"
#include "settings/settings_premium.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h" // Ui::RadiobuttonGroup.
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "api/api_premium.h"
#include "styles/style_premium.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	PremiumFeature feature = PremiumFeature::BusinessLocation;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		u"greeting_message"_q,
		u"away_message"_q,
		u"quick_replies"_q,
		u"business_hours"_q,
		u"business_location"_q,
		u"business_bots"_q,
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			u"business_location"_q,
			Entry{
				&st::settingsBusinessIconLocation,
				tr::lng_business_subtitle_location(),
				tr::lng_business_about_location(),
				PremiumFeature::BusinessLocation,
			},
		},
		{
			u"business_hours"_q,
			Entry{
				&st::settingsBusinessIconHours,
				tr::lng_business_subtitle_opening_hours(),
				tr::lng_business_about_opening_hours(),
				PremiumFeature::BusinessHours,
			},
		},
		{
			u"quick_replies"_q,
			Entry{
				&st::settingsBusinessIconReplies,
				tr::lng_business_subtitle_quick_replies(),
				tr::lng_business_about_quick_replies(),
				PremiumFeature::QuickReplies,
			},
		},
		{
			u"greeting_message"_q,
			Entry{
				&st::settingsBusinessIconGreeting,
				tr::lng_business_subtitle_greeting_messages(),
				tr::lng_business_about_greeting_messages(),
				PremiumFeature::GreetingMessage,
			},
		},
		{
			u"away_message"_q,
			Entry{
				&st::settingsBusinessIconAway,
				tr::lng_business_subtitle_away_messages(),
				tr::lng_business_about_away_messages(),
				PremiumFeature::AwayMessage,
			},
		},
		{
			u"business_bots"_q,
			Entry{
				&st::settingsBusinessIconChatbots,
				tr::lng_business_subtitle_chatbots(),
				tr::lng_business_about_chatbots(),
				PremiumFeature::BusinessBots,
			},
		},
	};
}

void AddBusinessSummary(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		Fn<void(PremiumFeature)> buttonCallback) {
	const auto &stDefault = st::settingsButton;
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	auto entryMap = EntryMap();
	auto iconContainers = std::vector<Ui::AbstractButton*>();
	iconContainers.reserve(int(entryMap.size()));

	const auto addRow = [&](Entry &entry) {
		const auto labelAscent = stLabel.style.font->ascent;
		const auto button = Ui::CreateChild<Ui::SettingsButton>(
			content.get(),
			rpl::single(QString()));

		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(entry.title) | rpl::map(Ui::Text::Bold),
				stLabel),
			titlePadding);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto description = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(entry.description),
				st::boxDividerLabel),
			descriptionPadding);
		description->setAttribute(Qt::WA_TransparentForMouseEvents);

		const auto dummy = Ui::CreateChild<Ui::AbstractButton>(content.get());
		dummy->setAttribute(Qt::WA_TransparentForMouseEvents);

		content->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			dummy->resize(s.width(), iconSize.height());
		}, dummy->lifetime());

		label->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			dummy->moveToLeft(0, r.y() + (r.height() - labelAscent));
		}, dummy->lifetime());

		rpl::combine(
			content->widthValue(),
			label->heightValue(),
			description->heightValue()
		) | rpl::start_with_next([=,
			topPadding = titlePadding,
			bottomPadding = descriptionPadding](
				int width,
				int topHeight,
				int bottomHeight) {
			button->resize(
				width,
				topPadding.top()
					+ topHeight
					+ topPadding.bottom()
					+ bottomPadding.top()
					+ bottomHeight
					+ bottomPadding.bottom());
		}, button->lifetime());
		label->topValue(
		) | rpl::start_with_next([=, padding = titlePadding.top()](int top) {
			button->moveToLeft(0, top - padding);
		}, button->lifetime());
		const auto arrow = Ui::CreateChild<Ui::IconButton>(
			button,
			st::backButton);
		arrow->setIconOverride(
			&st::settingsPremiumArrow,
			&st::settingsPremiumArrowOver);
		arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
		button->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			const auto &point = st::settingsPremiumArrowShift;
			arrow->moveToRight(
				-point.x(),
				point.y() + (s.height() - arrow->height()) / 2);
		}, arrow->lifetime());

		const auto feature = entry.feature;
		button->setClickedCallback([=] { buttonCallback(feature); });

		iconContainers.push_back(dummy);
	};

	auto icons = std::vector<const style::icon *>();
	icons.reserve(int(entryMap.size()));
	{
		const auto &account = controller->session().account();
		const auto mtpOrder = account.appConfig().get<Order>(
			"business_promo_order",
			FallbackOrder());
		const auto processEntry = [&](Entry &entry) {
			icons.push_back(entry.icon);
			addRow(entry);
		};

		for (const auto &key : mtpOrder) {
			auto it = entryMap.find(key);
			if (it == end(entryMap)) {
				continue;
			}
			processEntry(it->second);
		}
	}

	content->resizeToWidth(content->height());

	// Icons.
	Assert(iconContainers.size() > 2);
	const auto from = iconContainers.front()->y();
	const auto to = iconContainers.back()->y() + iconSize.height();
	auto gradient = QLinearGradient(0, 0, 0, to - from);
	gradient.setStops(Ui::Premium::FullHeightGradientStops());
	for (auto i = 0; i < int(icons.size()); i++) {
		const auto &iconContainer = iconContainers[i];

		const auto pointTop = iconContainer->y() - from;
		const auto pointBottom = pointTop + iconContainer->height();
		const auto ratioTop = pointTop / float64(to - from);
		const auto ratioBottom = pointBottom / float64(to - from);

		auto resultGradient = QLinearGradient(
			QPointF(),
			QPointF(0, pointBottom - pointTop));

		resultGradient.setColorAt(
			.0,
			anim::gradient_color_at(gradient, ratioTop));
		resultGradient.setColorAt(
			.1,
			anim::gradient_color_at(gradient, ratioBottom));

		const auto brush = QBrush(resultGradient);
		AddButtonIcon(
			iconContainer,
			stDefault,
			{ .icon = icons[i], .backgroundBrush = brush });
	}

	Ui::AddSkip(content, descriptionPadding.bottom());
}

class Business : public Section<Business> {
public:
	Business(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;
	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void showFinished() override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;

	QPointer<Ui::GradientButton> _subscribe;
	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	Fn<void(bool)> _setPaused;
	std::shared_ptr<Ui::RadiobuttonGroup> _radioGroup;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;
	rpl::variable<QString> _buttonText;

	PremiumFeature _waitingToShow = PremiumFeature::Business;

};

Business::Business(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _radioGroup(std::make_shared<Ui::RadiobuttonGroup>()) {
	setupContent();
	_controller->session().api().premium().reload();
}

rpl::producer<QString> Business::title() {
	return tr::lng_premium_summary_title();
}

bool Business::hasFlexibleTopBar() const {
	return true;
}

rpl::producer<> Business::sectionShowBack() {
	return _showBack.events();
}

void Business::setStepDataReference(std::any &data) {
	using namespace Info::Settings;
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Business::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto owner = &_controller->session().data();
	owner->chatbots().preload();
	owner->businessInfo().preload();
	owner->shortcutMessages().preloadShortcuts();

	Ui::AddSkip(content, st::settingsFromFileTop);

	const auto showFeature = [=](PremiumFeature feature) {
		showOther([&] {
			switch (feature) {
			case PremiumFeature::AwayMessage: return AwayMessageId();
			case PremiumFeature::BusinessHours: return WorkingHoursId();
			case PremiumFeature::BusinessLocation: return LocationId();
			case PremiumFeature::GreetingMessage: return GreetingId();
			case PremiumFeature::QuickReplies: return QuickRepliesId();
			case PremiumFeature::BusinessBots: return ChatbotsId();
			}
			Unexpected("Feature in showFeature.");
		}());
	};
	const auto isReady = [=](PremiumFeature feature) {
		switch (feature) {
		case PremiumFeature::AwayMessage:
			return owner->businessInfo().awaySettingsLoaded()
				&& owner->shortcutMessages().shortcutsLoaded();
		case PremiumFeature::BusinessHours:
			return owner->session().user()->isFullLoaded()
				&& owner->businessInfo().timezonesLoaded();
		case PremiumFeature::BusinessLocation:
			return owner->session().user()->isFullLoaded();
		case PremiumFeature::GreetingMessage:
			return owner->businessInfo().greetingSettingsLoaded()
				&& owner->shortcutMessages().shortcutsLoaded();
		case PremiumFeature::QuickReplies:
			return owner->shortcutMessages().shortcutsLoaded();
		case PremiumFeature::BusinessBots:
			return owner->chatbots().loaded();
		}
		Unexpected("Feature in isReady.");
	};
	const auto check = [=] {
		if (_waitingToShow != PremiumFeature::Business
			&& isReady(_waitingToShow)) {
			showFeature(
				std::exchange(_waitingToShow, PremiumFeature::Business));
		}
	};

	rpl::merge(
		owner->businessInfo().awaySettingsChanged(),
		owner->businessInfo().greetingSettingsChanged(),
		owner->businessInfo().timezonesValue() | rpl::to_empty,
		owner->shortcutMessages().shortcutsChanged(),
		owner->chatbots().changes() | rpl::to_empty,
		owner->session().changes().peerUpdates(
			owner->session().user(),
			Data::PeerUpdate::Flag::FullInfo) | rpl::to_empty
	) | rpl::start_with_next(check, content->lifetime());

	AddBusinessSummary(content, _controller, [=](PremiumFeature feature) {
		if (!_controller->session().premium()) {
			_setPaused(true);
			const auto hidden = crl::guard(this, [=] { _setPaused(false); });

			ShowPremiumPreviewToBuy(_controller, feature, hidden);
			return;
		} else if (!isReady(feature)) {
			_waitingToShow = feature;
		} else {
			showFeature(feature);
		}
	});

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Business::createPinnedToTop(
		not_null<QWidget*> parent) {
	auto title = tr::lng_business_title();
	auto about = [&]() -> rpl::producer<TextWithEntities> {
		return rpl::conditional(
			Data::AmPremiumValue(&_controller->session()),
			tr::lng_business_unlocked(),
			tr::lng_business_about()
		) | Ui::Text::ToWithEntities();
	}();

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
			st::defaultPremiumCover,
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = clickContextOther,
				.logo = u"dollar"_q,
				.title = std::move(title),
				.about = std::move(about),
			});
	}();
	_setPaused = [=](bool paused) {
		content->setPaused(paused);
		if (_subscribe) {
			_subscribe->setGlarePaused(paused);
		}
	};

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	const auto calculateMaximumHeight = [=] {
		return st::settingsPremiumTopHeight;
	};

	content->setMaximumHeight(calculateMaximumHeight());
	content->setMinimumHeight(st::settingsPremiumTopHeight);// st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());
	//content->additionalHeight(
	//) | rpl::start_with_next([=](int additionalHeight) {
	//	const auto wasMax = (content->height() == content->maximumHeight());
	//	content->setMaximumHeight(calculateMaximumHeight()
	//		+ additionalHeight);
	//	if (wasMax) {
	//		content->resize(content->width(), content->maximumHeight());
	//	}
	//}, content->lifetime());

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				(isLayer
					? st::settingsPremiumLayerTopBarBack
					: st::settingsPremiumTopBarBack)),
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
				st::settingsPremiumTopBarClose);
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

void Business::showFinished() {
	_showFinished.fire({});
}

QPointer<Ui::RpWidget> Business::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::RpWidget>(parent.get());

	const auto session = &_controller->session();

	auto buttonText = _buttonText.value();

	_subscribe = CreateSubscribeButton({
		_controller,
		content,
		[] { return u"business"_q; },
		std::move(buttonText),
		std::nullopt,
		[=, options = session->api().premium().subscriptionOptions()] {
			const auto value = _radioGroup->current();
			return (value < options.size() && value >= 0)
				? options[value].botUrl
				: QString();
		},
	});
	{
		const auto callback = [=](int value) {
			const auto options =
				_controller->session().api().premium().subscriptionOptions();
			if (options.empty()) {
				return;
			}
			Assert(value < options.size() && value >= 0);
			auto text = tr::lng_premium_subscribe_button(
				tr::now,
				lt_cost,
				options[value].costPerMonth);
			_buttonText = std::move(text);
		};
		_radioGroup->setChangedCallback(callback);
		callback(0);
	}

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		_subscribe->startGlareAnimation();
	}, _subscribe->lifetime());

	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::settingsPremiumButtonPadding;
		_subscribe->resizeToWidth(width - padding.left() - padding.right());
	}, _subscribe->lifetime());

	rpl::combine(
		_subscribe->heightValue(),
		Data::AmPremiumValue(session),
		session->premiumPossibleValue()
	) | rpl::start_with_next([=](
			int buttonHeight,
			bool premium,
			bool premiumPossible) {
		const auto padding = st::settingsPremiumButtonPadding;
		const auto finalHeight = !premiumPossible
			? 0
			: !premium
			? (padding.top() + buttonHeight + padding.bottom())
			: 0;
		content->resize(content->width(), finalHeight);
		_subscribe->moveToLeft(padding.left(), padding.top());
		_subscribe->setVisible(!premium && premiumPossible);
	}, _subscribe->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

} // namespace

template <>
struct SectionFactory<Business> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Business>(parent, controller);
	}
	bool hasCustomTopBar() const final override {
		return true;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

Type BusinessId() {
	return Business::Id();
}

void ShowBusiness(not_null<Window::SessionController*> controller) {
	if (!controller->session().premiumPossible()) {
		controller->show(Box(PremiumUnavailableBox));
		return;
	}
	controller->showSettings(Settings::BusinessId());
}

std::vector<PremiumFeature> BusinessFeaturesOrder(
		not_null<::Main::Session*> session) {
	const auto mtpOrder = session->account().appConfig().get<Order>(
		"business_promo_order",
		FallbackOrder());
	return ranges::views::all(
		mtpOrder
	) | ranges::views::transform([](const QString &s) {
		if (s == u"greeting_message"_q) {
			return PremiumFeature::GreetingMessage;
		} else if (s == u"away_message"_q) {
			return PremiumFeature::AwayMessage;
		} else if (s == u"quick_replies"_q) {
			return PremiumFeature::QuickReplies;
		} else if (s == u"business_hours"_q) {
			return PremiumFeature::BusinessHours;
		} else if (s == u"business_location"_q) {
			return PremiumFeature::BusinessLocation;
		} else if (s == u"business_bots"_q) {
			return PremiumFeature::BusinessBots;
		}
		return PremiumFeature::kCount;
	}) | ranges::views::filter([](PremiumFeature feature) {
		return (feature != PremiumFeature::kCount);
	}) | ranges::to_vector;
}

} // namespace Settings
