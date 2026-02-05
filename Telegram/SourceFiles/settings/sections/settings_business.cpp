/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_business.h"

#include "api/api_chat_links.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "boxes/premium_preview_box.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/business/data_business_chatbots.h"
#include "data/business/data_business_info.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/info_wrap_widget.h"
#include "info/settings/info_settings_widget.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/business/settings_away_message.h"
#include "settings/business/settings_chat_intro.h"
#include "settings/business/settings_chat_links.h"
#include "settings/business/settings_chatbots.h"
#include "settings/business/settings_greeting.h"
#include "settings/business/settings_location.h"
#include "settings/business/settings_quick_replies.h"
#include "settings/business/settings_working_hours.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "settings/sections/settings_premium.h"
#include "ui/controls/swipe_handler.h"
#include "ui/controls/swipe_handler_data.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	PremiumFeature feature = PremiumFeature::BusinessLocation;
	bool newBadge = false;
};

struct BusinessState {
	Fn<void(bool)> setPaused;
	QPointer<Ui::SettingsButton> sponsoredButton;
	base::flat_map<PremiumFeature, QPointer<Ui::SettingsButton>> featureButtons;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		u"greeting_message"_q,
		u"away_message"_q,
		u"quick_replies"_q,
		u"business_hours"_q,
		u"business_location"_q,
		u"business_links"_q,
		u"business_intro"_q,
		u"business_bots"_q,
		u"folder_tags"_q,
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
				true,
			},
		},
		{
			u"business_intro"_q,
			Entry{
				&st::settingsBusinessIconChatIntro,
				tr::lng_business_subtitle_chat_intro(),
				tr::lng_business_about_chat_intro(),
				PremiumFeature::ChatIntro,
			},
		},
		{
			u"business_links"_q,
			Entry{
				&st::settingsBusinessIconChatLinks,
				tr::lng_business_subtitle_chat_links(),
				tr::lng_business_about_chat_links(),
				PremiumFeature::ChatLinks,
			},
		},
		{
			u"folder_tags"_q,
			Entry{
				&st::settingsPremiumIconTags,
				tr::lng_premium_summary_subtitle_filter_tags(),
				tr::lng_premium_summary_about_filter_tags(),
				PremiumFeature::FilterTags,
			},
		},
	};
}

void AddBusinessSummary(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<BusinessState> state,
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
				std::move(entry.title) | rpl::map(tr::bold),
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

		if (entry.newBadge) {
			Ui::NewBadge::AddAfterLabel(content, label);
		}
		const auto dummy = Ui::CreateChild<Ui::AbstractButton>(content.get());
		dummy->setAttribute(Qt::WA_TransparentForMouseEvents);

		content->sizeValue(
		) | rpl::on_next([=](const QSize &s) {
			dummy->resize(s.width(), iconSize.height());
		}, dummy->lifetime());

		label->geometryValue(
		) | rpl::on_next([=](const QRect &r) {
			dummy->moveToLeft(0, r.y() + (r.height() - labelAscent));
		}, dummy->lifetime());

		rpl::combine(
			content->widthValue(),
			label->heightValue(),
			description->heightValue()
		) | rpl::on_next([=,
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
		) | rpl::on_next([=, padding = titlePadding.top()](int top) {
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
		) | rpl::on_next([=](const QSize &s) {
			const auto &point = st::settingsPremiumArrowShift;
			arrow->moveToRight(
				-point.x(),
				point.y() + (s.height() - arrow->height()) / 2);
		}, arrow->lifetime());

		const auto feature = entry.feature;
		button->setClickedCallback([=] { buttonCallback(feature); });

		if (state) {
			state->featureButtons[feature] = button;
		}

		iconContainers.push_back(dummy);
	};

	auto icons = std::vector<const style::icon *>();
	icons.reserve(int(entryMap.size()));
	{
		const auto session = &controller->session();
		const auto mtpOrder = session->appConfig().get<Order>(
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

[[nodiscard]] QString FeatureSearchId(PremiumFeature feature) {
	switch (feature) {
	case PremiumFeature::GreetingMessage: return u"business/greeting"_q;
	case PremiumFeature::AwayMessage: return u"business/away"_q;
	case PremiumFeature::QuickReplies: return u"business/replies"_q;
	case PremiumFeature::BusinessHours: return u"business/hours"_q;
	case PremiumFeature::BusinessLocation: return u"business/location"_q;
	case PremiumFeature::ChatLinks: return u"business/links"_q;
	case PremiumFeature::ChatIntro: return u"business/intro"_q;
	case PremiumFeature::BusinessBots: return u"business/bots"_q;
	case PremiumFeature::FilterTags: return u"business/tags"_q;
	default: return QString();
	}
}

[[nodiscard]] QString FeatureSearchTitle(PremiumFeature feature) {
	switch (feature) {
	case PremiumFeature::GreetingMessage:
		return tr::lng_business_subtitle_greeting_messages(tr::now);
	case PremiumFeature::AwayMessage:
		return tr::lng_business_subtitle_away_messages(tr::now);
	case PremiumFeature::QuickReplies:
		return tr::lng_business_subtitle_quick_replies(tr::now);
	case PremiumFeature::BusinessHours:
		return tr::lng_business_subtitle_opening_hours(tr::now);
	case PremiumFeature::BusinessLocation:
		return tr::lng_business_subtitle_location(tr::now);
	case PremiumFeature::ChatLinks:
		return tr::lng_business_subtitle_chat_links(tr::now);
	case PremiumFeature::ChatIntro:
		return tr::lng_business_subtitle_chat_intro(tr::now);
	case PremiumFeature::BusinessBots:
		return tr::lng_business_subtitle_chatbots(tr::now);
	case PremiumFeature::FilterTags:
		return tr::lng_premium_summary_subtitle_filter_tags(tr::now);
	default: return QString();
	}
}

void BuildBusinessFeatures(SectionBuilder &builder) {
	const auto session = builder.session();
	const auto mtpOrder = session->appConfig().get<Order>(
		"business_promo_order",
		FallbackOrder());

	const auto entryMap = EntryMap();
	for (const auto &key : mtpOrder) {
		const auto it = entryMap.find(key);
		if (it == end(entryMap)) {
			continue;
		}
		const auto feature = it->second.feature;
		const auto id = FeatureSearchId(feature);
		if (id.isEmpty()) {
			continue;
		}
		builder.add(nullptr, [feature, id] {
			return SearchEntry{
				.id = id,
				.title = FeatureSearchTitle(feature),
				.keywords = { u"business"_q },
			};
		});
	}
}

void BuildSponsoredSection(
		SectionBuilder &builder,
		std::shared_ptr<BusinessState> state) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"business/sponsored"_q,
			.title = tr::lng_business_button_sponsored(tr::now),
			.keywords = { u"ads"_q, u"advertising"_q },
		};
	});

	if (!controller) {
		return;
	}

	builder.add([controller, session, state](const WidgetContext &ctx) {
		const auto content = ctx.container;
		const auto sponsoredWrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));

		const auto fillSponsoredWrap = [=] {
			while (sponsoredWrap->entity()->count()) {
				delete sponsoredWrap->entity()->widgetAt(0);
			}
			Ui::AddDivider(sponsoredWrap->entity());
			const auto loading = sponsoredWrap->entity()->add(
				object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
					sponsoredWrap->entity(),
					object_ptr<Ui::FlatLabel>(
						sponsoredWrap->entity(),
						tr::lng_contacts_loading())),
				st::boxRowPadding);
			loading->entity()->setTextColorOverride(st::windowSubTextFg->c);

			const auto wrap = sponsoredWrap->entity()->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					sponsoredWrap->entity(),
					object_ptr<Ui::VerticalLayout>(sponsoredWrap->entity())));
			wrap->toggle(false, anim::type::instant);
			const auto inner = wrap->entity();
			Ui::AddSkip(inner);
			Ui::AddSubsectionTitle(
				inner,
				tr::lng_business_subtitle_sponsored());
			const auto button = inner->add(object_ptr<Ui::SettingsButton>(
				inner,
				tr::lng_business_button_sponsored()));
			if (state) {
				state->sponsoredButton = button;
			}
			Ui::AddSkip(inner);

			{
				inner->add(object_ptr<Ui::DividerLabel>(
					inner,
					object_ptr<Ui::FlatLabel>(
						inner,
						tr::lng_business_about_sponsored(
							lt_link,
							rpl::combine(
								tr::lng_business_about_sponsored_link(
									lt_emoji,
									rpl::single(Ui::Text::IconEmoji(
										&st::textMoreIconEmoji)),
									tr::rich),
								tr::lng_business_about_sponsored_url()
							) | rpl::map([](TextWithEntities text, QString url) {
								return tr::link(text, url);
							}),
							tr::rich),
						st::boxDividerLabel),
					st::defaultBoxDividerLabelPadding));
			}

			const auto api = inner->lifetime().make_state<Api::SponsoredToggle>(
				session);

			api->toggled(
			) | rpl::on_next([=](bool enabled) {
				button->toggleOn(rpl::single(enabled));
				wrap->toggle(true, anim::type::instant);
				loading->toggle(false, anim::type::instant);

				button->toggledChanges(
				) | rpl::on_next([=](bool toggled) {
					api->setToggled(
						toggled
					) | rpl::on_error_done([=](const QString &error) {
						controller->showToast(error);
					}, [] {
					}, button->lifetime());
				}, button->lifetime());
			}, inner->lifetime());

			Ui::ToggleChildrenVisibility(sponsoredWrap->entity(), true);
			sponsoredWrap->entity()->resizeToWidth(content->width());
		};

		Data::AmPremiumValue(session) | rpl::on_next([=](bool isPremium) {
			sponsoredWrap->toggle(isPremium, anim::type::normal);
			if (isPremium) {
				fillSponsoredWrap();
			}
		}, content->lifetime());

		return SectionBuilder::WidgetToAdd{};
	});
}

void BuildBusinessSectionContent(
		SectionBuilder &builder,
		std::shared_ptr<BusinessState> state) {
	const auto controller = builder.controller();
	const auto session = builder.session();

	if (controller) {
		const auto owner = &session->data();
		owner->chatbots().preload();
		owner->businessInfo().preload();
		owner->shortcutMessages().preloadShortcuts();
		owner->session().api().chatLinks().preload();
	}

	builder.addSkip(st::settingsFromFileTop);

	BuildBusinessFeatures(builder);

	if (controller) {
		builder.add([controller, session, state](const WidgetContext &ctx) {
			const auto content = ctx.container;
			const auto owner = &session->data();
			const auto showOther = ctx.showOther;

			auto waitingToShow = content->lifetime().make_state<PremiumFeature>(
				PremiumFeature::Business);

			const auto showFeature = [=](PremiumFeature feature) {
				if (feature == PremiumFeature::FilterTags) {
					ShowPremiumPreviewToBuy(controller, feature);
					return;
				}
				showOther([&] {
					switch (feature) {
					case PremiumFeature::AwayMessage: return AwayMessageId();
					case PremiumFeature::BusinessHours: return WorkingHoursId();
					case PremiumFeature::BusinessLocation: return LocationId();
					case PremiumFeature::GreetingMessage: return GreetingId();
					case PremiumFeature::QuickReplies: return QuickRepliesId();
					case PremiumFeature::BusinessBots: return ChatbotsId();
					case PremiumFeature::ChatIntro: return ChatIntroId();
					case PremiumFeature::ChatLinks: return ChatLinksId();
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
				case PremiumFeature::ChatIntro:
					return owner->session().user()->isFullLoaded();
				case PremiumFeature::ChatLinks:
					return owner->session().api().chatLinks().loaded();
				case PremiumFeature::FilterTags:
					return true;
				}
				Unexpected("Feature in isReady.");
			};

			const auto check = [=] {
				if (*waitingToShow != PremiumFeature::Business
					&& isReady(*waitingToShow)) {
					showFeature(
						std::exchange(*waitingToShow, PremiumFeature::Business));
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
					Data::PeerUpdate::Flag::FullInfo) | rpl::to_empty,
				owner->session().api().chatLinks().loadedUpdates()
			) | rpl::on_next(check, content->lifetime());

			AddBusinessSummary(content, controller, state, [=](PremiumFeature feature) {
				if (!session->premium()) {
					if (state && state->setPaused) {
						state->setPaused(true);
					}
					const auto hidden = crl::guard(content, [=] {
						if (state && state->setPaused) {
							state->setPaused(false);
						}
					});

					ShowPremiumPreviewToBuy(controller, feature, hidden);
					return;
				} else if (!isReady(feature)) {
					*waitingToShow = feature;
				} else {
					showFeature(feature);
				}
			});

			return SectionBuilder::WidgetToAdd{};
		});
	}

	BuildSponsoredSection(builder, state);
}

class Business : public Section<Business> {
public:
	Business(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;
	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void showFinished() override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();
	void setupSwipeBack();

	std::shared_ptr<BusinessState> _state;
	QPointer<Ui::GradientButton> _subscribe;
	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	std::shared_ptr<Ui::RadiobuttonGroup> _radioGroup;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;
	rpl::variable<QString> _buttonText;

};

Business::Business(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _state(std::make_shared<BusinessState>())
, _radioGroup(std::make_shared<Ui::RadiobuttonGroup>()) {
	setupContent();
	setupSwipeBack();
	controller->session().api().premium().reload();
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

void Business::setupSwipeBack() {
	using namespace Ui::Controls;

	auto swipeBackData = lifetime().make_state<SwipeBackResult>();

	auto update = [=](SwipeContextData data) {
		if (data.translation > 0) {
			if (!swipeBackData->callback) {
				(*swipeBackData) = SetupSwipeBack(
					this,
					[]() -> std::pair<QColor, QColor> {
						return {
							st::historyForwardChooseBg->c,
							st::historyForwardChooseFg->c,
						};
					});
			}
			swipeBackData->callback(data);
			return;
		} else if (swipeBackData->lifetime) {
			(*swipeBackData) = {};
		}
	};

	auto init = [=](int, Qt::LayoutDirection direction) {
		return (direction == Qt::RightToLeft)
			? DefaultSwipeBackHandlerFinishData([=] {
				_showBack.fire({});
			})
			: SwipeHandlerFinishData();
	};

	SetupSwipeHandler({
		.widget = this,
		.scroll = v::null,
		.update = std::move(update),
		.init = std::move(init),
	});
}

void Business::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto state = _state;

	const SectionBuildMethod buildMethod = [state](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = isPaused,
		});

		BuildBusinessSectionContent(builder, state);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

base::weak_qptr<Ui::RpWidget> Business::createPinnedToTop(
		not_null<QWidget*> parent) {
	auto title = tr::lng_business_title();
	auto about = [&]() -> rpl::producer<TextWithEntities> {
		return rpl::conditional(
			Data::AmPremiumValue(&controller()->session()),
			tr::lng_business_unlocked(tr::marked),
			tr::lng_business_about(tr::marked));
	}();

	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		const auto weak = base::make_weak(controller());
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
	_state->setPaused = [=](bool paused) {
		content->setPaused(paused);
		if (_subscribe) {
			_subscribe->setGlarePaused(paused);
		}
	};

	_wrap.value(
	) | rpl::on_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	content->setMaximumHeight(st::settingsPremiumTopHeight);
	content->setMinimumHeight(st::settingsPremiumTopHeight);

	content->resize(content->width(), content->maximumHeight());

	_wrap.value(
	) | rpl::on_next([=](Info::Wrap wrap) {
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
			? _backToggles.value() | rpl::type_erased
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			_showBack.fire({});
		});
		_back->toggledValue(
		) | rpl::on_next([=](bool toggled) {
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
				controller()->parentController()->hideLayer();
				controller()->parentController()->hideSpecialLayer();
			});
			content->widthValue(
			) | rpl::on_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, content->lifetime());

	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

void Business::showFinished() {
	_showFinished.fire({});
	crl::on_main(this, [=] {
		for (const auto &[feature, button] : _state->featureButtons) {
			const auto id = FeatureSearchId(feature);
			if (!id.isEmpty()) {
				controller()->checkHighlightControl(id, button);
			}
		}
		controller()->checkHighlightControl(
			u"business/sponsored"_q,
			_state->sponsoredButton);
	});
}

base::weak_qptr<Ui::RpWidget> Business::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::RpWidget>(parent.get());

	const auto session = &controller()->session();

	auto buttonText = _buttonText.value();

	_subscribe = CreateSubscribeButton({
		controller(),
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
			auto &api = controller()->session().api();
			const auto options = api.premium().subscriptionOptions();
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
	) | rpl::take(1) | rpl::on_next([=] {
		_subscribe->startGlareAnimation();
	}, _subscribe->lifetime());

	content->widthValue(
	) | rpl::on_next([=](int width) {
		const auto padding = st::settingsPremiumButtonPadding;
		_subscribe->resizeToWidth(width - padding.left() - padding.right());
	}, _subscribe->lifetime());

	rpl::combine(
		_subscribe->heightValue(),
		Data::AmPremiumValue(session),
		session->premiumPossibleValue()
	) | rpl::on_next([=](
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

	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

const auto kMeta = BuildHelper({
	.id = Business::Id(),
	.parentId = MainId(),
	.title = &tr::lng_business_title,
	.icon = &st::menuIconShop,
}, [](SectionBuilder &builder) {
	BuildBusinessSectionContent(builder, nullptr);
});

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
	const auto mtpOrder = session->appConfig().get<Order>(
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
		} else if (s == u"business_links"_q) {
			return PremiumFeature::ChatLinks;
		} else if (s == u"business_intro"_q) {
			return PremiumFeature::ChatIntro;
		} else if (s == u"business_bots"_q) {
			return PremiumFeature::BusinessBots;
		} else if (s == u"folder_tags"_q) {
			return PremiumFeature::FilterTags;
		}
		return PremiumFeature::kCount;
	}) | ranges::views::filter([](PremiumFeature feature) {
		return (feature != PremiumFeature::kCount);
	}) | ranges::to_vector;
}

namespace Builder {

SectionBuildMethod BusinessSection = kMeta.build;

} // namespace Builder
} // namespace Settings
