/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/starref/info_bot_starref_common.h"

#include "apiwrap.h"
#include "boxes/peers/replace_boost_box.h" // CreateUserpicsTransfer.
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_chat.h"
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

} // namespace

QString FormatStarRefCommission(ushort commission) {
	return QString::number(commission / 10.) + '%';
}

rpl::producer<TextWithEntities> FormatProgramDuration(
		StarRefProgram program) {
	return !program.durationMonths
		? tr::lng_star_ref_one_about_for_forever(Ui::Text::RichLangValue)
		: (program.durationMonths < 12)
		? tr::lng_star_ref_one_about_for_months(
			lt_count,
			rpl::single(program.durationMonths * 1.),
			Ui::Text::RichLangValue)
		: tr::lng_star_ref_one_about_for_years(
			lt_count,
			rpl::single((program.durationMonths / 12) * 1.),
			Ui::Text::RichLangValue);
}

not_null<Ui::AbstractButton*> AddViewListButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle) {
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
			CreateUserpicsTransfer(
				box,
				rpl::single(std::vector{ not_null<PeerData*>(bot) }),
				peer,
				UserpicsTransferType::StarRefJoin),
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
							FormatStarRefCommission(program.commission))),
						lt_app,
						rpl::single(Ui::Text::Bold(bot->name())),
						lt_duration,
						FormatProgramDuration(program),
						Ui::Text::WithEntities),
				st::starrefCenteredText),
			st::boxRowPadding);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 4);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_star_ref_link_recipient(),
				st::starrefCenteredText));
		Ui::AddSkip(box->verticalLayout());
		box->addRow(object_ptr<Ui::AbstractButton>::fromRaw(
			MakePeerBubbleButton(box, peer).release()
		))->setAttribute(Qt::WA_TransparentForMouseEvents);

		Ui::AddSkip(box->verticalLayout());
		row.state.link;

		const auto copy = [=] {
			QApplication::clipboard()->setText(row.state.link);
			box->uiShow()->showToast(tr::lng_username_copied(tr::now));
			box->closeBox();
		};
		const auto button = AddFullWidthButton(
			box,
			tr::lng_star_ref_link_copy(),
			copy,
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

[[nodiscard]] object_ptr<Ui::BoxContent> JoinStarRefBox(
		ConnectedBot row,
		not_null<PeerData*> peer) {
	Expects(row.bot->isUser());

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
			CreateUserpicsTransfer(
				box,
				rpl::single(std::vector{ not_null<PeerData*>(bot) }),
				peer,
				UserpicsTransferType::StarRefJoin),
			st::boxRowPadding + st::starrefJoinUserpicsPadding);
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
						FormatStarRefCommission(program.commission))),
					lt_duration,
					FormatProgramDuration(program),
					Ui::Text::WithEntities),
				st::starrefCenteredText),
			st::boxRowPadding);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 4);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_star_ref_link_recipient(),
				st::starrefCenteredText));
		Ui::AddSkip(box->verticalLayout());
		box->addRow(object_ptr<Ui::AbstractButton>::fromRaw(
			MakePeerBubbleButton(box, peer).release()
		))->setAttribute(Qt::WA_TransparentForMouseEvents);

		struct State {
			QPointer<Ui::GenericBox> weak;
			bool sent = false;
		};
		const auto state = std::make_shared<State>();
		state->weak = box;

		const auto send = [=] {
			if (state->sent) {
				return;
			}
			state->sent = true;
			ConnectStarRef(bot->asUser(), peer, [=](ConnectedBot info) {
				show->show(StarRefLinkBox(info, peer));
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

std::unique_ptr<Ui::AbstractButton> MakePeerBubbleButton(
		not_null<QWidget*> parent,
		not_null<PeerData*> peer,
		Ui::RpWidget *right) {
	auto result = std::make_unique<Ui::AbstractButton>(parent);

	const auto size = st::chatGiveawayPeerSize;
	const auto padding = st::chatGiveawayPeerPadding;

	const auto raw = result.get();

	const auto width = raw->lifetime().make_state<int>();
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
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(left, 0, *width, size, skip, skip);
	}, raw->lifetime());

	return result;
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