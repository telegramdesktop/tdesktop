/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_sponsored.h"

#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_sponsored_messages.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Menu {
namespace {

void AboutBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	constexpr auto kUrl = "https://promote.telegram.org"_cs;

	box->setNoContentMargin(true);

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
		const auto widget = box->addRow(object_ptr<Ui::CenterWrap<>>(
			content,
			std::move(owned)))->entity();
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
	box->addRow(object_ptr<Ui::CenterWrap<>>(
		content,
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_sponsored_menu_revenued_about(),
			st::boxTitle)));
	Ui::AddSkip(content);
	box->addRow(object_ptr<Ui::CenterWrap<>>(
		content,
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_sponsored_revenued_subtitle(),
			st::channelEarnLearnDescription)));
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
			content->add(
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
		};
		addEntry(
			tr::lng_sponsored_revenued_info1_title(),
			tr::lng_sponsored_revenued_info1_description(
				Ui::Text::RichLangValue),
			st::sponsoredAboutPrivacyIcon);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		addEntry(
			tr::lng_sponsored_revenued_info2_title(),
			tr::lng_sponsored_revenued_info2_description(
				Ui::Text::RichLangValue),
			st::sponsoredAboutSplitIcon);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		addEntry(
			tr::lng_sponsored_revenued_info3_title(),
			tr::lng_sponsored_revenued_info3_description(
				lt_count,
				rpl::single(float64(levels)),
				lt_link,
				tr::lng_settings_privacy_premium_link(
				) | rpl::map([=](QString t) {
					return Ui::Text::Link(t, kUrl.utf16());
				}),
				Ui::Text::RichLangValue),
			st::sponsoredAboutRemoveIcon);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				content,
				object_ptr<Ui::FlatLabel>(
					content,
					tr::lng_sponsored_revenued_footer_title(),
					st::boxTitle)));
	}
	Ui::AddSkip(content);
	{
		const auto arrow = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::topicButtonArrow,
				st::channelEarnLearnArrowMargins,
				false));
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(
				content,
				st::channelEarnLearnDescription));
		tr::lng_sponsored_revenued_footer_description(
			lt_link,
			tr::lng_channel_earn_about_link(
				lt_emoji,
				rpl::single(arrow),
				Ui::Text::RichLangValue
			) | rpl::map([=](TextWithEntities text) {
				return Ui::Text::Link(std::move(text), kUrl.utf16());
			}),
			Ui::Text::RichLangValue
		) | rpl::start_with_next([=, l = label](
				TextWithEntities t) {
			l->setMarkedText(
				std::move(t),
				Core::MarkedTextContext{
					.session = session,
					.customEmojiRepaint = [=] { l->update(); },
				});
			l->resizeToWidth(box->width()
				- rect::m::sum::h(st::boxRowPadding));
		}, label->lifetime());
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

}

void ShowReportSponsoredBox(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	auto &sponsoredMessages = peer->session().data().sponsoredMessages();
	const auto fullId = item->fullId();
	const auto report = sponsoredMessages.createReportCallback(fullId);
	const auto guideLink = Ui::Text::Link(
		tr::lng_report_sponsored_reported_link(tr::now),
		u"https://promote.telegram.org/guidelines"_q);

	auto performRequest = [=](
			const auto &repeatRequest,
			Data::SponsoredReportResult::Id id) -> void {
		report(id, [=](const Data::SponsoredReportResult &result) {
			if (!result.error.isEmpty()) {
				show->showToast(result.error);
			}
			if (!result.options.empty()) {
				show->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(rpl::single(result.title));

					for (const auto &option : result.options) {
						const auto button = box->verticalLayout()->add(
							object_ptr<Ui::SettingsButton>(
								box,
								rpl::single(QString()),
								st::settingsButtonNoIcon));
						const auto label = Ui::CreateChild<Ui::FlatLabel>(
							button,
							rpl::single(option.text),
							st::sponsoredReportLabel);
						const auto icon = Ui::CreateChild<Ui::RpWidget>(
							button);
						icon->resize(st::settingsPremiumArrow.size());
						icon->paintRequest(
						) | rpl::start_with_next([=, w = icon->width()] {
							auto p = Painter(icon);
							st::settingsPremiumArrow.paint(p, 0, 0, w);
						}, icon->lifetime());
						button->sizeValue(
						) | rpl::start_with_next([=](const QSize &size) {
							const auto left = button->st().padding.left();
							const auto right = button->st().padding.right();
							icon->moveToRight(
								right,
								(size.height() - icon->height()) / 2);
							label->resizeToWidth(size.width()
								- icon->width()
								- left
								- st::settingsButtonRightSkip
								- right);
							label->moveToLeft(
								left,
								(size.height() - label->height()) / 2);
							button->resize(
								button->width(),
								rect::m::sum::v(button->st().padding)
									+ label->height());
						}, button->lifetime());
						label->setAttribute(Qt::WA_TransparentForMouseEvents);
						icon->setAttribute(Qt::WA_TransparentForMouseEvents);
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

void ShowSponsored(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<HistoryItem*> item) {
	Expects(item->isSponsored());

	struct State final {
	};
	const auto state = std::make_shared<State>();

	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent.get(),
		st::popupMenuWithIcons);

	menu->addAction(tr::lng_sponsored_menu_revenued_about(tr::now), [=] {
		show->show(Box(AboutBox, &item->history()->session()));
	}, &st::menuIconInfo);

	menu->addAction(tr::lng_sponsored_menu_revenued_report(tr::now), [=] {
		ShowReportSponsoredBox(show, item);
	}, &st::menuIconBlock);

	menu->addSeparator(&st::expandedMenuSeparator);

	menu->addAction(tr::lng_sponsored_hide_ads(tr::now), [=] {
		ShowPremiumPreviewBox(show, PremiumFeature::NoAds);
	}, &st::menuIconCancel);

	menu->popup(QCursor::pos());
}

void ShowSponsoredAbout(std::shared_ptr<ChatHelpers::Show> show) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		AboutBox(box, &show->session());
	}));
}

} // namespace Menu
