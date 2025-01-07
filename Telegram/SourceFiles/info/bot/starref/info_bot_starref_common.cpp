/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/starref/info_bot_starref_common.h"

#include "apiwrap.h"
#include "boxes/peers/replace_boost_box.h" // CreateUserpicsTransfer.
#include "boxes/send_credits_box.h" // Ui::CreditsEmoji.
#include "chat_helpers/stickers_lottie.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <QtWidgets/QApplication>

namespace Info::BotStarRef {
namespace {

void ConnectStarRef(
		not_null<UserData*> bot,
		not_null<PeerData*> peer,
		Fn<void(ConnectedBot)> done,
		Fn<void(const QString &)> fail) {
	bot->session().api().request(MTPpayments_ConnectStarRefBot(
		peer->input,
		bot->inputUser
	)).done([=](const MTPpayments_ConnectedStarRefBots &result) {
		const auto parsed = Parse(&bot->session(), result);
		if (parsed.empty()) {
			fail(u"EMPTY"_q);
		} else {
			done(parsed.front());
		}
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateLinkIcon(
		not_null<QWidget*> parent,
		not_null<UserData*> bot,
		int users) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();

	struct State {
		not_null<DocumentData*> icon;
		std::shared_ptr<Data::DocumentMedia> media;
		std::shared_ptr<HistoryView::StickerPlayer> player;
		int counterWidth = 0;
	};
	const auto outerSide = st::starrefLinkThumbOuter;
	const auto outerSkip = (outerSide - st::starrefLinkThumbInner) / 2;
	const auto innerSide = (outerSide - 2 * outerSkip);
	const auto add = st::starrefLinkCountAdd;
	const auto outer = QSize(outerSide, outerSide + add);
	const auto inner = QSize(innerSide, innerSide);
	const auto state = raw->lifetime().make_state<State>(State{
		.icon = ChatHelpers::GenerateLocalTgsSticker(
			&bot->session(),
			u"starref_link"_q),
	});
	state->icon->overrideEmojiUsesTextColor(true);
	state->media = state->icon->createMediaView();
	state->player = std::make_unique<HistoryView::LottiePlayer>(
		ChatHelpers::LottiePlayerFromDocument(
			state->media.get(),
			ChatHelpers::StickerLottieSize::MessageHistory,
			inner,
			Lottie::Quality::High));
	const auto player = state->player.get();
	player->setRepaintCallback([=] { raw->update(); });

	const auto text = users
		? Lang::FormatCountToShort(users).string
		: QString();
	const auto length = st::starrefLinkCountFont->width(text);
	const auto contents = length + st::starrefLinkCountIcon.width();
	const auto delta = (outer.width() - contents) / 2;
	const auto badge = QRect(
		delta,
		outer.height() - st::starrefLinkCountFont->height - st::lineWidth,
		outer.width() - 2 * delta,
		st::starrefLinkCountFont->height);
	const auto badgeRect = badge.marginsAdded(st::starrefLinkCountPadding);

	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);

		auto hq = PainterHighQualityEnabler(p);

		const auto left = (raw->width() - outer.width()) / 2;
		p.drawEllipse(left, 0, outerSide, outerSide);

		if (!text.isEmpty()) {
			const auto rect = badgeRect.translated(left, 0);
			const auto textRect = badge.translated(left, 0);
			const auto radius = st::starrefLinkCountFont->height / 2.;
			p.setPen(st::historyPeerUserpicFg);
			p.setBrush(st::historyPeer2UserpicBg2);
			p.drawRoundedRect(rect, radius, radius);

			p.setFont(st::starrefLinkCountFont);
			const auto shift = QPoint(
				st::starrefLinkCountIcon.width(),
				st::starrefLinkCountFont->ascent);
			st::starrefLinkCountIcon.paint(
				p,
				textRect.topLeft() + st::starrefLinkCountIconPosition,
				raw->width());
			p.drawText(textRect.topLeft() + shift, text);
		}
		if (player->ready()) {
			const auto now = crl::now();
			const auto color = st::windowFgActive->c;
			auto info = player->frame(inner, color, false, now, false);
			p.drawImage(
				QRect(QPoint(left + outerSkip, outerSkip), inner),
				info.image);
			if (info.index + 1 < player->framesCount()) {
				player->markFrameShown();
			}
		}
	}, raw->lifetime());

	raw->resize(outer);

	return result;
}

void ChooseRecipient(
		not_null<Ui::RpWidget*> button,
		const std::vector<not_null<PeerData*>> &list,
		not_null<PeerData*> now,
		Fn<void(not_null<PeerData*>)> done) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		button,
		st::starrefPopupMenu);

	struct Entry {
		not_null<Ui::WhoReactedEntryAction*> action;
		std::shared_ptr<Ui::DynamicImage> userpic;
	};
	auto actions = std::make_shared<std::vector<Entry>>();
	actions->reserve(list.size());
	for (const auto &peer : list) {
		auto view = peer->createUserpicView();
		auto action = base::make_unique_q<Ui::WhoReactedEntryAction>(
			menu->menu(),
			Data::ReactedMenuFactory(&list.front()->session()),
			menu->menu()->st(),
			Ui::WhoReactedEntryData());
		const auto index = int(actions->size());
		actions->push_back({ action.get(), Ui::MakeUserpicThumbnail(peer) });

		const auto updateUserpic = [=] {
			const auto size = st::defaultWhoRead.photoSize;
			actions->at(index).action->setData({
				.text = peer->name(),
				.date = (peer->isSelf()
					? tr::lng_group_call_join_as_personal(tr::now)
					: peer->isUser()
					? tr::lng_status_bot(tr::now)
					: peer->isBroadcast()
					? tr::lng_channel_status(tr::now)
					: tr::lng_group_status(tr::now)),
				.type = (peer == now
					? Ui::WhoReactedType::RefRecipientNow
					: Ui::WhoReactedType::RefRecipient),
				.userpic = actions->at(index).userpic->image(size),
				.callback = [=] { done(peer); },
			});
		};
		actions->back().userpic->subscribeToUpdates(updateUserpic);

		menu->addAction(std::move(action));
		updateUserpic();
	}

	menu->popup(button->mapToGlobal(QPoint(button->width() / 2, 0)));
}

} // namespace

QString FormatCommission(ushort commission) {
	return QString::number(commission / 10.) + '%';
}

QString FormatProgramDuration(int durationMonths) {
	return !durationMonths
		? tr::lng_star_ref_duration_forever(tr::now)
		: (durationMonths < 12)
		? tr::lng_months(tr::now, lt_count, durationMonths)
		: tr::lng_years(tr::now, lt_count, durationMonths / 12);
}

rpl::producer<TextWithEntities> FormatForProgramDuration(
		int durationMonths) {
	return !durationMonths
		? tr::lng_star_ref_one_about_for_forever(Ui::Text::RichLangValue)
		: (durationMonths < 12)
		? tr::lng_star_ref_one_about_for_months(
			lt_count,
			rpl::single(durationMonths * 1.),
			Ui::Text::RichLangValue)
		: tr::lng_star_ref_one_about_for_years(
			lt_count,
			rpl::single((durationMonths / 12) * 1.),
			Ui::Text::RichLangValue);
}

not_null<Ui::AbstractButton*> AddViewListButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle,
		bool newBadge) {
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	const auto button = Ui::CreateChild<Ui::SettingsButton>(
		parent,
		rpl::single(QString()));
	button->show();

	const auto label = parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(title) | Ui::Text::ToBold(),
			stLabel),
		titlePadding);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto description = parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(subtitle),
			st::boxDividerLabel),
		descriptionPadding);
	description->setAttribute(Qt::WA_TransparentForMouseEvents);

	if (newBadge) {
		Ui::NewBadge::AddAfterLabel(parent, label);
	}

	const auto dummy = Ui::CreateChild<Ui::AbstractButton>(parent);
	dummy->setAttribute(Qt::WA_TransparentForMouseEvents);
	dummy->show();

	parent->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		dummy->resize(s.width(), iconSize.height());
	}, dummy->lifetime());

	button->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		dummy->moveToLeft(0, r.y() + (r.height() - iconSize.height()) / 2);
	}, dummy->lifetime());

	::Settings::AddButtonIcon(dummy, st::settingsButton, {
		.icon = &st::settingsStarRefEarnStars,
		.backgroundBrush = st::premiumIconBg3,
	});

	rpl::combine(
		parent->widthValue(),
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

	return button;
}

not_null<Ui::RoundButton*> AddFullWidthButton(
		not_null<Ui::BoxContent*> box,
		rpl::producer<QString> text,
		Fn<void()> callback,
		const style::RoundButton *stOverride) {
	const auto &boxSt = box->getDelegate()->style();
	const auto result = box->addButton(
		std::move(text),
		std::move(callback),
		stOverride ? *stOverride : boxSt.button);
	rpl::combine(
		box->widthValue(),
		result->widthValue()
	) | rpl::start_with_next([=](int width, int buttonWidth) {
		const auto correct = width
			- boxSt.buttonPadding.left()
			- boxSt.buttonPadding.right();
		if (correct > 0 && buttonWidth != correct) {
			result->resizeToWidth(correct);
			result->moveToLeft(
				boxSt.buttonPadding.left(),
				boxSt.buttonPadding.top(),
				width);
		}
	}, result->lifetime());
	return result;
}

void AddFullWidthButtonFooter(
		not_null<Ui::BoxContent*> box,
		not_null<Ui::RpWidget*> button,
		rpl::producer<TextWithEntities> text) {
	const auto footer = Ui::CreateChild<Ui::FlatLabel>(
		button->parentWidget(),
		std::move(text),
		st::starrefJoinFooter);
	footer->setTryMakeSimilarLines(true);
	button->geometryValue() | rpl::start_with_next([=](QRect geometry) {
		footer->resizeToWidth(geometry.width());
		const auto &st = box->getDelegate()->style();
		const auto top = geometry.y() + geometry.height();
		const auto available = st.buttonPadding.bottom();
		footer->moveToLeft(
			geometry.left(),
			top + (available - footer->height()) / 2);
	}, footer->lifetime());
}

object_ptr<Ui::AbstractButton> MakeLinkLabel(
		not_null<QWidget*> parent,
		const QString &link) {
	const auto text = link.startsWith(u"https://"_q)
		? link.mid(8)
		: link.startsWith(u"http://"_q)
		? link.mid(7)
		: link;
	const auto margins = st::dialogsFilter.textMargins;
	const auto height = st::dialogsFilter.heightMin;
	const auto skip = margins.left();

	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	raw->resize(height, height);
	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::dialogsFilter.textBg);
		const auto radius = st::roundRadiusLarge;
		p.drawRoundedRect(0, 0, raw->width(), height, radius, radius);

		const auto font = st::dialogsFilter.style.font;
		p.setPen(st::dialogsFilter.textFg);
		p.setFont(font);
		const auto available = raw->width() - skip * 2;
		p.drawText(
			QRect(skip, margins.top(), available, font->height),
			style::al_top,
			font->elided(link, available));
	}, raw->lifetime());

	return result;
}

object_ptr<Ui::BoxContent> StarRefLinkBox(
		ConnectedBot row,
		not_null<PeerData*> peer) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		const auto show = box->uiShow();

		const auto bot = row.bot;
		const auto program = row.state.program;

		box->setStyle(st::starrefFooterBox);
		box->setNoContentMargin(true);
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		box->addRow(
			CreateLinkIcon(box, bot, row.state.users),
			st::boxRowPadding + st::starrefJoinUserpicsPadding);
		box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_link_title(),
					st::boxTitle)),
			st::boxRowPadding + st::starrefJoinTitlePadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				(peer->isSelf()
					? tr::lng_star_ref_link_about_user
					: peer->isUser()
					? tr::lng_star_ref_link_about_user
					: tr::lng_star_ref_link_about_channel)(
						lt_amount,
						rpl::single(Ui::Text::Bold(
							FormatCommission(program.commission))),
						lt_app,
						rpl::single(Ui::Text::Bold(bot->name())),
						lt_duration,
						FormatForProgramDuration(program.durationMonths),
						Ui::Text::WithEntities),
				st::starrefCenteredText),
			st::boxRowPadding);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 3);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_star_ref_link_recipient(),
				st::starrefCenteredText));
		Ui::AddSkip(box->verticalLayout());
		box->addRow(object_ptr<Ui::AbstractButton>::fromRaw(
			MakePeerBubbleButton(box, peer).release()
		))->setAttribute(Qt::WA_TransparentForMouseEvents);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 2);
		const auto preview = box->addRow(MakeLinkLabel(box, row.state.link));
		Ui::AddSkip(box->verticalLayout());

		const auto copy = [=](bool close) {
			return [=] {
				QApplication::clipboard()->setText(row.state.link);
				box->uiShow()->showToast(tr::lng_username_copied(tr::now));
				if (close) {
					box->closeBox();
				}
			};
		};
		preview->setClickedCallback(copy(false));
		const auto button = AddFullWidthButton(
			box,
			tr::lng_star_ref_link_copy(),
			copy(true),
			&st::starrefCopyButton);
		const auto name = TextWithEntities{ bot->name() };
		AddFullWidthButtonFooter(
			box,
			button,
			(row.state.users > 0
				? tr::lng_star_ref_link_copy_users(
					lt_count,
					rpl::single(row.state.users * 1.),
					lt_app,
					rpl::single(name),
					Ui::Text::WithEntities)
				: tr::lng_star_ref_link_copy_none(
					lt_app,
					rpl::single(name),
					Ui::Text::WithEntities)));
	});
}

object_ptr<Ui::BoxContent> JoinStarRefBox(
		ConnectedBot row,
		not_null<PeerData*> initialRecipient,
		std::vector<not_null<PeerData*>> recipients,
		Fn<void(ConnectedBotState)> done) {
	Expects(row.bot->isUser());

	return Box([=](not_null<Ui::GenericBox*> box) {
		const auto show = box->uiShow();

		const auto bot = row.bot;
		const auto program = row.state.program;
		auto list = recipients;
		if (!list.empty()) {
			list.erase(ranges::remove(list, bot), end(list));
			if (!ranges::contains(list, initialRecipient)) {
				list.insert(begin(list), initialRecipient);
			}
		}

		box->setStyle(st::starrefFooterBox);
		box->setNoContentMargin(true);
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		struct State {
			rpl::variable<not_null<PeerData*>> recipient;
			QPointer<Ui::GenericBox> weak;
			bool sent = false;
		};
		const auto state = std::make_shared<State>(State{
			.recipient = initialRecipient,
			.weak = box.get(),
		});
		const auto userpicsWrap = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			QMargins());

		state->recipient.value(
		) | rpl::start_with_next([=](not_null<PeerData*> recipient) {
			while (userpicsWrap->count()) {
				delete userpicsWrap->widgetAt(0);
			}
			userpicsWrap->add(
				CreateUserpicsTransfer(
					box,
					rpl::single(std::vector{ not_null<PeerData*>(bot) }),
					recipient,
					UserpicsTransferType::StarRefJoin),
				st::boxRowPadding + st::starrefJoinUserpicsPadding);
			userpicsWrap->resizeToWidth(box->width());
		}, box->lifetime());

		box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_title(),
					st::boxTitle)),
			st::boxRowPadding + st::starrefJoinTitlePadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_star_ref_one_about(
					lt_app,
					rpl::single(Ui::Text::Bold(bot->name())),
					lt_amount,
					rpl::single(Ui::Text::Bold(
						FormatCommission(program.commission))),
					lt_duration,
					FormatForProgramDuration(program.durationMonths),
					Ui::Text::WithEntities),
				st::starrefCenteredText),
			st::boxRowPadding);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 3);
		if (const auto average = program.revenuePerUser) {
			const auto layout = box->verticalLayout();
			const auto session = &initialRecipient->session();
			const auto makeContext = [session](Fn<void()> update) {
				return Core::MarkedTextContext{
					.session = session,
					.customEmojiRepaint = std::move(update),
				};
			};
			auto text = Ui::Text::Colorized(Ui::CreditsEmoji(session));
			text.append(Lang::FormatStarsAmountRounded(average));
			layout->add(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_one_daily_revenue(
						lt_amount,
						rpl::single(
							Ui::Text::Wrapped(text, EntityType::Bold)),
						Ui::Text::WithEntities),
					st::starrefRevenueText,
					st::defaultPopupMenu,
					makeContext),
				st::boxRowPadding);
			Ui::AddSkip(layout, st::defaultVerticalListSkip);
		}

		if (!list.empty()) {
			struct Name {
				not_null<PeerData*> peer;
				QString name;
			};
			auto names = ranges::views::transform(list, [](auto peer) {
				const auto name = TextUtilities::NameSortKey(peer->name());
				return Name{ peer, name };
			}) | ranges::to_vector;
			ranges::sort(names, ranges::less(), &Name::name);

			list = ranges::views::transform(names, &Name::peer)
				| ranges::to_vector;

			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_link_recipient(),
					st::starrefCenteredText));
			Ui::AddSkip(box->verticalLayout());
			const auto recipientWrap = box->addRow(
				object_ptr<Ui::VerticalLayout>(box),
				QMargins());
			state->recipient.value(
			) | rpl::start_with_next([=](not_null<PeerData*> recipient) {
				while (recipientWrap->count()) {
					delete recipientWrap->widgetAt(0);
				}

				const auto selectable = (list.size() > 1);
				const auto bgOverride = selectable
					? &st::lightButtonBgOver
					: nullptr;
				const auto right = selectable
					? Ui::CreateChild<Ui::RpWidget>(recipientWrap)
					: nullptr;
				if (right) {
					const auto skip = st::chatGiveawayPeerPadding.right();
					const auto icon = &st::starrefRecipientArrow;
					const auto height = st::chatGiveawayPeerSize
						- st::chatGiveawayPeerPadding.top() * 2;
					right->resize(skip + icon->width(), height);
					right->paintRequest() | rpl::start_with_next([=] {
						auto p = QPainter(right);
						icon->paint(
							p,
							skip,
							(height - icon->height()) / 2,
							right->width());
					}, right->lifetime());
				}
				const auto button = recipientWrap->add(
					object_ptr<Ui::AbstractButton>::fromRaw(
						MakePeerBubbleButton(
							box,
							recipient,
							right,
							bgOverride).release()),
					st::boxRowPadding);
				recipientWrap->resizeToWidth(box->width());
				if (!selectable) {
					button->setAttribute(Qt::WA_TransparentForMouseEvents);
					return;
				}
				button->setClickedCallback([=] {
					const auto callback = [=](not_null<PeerData*> peer) {
						state->recipient = peer;
					};
					ChooseRecipient(
						button,
						list,
						state->recipient.current(),
						crl::guard(button, callback));
				});
			}, box->lifetime());
		}

		const auto send = [=] {
			if (state->sent) {
				return;
			}
			state->sent = true;
			const auto recipient = state->recipient.current();
			ConnectStarRef(bot->asUser(), recipient, [=](ConnectedBot info) {
				if (recipient == initialRecipient) {
					if (const auto onstack = done) {
						onstack(info.state);
					}
				}
				show->show(StarRefLinkBox(info, recipient));
				if (const auto strong = state->weak.data()) {
					strong->closeBox();
				}
			}, [=](const QString &error) {
				state->sent = false;
				show->showToast(u"Failed: "_q + error);
			});
		};
		const auto button = AddFullWidthButton(
			box,
			tr::lng_star_ref_one_join(),
			send);
		AddFullWidthButtonFooter(
			box,
			button,
			tr::lng_star_ref_one_join_text(
				lt_terms,
				tr::lng_star_ref_button_link(
				) | Ui::Text::ToLink(tr::lng_star_ref_tos_url(tr::now)),
				Ui::Text::WithEntities));
	});
}

object_ptr<Ui::BoxContent> ConfirmEndBox(Fn<void()> finish) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_star_ref_warning_title());
		const auto skip = st::defaultVerticalListSkip;
		const auto margins = st::boxRowPadding + QMargins(0, 0, 0, skip);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_star_ref_warning_if_end(Ui::Text::RichLangValue),
				st::boxLabel),
			margins);
		const auto addPoint = [&](tr::phrase<> text) {
			const auto padded = box->addRow(
				object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
					box,
					object_ptr<Ui::FlatLabel>(
						box,
						text(Ui::Text::RichLangValue),
						st::blockUserConfirmation),
					QMargins(st::boxTextFont->height, 0, 0, 0)),
				margins);
			padded->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(padded);
				auto hq = PainterHighQualityEnabler(p);
				const auto size = st::starrefEndBulletSize;
				const auto top = st::starrefEndBulletTop;
				p.setBrush(st::windowFg);
				p.setPen(Qt::NoPen);
				p.drawEllipse(0, top, size, size);
			}, padded->lifetime());
		};
		addPoint(tr::lng_star_ref_warning_if_end1);
		addPoint(tr::lng_star_ref_warning_if_end2);
		addPoint(tr::lng_star_ref_warning_if_end3);
		const auto done = [=] {
			box->closeBox();
			finish();
		};
		box->addButton(
			tr::lng_star_ref_warning_end(),
			done,
			st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	});
}

void ResolveRecipients(
		not_null<Main::Session*> session,
		Fn<void(std::vector<not_null<PeerData*>>)> done) {
	struct State {
		not_null<Main::Session*> session;
		std::vector<not_null<PeerData*>> list;
		Fn<void(std::vector<not_null<PeerData*>>)> done;
	};
	const auto state = std::make_shared<State>(State{
		.session = session,
		.done = std::move(done),
	});
	const auto finish1 = [state](const MTPmessages_Chats &result) {
		const auto already = int(state->list.size());
		const auto session = state->session;
		result.match([&](const auto &data) {
			const auto &list = data.vchats().v;
			state->list.reserve(list.size() + (already ? already : 1));
			if (!already) {
				state->list.push_back(session->user());
			}
			for (const auto &chat : list) {
				const auto peer = session->data().processChat(chat);
				if (const auto channel = peer->asBroadcast()) {
					if (channel->canPostMessages()) {
						state->list.push_back(channel);
					}
				}
			}
			if (already) {
				base::take(state->done)(base::take(state->list));
			}
		});
	};
	const auto finish2 = [state](const MTPVector<MTPUser> &result) {
		const auto already = int(state->list.size());
		const auto session = state->session;
		const auto &list = result.v;
		state->list.reserve(list.size() + (already ? already : 1));
		if (!already) {
			state->list.push_back(session->user());
		}
		for (const auto &user : list) {
			state->list.push_back(session->data().processUser(user));
		}
		if (already) {
			base::take(state->done)(base::take(state->list));
		}
	};

	session->api().request(MTPchannels_GetAdminedPublicChannels(
		MTP_flags(0)
	)).done(finish1).fail([=] {
		finish1(MTP_messages_chats(MTP_vector<MTPChat>(0)));
	}).send();

	state->session->api().request(MTPbots_GetAdminedBots(
	)).done(finish2).fail([=] {
		finish2(MTP_vector<MTPUser>(0));
	}).send();
}

std::unique_ptr<Ui::AbstractButton> MakePeerBubbleButton(
		not_null<QWidget*> parent,
		not_null<PeerData*> peer,
		Ui::RpWidget *right,
		const style::color *bgOverride) {
	class Button final : public Ui::AbstractButton {
	public:
		Button(QWidget *parent, not_null<int*> innerWidth)
		: AbstractButton(parent)
		, _innerWidth(innerWidth) {
		}

		void mouseMoveEvent(QMouseEvent *e) override {
			const auto inner = *_innerWidth;
			const auto skip = (width() - inner) / 2;
			const auto p = e->pos();
			const auto over = QRect(skip, 0, inner, height()).contains(p);
			setOver(over, StateChangeSource::ByHover);
		}

	private:
		const not_null<int*> _innerWidth;

	};

	auto ownedWidth = std::make_unique<int>();
	const auto width = ownedWidth.get();
	auto result = std::make_unique<Button>(parent, width);
	result->lifetime().add([moved = std::move(ownedWidth)] {});

	const auto size = st::chatGiveawayPeerSize;
	const auto padding = st::chatGiveawayPeerPadding;

	const auto raw = result.get();

	const auto name = raw->lifetime().make_state<Ui::FlatLabel>(
		raw,
		rpl::single(peer->name()),
		st::botEmojiStatusName);
	const auto userpic = raw->lifetime().make_state<Ui::UserpicButton>(
		raw,
		peer,
		st::botEmojiStatusUserpic);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

	if (right) {
		right->setParent(result.get());
		right->show();
		right->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	auto rightWidth = right ? right->widthValue() : rpl::single(0);

	raw->resize(size, size);
	rpl::combine(
		raw->sizeValue(),
		std::move(rightWidth)
	) | rpl::start_with_next([=](QSize outer, int rwidth) {
		const auto full = outer.width();
		const auto decorations = size
			+ padding.left()
			+ padding.right()
			+ rwidth;
		const auto inner = full - decorations;
		const auto use = std::min(inner, name->textMaxWidth());
		*width = use + decorations;
		const auto left = (full - *width) / 2;
		if (inner > 0) {
			userpic->moveToLeft(left, 0, outer.width());
			if (right) {
				right->moveToLeft(
					left + *width - padding.right() - right->width(),
					padding.top(),
					outer.width());
			}
			name->resizeToWidth(use);
			name->moveToLeft(
				left + size + padding.left(),
				padding.top(),
				outer.width());
		}
	}, raw->lifetime());
	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - *width) / 2;
		const auto skip = size / 2;
		p.setClipRect(left + skip, 0, *width - skip, size);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bgOverride ? *bgOverride : st::windowBgOver);
		p.drawRoundedRect(left, 0, *width, size, skip, skip);
	}, raw->lifetime());

	return result;
}

void ConfirmUpdate(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		const StarRefProgram &program,
		bool exists,
		Fn<void(Fn<void(bool)>)> update) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto sent = std::make_shared<bool>();
		Ui::ConfirmBox(box, {
			.text = (exists
				? tr::lng_star_ref_warning_change
				: tr::lng_star_ref_warning_text)(Ui::Text::RichLangValue),
			.confirmed = [=](Fn<void()> close) {
				if (*sent) {
					return;
				}
				*sent = true;
				update([=](bool success) {
					*sent = false;
					if (success) {
						close();
					}
				});
			},
			.confirmText = (exists
				? tr::lng_star_ref_warning_update
				: tr::lng_star_ref_warning_start)(),
			.title = tr::lng_star_ref_warning_title(),
		});

		auto table = box->addRow(
			object_ptr<Ui::TableLayout>(
				box,
				st::giveawayGiftCodeTable),
			st::giveawayGiftCodeTableMargin);
		const auto addRow = [&](
				rpl::producer<QString> label,
				const QString &value) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					std::move(label),
					table->st().defaultLabel),
				object_ptr<Ui::FlatLabel>(
					table,
					value,
					table->st().defaultValue,
					st::defaultPopupMenu),
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
		};
		addRow(
			tr::lng_star_ref_commission_title(),
			FormatCommission(program.commission));
		addRow(
			tr::lng_star_ref_duration_title(),
			FormatProgramDuration(program.durationMonths));
	}));
}

void UpdateProgram(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		const StarRefProgram &program,
		Fn<void(bool)> done) {
	using Flag = MTPbots_UpdateStarRefProgram::Flag;
	bot->session().api().request(MTPbots_UpdateStarRefProgram(
		MTP_flags((program.commission > 0 && program.durationMonths > 0)
			? Flag::f_duration_months
			: Flag()),
		bot->inputUser,
		MTP_int(program.commission),
		MTP_int(program.durationMonths)
	)).done([=](const MTPStarRefProgram &result) {
		bot->setStarRefProgram(Data::ParseStarRefProgram(&result));
		done(true);
	}).fail([=](const MTP::Error &error) {
		show->showToast(u"Failed: "_q + error.type());
		done(false);
	}).send();
}

void FinishProgram(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		Fn<void(bool)> done) {
	UpdateProgram(std::move(show), bot, {}, std::move(done));
}

ConnectedBots Parse(
		not_null<Main::Session*> session,
		const MTPpayments_ConnectedStarRefBots &bots) {
	const auto &data = bots.data();
	session->data().processUsers(data.vusers());
	const auto &list = data.vconnected_bots().v;
	auto result = ConnectedBots();
	for (const auto &bot : list) {
		const auto &data = bot.data();
		const auto botId = UserId(data.vbot_id());
		const auto link = qs(data.vurl());
		const auto date = data.vdate().v;
		const auto commission = data.vcommission_permille().v;
		const auto durationMonths
			= data.vduration_months().value_or_empty();
		const auto users = int(data.vparticipants().v);
		const auto revoked = data.is_revoked();
		result.push_back({
			.bot = session->data().user(botId),
			.state = {
				.program = {
					.commission = ushort(commission),
					.durationMonths = uchar(durationMonths),
				},
				.link = link,
				.date = date,
				.users = users,
				.revoked = revoked,
			},
		});
	}
	return result;
}

} // namespace Info::BotStarRef