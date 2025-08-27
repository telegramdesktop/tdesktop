/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_sponsored.h"

#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h" // TextContext
#include "data/components/sponsored_messages.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/report_box_graphics.h" // AddReportOptionButton.
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Menu {
namespace {

[[nodiscard]] SponsoredPhrases PhrasesForMessage(FullMsgId fullId) {
	return peerIsChannel(fullId.peer)
		? SponsoredPhrases::Channel
		: SponsoredPhrases::Bot;
}

void AboutBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		SponsoredPhrases phrases,
		const Data::SponsoredMessages::Details &details,
		Data::SponsoredReportAction report) {
	constexpr auto kUrl = "https://promote.telegram.org"_cs;

	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);

	const auto isChannel = (phrases == SponsoredPhrases::Channel);
	const auto isSearch = (phrases == SponsoredPhrases::Search);
	const auto session = &show->session();

	const auto content = box->verticalLayout().get();
	const auto levels = Data::LevelLimits(session)
		.channelRestrictSponsoredLevelMin();

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto &icon = st::sponsoredAboutTitleIcon;
		const auto rect = Rect(icon.size() * 1.4);
		auto owned = object_ptr<Ui::RpWidget>(content);
		owned->resize(rect.size());
		owned->setNaturalWidth(rect.width());
		const auto widget = box->addRow(std::move(owned), style::al_top);
		widget->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(widget);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::activeButtonBg);
			p.drawEllipse(rect);
			icon.paintInCenter(p, rect);
		}, widget->lifetime());
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_sponsored_menu_revenued_about(),
			st::boxTitle),
		style::al_top);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_sponsored_revenued_subtitle(),
			st::channelEarnLearnDescription),
		style::al_top);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto padding = QMargins(
			st::settingsButton.padding.left(),
			st::boxRowPadding.top(),
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom());
		const auto addEntry = [&](
				rpl::producer<QString> title,
				rpl::producer<TextWithEntities> about,
				const style::icon &icon) {
			const auto top = content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					std::move(title),
					st::channelEarnSemiboldLabel),
				padding);
			Ui::AddSkip(content, st::channelEarnHistoryThreeSkip);
			const auto label = content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					std::move(about),
					st::channelEarnHistoryRecipientLabel),
				padding);
			const auto left = Ui::CreateChild<Ui::RpWidget>(
				box->verticalLayout().get());
			left->paintRequest(
			) | rpl::start_with_next([=] {
				auto p = Painter(left);
				icon.paint(p, 0, 0, left->width());
			}, left->lifetime());
			left->resize(icon.size());
			top->geometryValue(
			) | rpl::start_with_next([=](const QRect &g) {
				left->moveToLeft(
					(g.left() - left->width()) / 2,
					g.top() + st::channelEarnHistoryThreeSkip);
			}, left->lifetime());
			return label;
		};
		addEntry(
			tr::lng_sponsored_revenued_info1_title(),
			(isChannel
				? tr::lng_sponsored_revenued_info1_description
				: isSearch
				? tr::lng_sponsored_revenued_info1_search_description
				: tr::lng_sponsored_revenued_info1_bot_description)(
					Ui::Text::RichLangValue),
			st::sponsoredAboutPrivacyIcon);
		if (!isSearch) {
			Ui::AddSkip(content);
			Ui::AddSkip(content);
			addEntry(
				(isChannel
					? tr::lng_sponsored_revenued_info2_title
					: tr::lng_sponsored_revenued_info2_bot_title)(),
				(isChannel
					? tr::lng_sponsored_revenued_info2_description
					: tr::lng_sponsored_revenued_info2_bot_description)(
						Ui::Text::RichLangValue),
				st::sponsoredAboutSplitIcon);
		}
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		auto link = tr::lng_settings_privacy_premium_link(
		) | rpl::map([](QString t) {
			return Ui::Text::Link(std::move(t), u"internal:"_q);
		});
		addEntry(
			tr::lng_sponsored_revenued_info3_title(),
			(isChannel
				? tr::lng_sponsored_revenued_info3_description(
					lt_count,
					rpl::single(float64(levels)),
					lt_link,
					std::move(link),
					Ui::Text::RichLangValue)
				: isSearch
				? tr::lng_sponsored_revenued_info3_search_description(
					lt_link,
					tr::lng_sponsored_revenued_info3_search_link(
						lt_arrow,
						rpl::single(
							Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
						Ui::Text::WithEntities
					) | rpl::map([](TextWithEntities &&link) {
						return Ui::Text::Wrapped(
							std::move(link),
							EntityType::CustomUrl,
							u"internal:"_q);
					}),
					Ui::Text::RichLangValue)
				: tr::lng_sponsored_revenued_info3_bot_description(
					lt_link,
					std::move(link),
					Ui::Text::RichLangValue)),
			st::sponsoredAboutRemoveIcon)->setClickHandlerFilter([=](
					const auto &...) {
				ShowPremiumPreviewBox(show, PremiumFeature::NoAds);
				return true;
			});
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_sponsored_revenued_footer_title(),
				st::boxTitle),
			style::al_top);
	}
	Ui::AddSkip(content);
	{
		const auto arrow = Ui::Text::IconEmoji(&st::textMoreIconEmoji);
		const auto available = box->width()
			- rect::m::sum::h(st::boxRowPadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				content,
				(isChannel
					? tr::lng_sponsored_revenued_footer_description
					: isSearch
					? tr::lng_sponsored_revenued_footer_search_description
					: tr::lng_sponsored_revenued_footer_bot_description)(
						lt_link,
						tr::lng_channel_earn_about_link(
							lt_emoji,
							rpl::single(arrow),
							Ui::Text::RichLangValue
						) | rpl::map([=](TextWithEntities t) {
							return Ui::Text::Link(std::move(t), kUrl.utf16());
						}),
						Ui::Text::RichLangValue),
				st::channelEarnLearnDescription))->resizeToWidth(available);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto &st = st::premiumPreviewDoubledLimitsBox;
		box->setStyle(st);
		auto button = object_ptr<Ui::RoundButton>(
			box,
			tr::lng_box_ok(),
			st::defaultActiveButton);
		button->resizeToWidth(box->width()
			- st.buttonPadding.left()
			- st.buttonPadding.left());
		button->setClickedCallback([=] { box->closeBox(); });
		box->addButton(std::move(button));
	}

	if (!isChannel) {
		const auto top = Ui::CreateChild<Ui::IconButton>(
			box,
			st::infoTopBarMenu);
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			top->raise();
			top->moveToLeft(
				width - top->width() - st::defaultScrollArea.width,
				0);
		}, top->lifetime());
		using MenuPtr = base::unique_qptr<Ui::PopupMenu>;
		const auto menu = top->lifetime().make_state<MenuPtr>();
		top->setClickedCallback([=] {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				box->window(),
				st::popupMenuWithIcons);
			const auto raw = menu->get();
			raw->animatePhaseValue(
			) | rpl::start_with_next([=](Ui::PopupMenu::AnimatePhase phase) {
				top->setForceRippled(false
					|| phase == Ui::PopupMenu::AnimatePhase::Shown
					|| phase == Ui::PopupMenu::AnimatePhase::StartShow);
			}, top->lifetime());
			raw->setDestroyedCallback([=] {
				top->setForceRippled(false);
			});
			FillSponsored(
				Ui::Menu::CreateAddActionCallback(menu->get()),
				show,
				phrases,
				details,
				report,
				{ .skipAbout = true });
			const auto global = top->mapToGlobal(
				QPoint(top->width() / 4 * 3, top->height() / 2));
			raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
			raw->popup(
				QPoint(
					global.x(),
					std::max(global.y(), QCursor::pos().y())));
			return true;
		});
	}
}

void ShowReportSponsoredBox(
		std::shared_ptr<ChatHelpers::Show> show,
		Data::SponsoredReportAction report) {
	const auto guideLink = Ui::Text::Link(
		tr::lng_report_sponsored_reported_link(tr::now),
		u"https://promote.telegram.org/guidelines"_q);

	auto performRequest = [=](
			const auto &repeatRequest,
			Data::SponsoredReportResult::Id id) -> void {
		report.callback(id, [=](const Data::SponsoredReportResult &result) {
			if (!result.error.isEmpty()) {
				show->showToast(result.error);
			}
			if (!result.options.empty()) {
				show->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(rpl::single(result.title));

					for (const auto &option : result.options) {
						const auto button = Ui::AddReportOptionButton(
							box->verticalLayout(),
							option.text,
							nullptr);
						button->setClickedCallback([=] {
							repeatRequest(repeatRequest, option.id);
						});
					}
					if (!id.isNull()) {
						box->addLeftButton(
							tr::lng_create_group_back(),
							[=] { box->closeBox(); });
					} else {
						const auto container = box->verticalLayout();
						Ui::AddSkip(container);
						container->add(object_ptr<Ui::DividerLabel>(
							container,
							object_ptr<Ui::FlatLabel>(
								container,
								tr::lng_report_sponsored_reported_learn(
									lt_link,
									rpl::single(guideLink),
									Ui::Text::WithEntities),
								st::boxDividerLabel),
							st::defaultBoxDividerLabelPadding,
							RectPart::Top | RectPart::Bottom));
					}
					box->addButton(
						tr::lng_close(),
						[=] { show->hideLayer(); });
				}));
			} else {
				constexpr auto kToastDuration = crl::time(4000);
				switch (result.result) {
				case Data::SponsoredReportResult::FinalStep::Hidden: {
					show->showToast(
						tr::lng_report_sponsored_hidden(tr::now),
						kToastDuration);
				} break;
				case Data::SponsoredReportResult::FinalStep::Reported: {
					auto text = tr::lng_report_sponsored_reported(
						tr::now,
						lt_link,
						guideLink,
						Ui::Text::WithEntities);
					show->showToast({
						.text = std::move(text),
						.duration = kToastDuration,
					});
				} break;
				case Data::SponsoredReportResult::FinalStep::Premium: {
					ShowPremiumPreviewBox(show, PremiumFeature::NoAds);
				} break;
				}
				show->hideLayer();
			}
		});
	};
	performRequest(performRequest, Data::SponsoredReportResult::Id());
}

} // namespace

void FillSponsored(
		const Ui::Menu::MenuCallback &addAction,
		std::shared_ptr<ChatHelpers::Show> show,
		SponsoredPhrases phrases,
		const Data::SponsoredMessages::Details &details,
		Data::SponsoredReportAction report,
		SponsoredMenuSettings settings) {
	const auto session = &show->session();
	const auto &info = details.info;
	const auto dark = settings.dark;

	if (!settings.skipInfo && !info.empty()) {
		auto fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
			const auto allText = ranges::accumulate(
				info,
				TextWithEntities(),
				[](TextWithEntities a, TextWithEntities b) {
					return a.text.isEmpty() ? b : a.append('\n').append(b);
				}).text;
			const auto callback = [=] {
				TextUtilities::SetClipboardText({ allText });
				show->showToast(tr::lng_text_copied(tr::now));
			};
			for (const auto &i : info) {
				auto item = base::make_unique_q<Ui::Menu::MultilineAction>(
					menu,
					dark ? st::storiesMenu : st::defaultMenu,
					(dark
						? st::historySponsorInfoItemDark
						: st::historySponsorInfoItem),
					st::historyHasCustomEmojiPosition,
					base::duplicate(i));
				item->clicks(
				) | rpl::start_with_next(callback, menu->lifetime());
				menu->addAction(std::move(item));
				if (i != details.info.back()) {
					menu->addSeparator();
				}
			}
		};
		addAction({
			.text = tr::lng_sponsored_info_menu(tr::now),
			.handler = nullptr,
			.icon = (dark
				? &st::mediaMenuIconChannel
				: &st::menuIconChannel),
			.fillSubmenu = std::move(fillSubmenu),
		});
		addAction({
			.separatorSt = (dark
				? &st::mediaviewMenuSeparator
				: &st::expandedMenuSeparator),
			.isSeparator = true,
		});
	}
	if (details.canReport) {
		if (!settings.skipAbout) {
			addAction(tr::lng_sponsored_menu_revenued_about(tr::now), [=] {
				show->show(Box(AboutBox, show, phrases, details, report));
			}, (dark ? &st::mediaMenuIconInfo : &st::menuIconInfo));
		}

		addAction(tr::lng_sponsored_menu_revenued_report(tr::now), [=] {
			ShowReportSponsoredBox(show, report);
		}, (dark ? &st::mediaMenuIconBlock : &st::menuIconBlock));

		addAction({
			.separatorSt = (dark
				? &st::mediaviewMenuSeparator
				: &st::expandedMenuSeparator),
			.isSeparator = true,
		});
	}
	addAction(tr::lng_sponsored_hide_ads(tr::now), [=] {
		if (session->premium()) {
			using Result = Data::SponsoredReportResult;
			report.callback(Result::Id("-1"), [](const auto &) {});
		} else {
			ShowPremiumPreviewBox(show, PremiumFeature::NoAds);
		}
	}, (dark ? &st::mediaMenuIconCancel : &st::menuIconCancel));
}

void FillSponsored(
		const Ui::Menu::MenuCallback &addAction,
		std::shared_ptr<ChatHelpers::Show> show,
		const FullMsgId &fullId,
		SponsoredMenuSettings settings) {
	const auto session = &show->session();
	FillSponsored(
		addAction,
		show,
		PhrasesForMessage(fullId),
		session->sponsoredMessages().lookupDetails(fullId),
		session->sponsoredMessages().createReportCallback(fullId),
		settings);
}

void ShowSponsored(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		const FullMsgId &fullId) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent.get(),
		st::popupMenuWithIcons);

	FillSponsored(
		Ui::Menu::CreateAddActionCallback(menu),
		show,
		fullId);

	menu->popup(QCursor::pos());
}

void ShowSponsoredAbout(
		std::shared_ptr<ChatHelpers::Show> show,
		const FullMsgId &fullId) {
	const auto session = &show->session();
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		AboutBox(
			box,
			show,
			PhrasesForMessage(fullId),
			session->sponsoredMessages().lookupDetails(fullId),
			session->sponsoredMessages().createReportCallback(fullId));
	}));
}

} // namespace Menu
