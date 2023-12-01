/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/gift_premium_box.h"

#include "api/api_premium.h"
#include "api/api_premium_option.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "data/data_boosts.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_media_types.h" // Data::Giveaway
#include "data/data_peer_values.h" // Data::PeerPremiumValue.
#include "data/data_session.h"
#include "data/data_subscription_option.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "settings/settings_premium.h"
#include "ui/basic_click_handlers.h" // UrlClickHandler::Open.
#include "ui/boxes/boost_box.h" // StartFireworks.
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/table_layout.h"
#include "window/window_peer_menu.h" // ShowChooseRecipientBox.
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

#include <QtGui/QGuiApplication>

namespace {

constexpr auto kDiscountDivider = 5.;

using GiftOption = Data::SubscriptionOption;
using GiftOptions = Data::SubscriptionOptions;

GiftOptions GiftOptionFromTL(const MTPDuserFull &data) {
	auto result = GiftOptions();
	const auto gifts = data.vpremium_gifts();
	if (!gifts) {
		return result;
	}
	result = Api::SubscriptionOptionsFromTL(gifts->v);
	for (auto &option : result) {
		option.costPerMonth = tr::lng_premium_gift_per(
			tr::now,
			lt_cost,
			option.costPerMonth);
	}
	return result;
}

void GiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		GiftOptions options) {
	const auto boxWidth = st::boxWideWidth;
	box->setWidth(boxWidth);
	box->setNoContentMargin(true);
	const auto buttonsParent = box->verticalLayout().get();

	struct State {
		rpl::event_stream<QString> buttonText;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto userpicPadding = st::premiumGiftUserpicPadding;
	const auto top = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		buttonsParent,
		userpicPadding.top()
			+ userpicPadding.bottom()
			+ st::defaultUserpicButton.size.height()));

	using ColoredMiniStars = Ui::Premium::ColoredMiniStars;
	const auto stars = box->lifetime().make_state<ColoredMiniStars>(top, true);

	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		top,
		user,
		st::defaultUserpicButton);
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	top->widthValue(
	) | rpl::start_with_next([=](int width) {
		userpic->moveToLeft(
			(width - userpic->width()) / 2,
			userpicPadding.top());

		const auto center = top->rect().center();
		const auto size = QSize(
			userpic->width() * Ui::Premium::MiniStars::kSizeFactor,
			userpic->height());
		const auto ministarsRect = QRect(
			QPoint(center.x() - size.width(), center.y() - size.height()),
			QPoint(center.x() + size.width(), center.y() + size.height()));
		stars->setPosition(ministarsRect.topLeft());
		stars->setSize(ministarsRect.size());
	}, userpic->lifetime());

	top->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(top);

		p.fillRect(r, Qt::transparent);
		stars->paint(p);
	}, top->lifetime());

	const auto close = Ui::CreateChild<Ui::IconButton>(
		buttonsParent,
		st::infoTopBarClose);
	close->setClickedCallback([=] { box->closeBox(); });

	buttonsParent->widthValue(
	) | rpl::start_with_next([=](int width) {
		close->moveToRight(0, 0, width);
	}, close->lifetime());

	// Header.
	const auto &padding = st::premiumGiftAboutPadding;
	const auto available = boxWidth - padding.left() - padding.right();
	const auto &stTitle = st::premiumPreviewAboutTitle;
	auto titleLabel = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_premium_gift_title(),
		stTitle);
	titleLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			box,
			std::move(titleLabel)),
		st::premiumGiftTitlePadding);

	auto textLabel = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_premium_gift_about(
			lt_user,
			user->session().changes().peerFlagsValue(
				user,
				Data::PeerUpdate::Flag::Name
			) | rpl::map([=] { return TextWithEntities{ user->firstName }; }),
			Ui::Text::RichLangValue),
		st::premiumPreviewAbout);
	textLabel->setTextColorOverride(stTitle.textFg->c);
	textLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(box, std::move(textLabel)),
		padding);

	// List.
	const auto group = std::make_shared<Ui::RadiobuttonGroup>();
	const auto groupValueChangedCallback = [=](int value) {
		Expects(value < options.size() && value >= 0);
		auto text = tr::lng_premium_gift_button(
			tr::now,
			lt_cost,
			options[value].costTotal);
		state->buttonText.fire(std::move(text));
	};
	group->setChangedCallback(groupValueChangedCallback);
	Ui::Premium::AddGiftOptions(
		buttonsParent,
		group,
		options,
		st::premiumGiftOption);

	// Footer.
	auto terms = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_premium_gift_terms(
			lt_link,
			tr::lng_premium_gift_terms_link(
			) | rpl::map([=](const QString &t) {
				return Ui::Text::Link(t, 1);
			}),
			Ui::Text::WithEntities),
		st::premiumGiftTerms);
	terms->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		box->closeBox();
		Settings::ShowPremium(&user->session(), QString());
	}));
	terms->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(box, std::move(terms)),
		st::premiumGiftTermsPadding);

	// Button.
	const auto &stButton = st::premiumGiftBox;
	box->setStyle(stButton);
	auto raw = Settings::CreateSubscribeButton({
		controller,
		box,
		[] { return QString("gift"); },
		state->buttonText.events(),
		Ui::Premium::GiftGradientStops(),
		[=] {
			const auto value = group->value();
			return (value < options.size() && value >= 0)
				? options[value].botUrl
				: QString();
		},
	});
	auto button = object_ptr<Ui::GradientButton>::fromRaw(raw);
	button->resizeToWidth(boxWidth
		- stButton.buttonPadding.left()
		- stButton.buttonPadding.right());
	box->setShowFinishedCallback([raw = button.data()]{
		raw->startGlareAnimation();
	});
	box->addButton(std::move(button));

	groupValueChangedCallback(0);

	Data::PeerPremiumValue(
		user
	) | rpl::skip(1) | rpl::start_with_next([=] {
		box->closeBox();
	}, box->lifetime());
}

[[nodiscard]] Data::GiftCodeLink MakeGiftCodeLink(
		not_null<Main::Session*> session,
		const QString &slug) {
	const auto path = u"giftcode/"_q + slug;
	return {
		session->createInternalLink(path),
		session->createInternalLinkFull(path),
	};
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeLinkCopyIcon(
		not_null<QWidget*> parent) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();

	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto &icon = st::giveawayGiftCodeLinkCopy;
		const auto left = (raw->width() - icon.width()) / 2;
		const auto top = (raw->height() - icon.height()) / 2;
		icon.paint(p, left, top, raw->width());
	}, raw->lifetime());

	raw->resize(
		st::giveawayGiftCodeLinkCopyWidth,
		st::giveawayGiftCodeLinkHeight);

	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	return result;
}

[[nodiscard]] tr::phrase<lngtag_count> GiftDurationPhrase(int months) {
	return (months < 12)
		? tr::lng_premium_gift_duration_months
		: tr::lng_premium_gift_duration_years;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePeerTableValue(
		not_null<QWidget*> parent,
		not_null<Window::SessionNavigation*> controller,
		PeerId id) {
	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	const auto &st = st::giveawayGiftCodeUserpic;
	raw->resize(raw->width(), st.photoSize);

	const auto peer = controller->session().data().peer(id);
	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(raw, peer, st);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		peer->name(),
		st::giveawayGiftCodeValue);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto position = st::giveawayGiftCodeNamePosition;
		label->resizeToNaturalWidth(width - position.x());
		label->moveToLeft(position.x(), position.y(), width);
		const auto top = (raw->height() - userpic->height()) / 2;
		userpic->moveToLeft(0, top, width);
	}, label->lifetime());

	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setTextColorOverride(st::windowActiveTextFg->c);

	raw->setClickedCallback([=] {
		controller->uiShow()->showBox(PrepareShortInfoBox(peer, controller));
	});

	return result;
}

void AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		object_ptr<Ui::RpWidget> value,
		style::margins valueMargins) {
	table->addRow(
		object_ptr<Ui::FlatLabel>(
			table,
			std::move(label),
			st::giveawayGiftCodeLabel),
		std::move(value),
		st::giveawayGiftCodeLabelMargin,
		valueMargins);
}

not_null<Ui::FlatLabel*> AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		rpl::producer<TextWithEntities> value) {
	auto widget = object_ptr<Ui::FlatLabel>(
		table,
		std::move(value),
		st::giveawayGiftCodeValue);
	const auto result = widget.data();
	AddTableRow(
		table,
		std::move(label),
		std::move(widget),
		st::giveawayGiftCodeValueMargin);
	return result;
}

void AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		not_null<Window::SessionNavigation*> controller,
		PeerId id) {
	if (!id) {
		return;
	}
	AddTableRow(
		table,
		std::move(label),
		MakePeerTableValue(table, controller, id),
		st::giveawayGiftCodePeerMargin);
}

void AddTable(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionNavigation*> controller,
		const Api::GiftCode &current,
		bool skipReason) {
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	AddTableRow(
		table,
		tr::lng_gift_link_label_from(),
		controller,
		current.from);
	if (current.to) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_to(),
			controller,
			current.to);
	} else {
		AddTableRow(
			table,
			tr::lng_gift_link_label_to(),
			tr::lng_gift_link_label_to_unclaimed(Ui::Text::WithEntities));
	}
	AddTableRow(
		table,
		tr::lng_gift_link_label_gift(),
		tr::lng_gift_link_gift_premium(
			lt_duration,
			GiftDurationValue(current.months) | Ui::Text::ToWithEntities(),
			Ui::Text::WithEntities));
	if (!skipReason) {
		const auto reason = AddTableRow(
			table,
			tr::lng_gift_link_label_reason(),
			(current.giveawayId
				? ((current.to
					? tr::lng_gift_link_reason_giveaway
					: tr::lng_gift_link_reason_unclaimed)(
						) | Ui::Text::ToLink())
				: current.giveaway
				? ((current.to
					? tr::lng_gift_link_reason_giveaway
					: tr::lng_gift_link_reason_unclaimed)(
						Ui::Text::WithEntities
					) | rpl::type_erased())
				: tr::lng_gift_link_reason_chosen(Ui::Text::WithEntities)));
		reason->setClickHandlerFilter([=](const auto &...) {
			controller->showPeerHistory(
				current.from,
				Window::SectionShow::Way::Forward,
				current.giveawayId);
			return false;
		});
	}
	if (current.date) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_date(),
			rpl::single(Ui::Text::WithEntities(
				langDateTime(base::unixtime::parse(current.date)))));
	}
}

} // namespace

GiftPremiumValidator::GiftPremiumValidator(
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _api(&_controller->session().mtp()) {
}

void GiftPremiumValidator::cancel() {
	_requestId = 0;
}

void GiftPremiumValidator::showBox(not_null<UserData*> user) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPusers_GetFullUser(
		user->inputUser
	)).done([=](const MTPusers_UserFull &result) {
		if (!_requestId) {
			// Canceled.
			return;
		}
		_requestId = 0;
//		_controller->api().processFullPeer(peer, result);
		_controller->session().data().processUsers(result.data().vusers());
		_controller->session().data().processChats(result.data().vchats());

		const auto &fullUser = result.data().vfull_user().data();
		auto options = GiftOptionFromTL(fullUser);
		if (!options.empty()) {
			_controller->show(
				Box(GiftBox, _controller, user, std::move(options)));
		}
	}).fail([=] {
		_requestId = 0;
	}).send();
}

rpl::producer<QString> GiftDurationValue(int months) {
	return GiftDurationPhrase(months)(
		lt_count,
		rpl::single(float64((months < 12) ? months : (months / 12))));
}

QString GiftDuration(int months) {
	return GiftDurationPhrase(months)(
		tr::now,
		lt_count,
		(months < 12) ? months : (months / 12));
}

void GiftCodeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> controller,
		const QString &slug) {
	struct State {
		rpl::variable<Api::GiftCode> data;
		rpl::variable<bool> used;
		bool sent = false;
	};
	const auto session = &controller->session();
	const auto state = box->lifetime().make_state<State>(State{});
	state->data = session->api().premium().giftCodeValue(slug);
	state->used = state->data.value(
	) | rpl::map([=](const Api::GiftCode &data) {
		return data.used;
	});

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto bar = box->setPinnedToTopContent(
		object_ptr<Ui::Premium::TopBar>(
			box,
			st::giveawayGiftCodeCover,
			nullptr,
			rpl::conditional(
				state->used.value(),
				tr::lng_gift_link_used_title(),
				tr::lng_gift_link_title()),
			rpl::conditional(
				state->used.value(),
				tr::lng_gift_link_used_about(Ui::Text::RichLangValue),
				tr::lng_gift_link_about(Ui::Text::RichLangValue)),
			true));

	const auto max = st::giveawayGiftCodeTopHeight;
	bar->setMaximumHeight(max);
	bar->setMinimumHeight(st::infoLayerTopBarHeight);

	bar->resize(bar->width(), bar->maximumHeight());

	const auto link = MakeGiftCodeLink(&controller->session(), slug);
	box->addRow(
		Ui::MakeLinkLabel(
			box,
			rpl::single(link.text),
			rpl::single(link.link),
			box->uiShow(),
			MakeLinkCopyIcon(box)),
		st::giveawayGiftCodeLinkMargin);

	AddTable(box->verticalLayout(), controller, state->data.current(), false);

	auto shareLink = tr::lng_gift_link_also_send_link(
	) | rpl::map([](const QString &text) {
		return Ui::Text::Link(text);
	});
	auto richDate = [](const Api::GiftCode &data) {
		return TextWithEntities{
			langDateTime(base::unixtime::parse(data.used)),
		};
	};
	const auto footer = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::conditional(
				state->used.value(),
				tr::lng_gift_link_used_footer(
					lt_date,
					state->data.value() | rpl::map(richDate),
					Ui::Text::WithEntities),
				tr::lng_gift_link_also_send(
					lt_link,
					std::move(shareLink),
					Ui::Text::WithEntities)),
			st::giveawayGiftCodeFooter),
		st::giveawayGiftCodeFooterMargin);
	footer->setClickHandlerFilter([=](const auto &...) {
		const auto chosen = [=](not_null<Data::Thread*> thread) {
			const auto content = controller->parentController()->content();
			return content->shareUrl(
				thread,
				MakeGiftCodeLink(&controller->session(), slug).link,
				QString());
		};
		Window::ShowChooseRecipientBox(controller, chosen);
		return false;
	});

	const auto close = Ui::CreateChild<Ui::IconButton>(
		box.get(),
		st::boxTitleClose);
	close->setClickedCallback([=] {
		box->closeBox();
	});
	box->widthValue(
	) | rpl::start_with_next([=](int width) {
		close->moveToRight(0, 0);
	}, box->lifetime());

	const auto button = box->addButton(rpl::conditional(
		state->used.value(),
		tr::lng_box_ok(),
		tr::lng_gift_link_use()
	), [=] {
		if (state->used.current()) {
			box->closeBox();
		} else if (!state->sent) {
			state->sent = true;
			const auto done = crl::guard(box, [=](const QString &error) {
				if (error.isEmpty()) {
					auto copy = state->data.current();
					copy.used = base::unixtime::now();
					state->data = std::move(copy);

					Ui::StartFireworks(box->parentWidget());
				} else {
					box->uiShow()->showToast(error);
				}
			});
			controller->session().api().premium().applyGiftCode(slug, done);
		}
	});
	const auto buttonPadding = st::giveawayGiftCodeBox.buttonPadding;
	const auto buttonWidth = st::boxWideWidth
		- buttonPadding.left()
		- buttonPadding.right();
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}


void GiftCodePendingBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> controller,
		const Api::GiftCode &data) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	{
		const auto peerTo = controller->session().data().peer(data.to);
		const auto clickContext = [=, weak = base::make_weak(controller)] {
			if (const auto strong = weak.get()) {
				strong->uiShow()->showBox(
					PrepareShortInfoBox(peerTo, strong));
			}
			return QVariant();
		};
		const auto &st = st::giveawayGiftCodeCover;
		const auto resultToName = st.about.style.font->elided(
			peerTo->shortName(),
			st.about.minWidth / 2,
			Qt::ElideMiddle);
		const auto bar = box->setPinnedToTopContent(
			object_ptr<Ui::Premium::TopBar>(
				box,
				st,
				clickContext,
				tr::lng_gift_link_title(),
				tr::lng_gift_link_pending_about(
					lt_user,
					rpl::single(Ui::Text::Link(resultToName)),
					Ui::Text::RichLangValue),
				true));

		const auto max = st::giveawayGiftCodeTopHeight;
		bar->setMaximumHeight(max);
		bar->setMinimumHeight(st::infoLayerTopBarHeight);

		bar->resize(bar->width(), bar->maximumHeight());
	}

	{
		const auto linkLabel = box->addRow(
			Ui::MakeLinkLabel(box, nullptr, nullptr, nullptr, nullptr),
			st::giveawayGiftCodeLinkMargin);
		const auto spoiler = Ui::CreateChild<Ui::AbstractButton>(linkLabel);
		spoiler->lifetime().make_state<Ui::Animations::Basic>([=] {
			spoiler->update();
		})->start();
		linkLabel->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			spoiler->setGeometry(Rect(s));
		}, spoiler->lifetime());
		const auto spoilerCached = Ui::SpoilerMessCached(
			Ui::DefaultTextSpoilerMask(),
			st::giveawayGiftCodeLink.textFg->c);
		const auto textHeight = st::giveawayGiftCodeLink.style.font->height;
		spoiler->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(spoiler);
			const auto rect = spoiler->rect();
			const auto r = rect
				- QMargins(
					st::boxRowPadding.left(),
					(rect.height() - textHeight) / 2,
					st::boxRowPadding.right(),
					(rect.height() - textHeight) / 2);
			Ui::FillSpoilerRect(p, r, spoilerCached.frame());
		}, spoiler->lifetime());
		spoiler->setClickedCallback([show = box->uiShow()] {
			show->showToast(tr::lng_gift_link_pending_toast(tr::now));
		});
		spoiler->show();
	}

	AddTable(box->verticalLayout(), controller, data, true);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_link_pending_footer(),
			st::giveawayGiftCodeFooter),
		st::giveawayGiftCodeFooterMargin);

	const auto close = Ui::CreateChild<Ui::IconButton>(
		box.get(),
		st::boxTitleClose);
	const auto closeCallback = [=] { box->closeBox(); };
	close->setClickedCallback(closeCallback);
	box->widthValue(
	) | rpl::start_with_next([=](int width) {
		close->moveToRight(0, 0);
	}, box->lifetime());

	const auto button = box->addButton(tr::lng_close(), closeCallback);
	const auto buttonPadding = st::giveawayGiftCodeBox.buttonPadding;
	const auto buttonWidth = st::boxWideWidth
		- buttonPadding.left()
		- buttonPadding.right();
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}

void ResolveGiftCode(
		not_null<Window::SessionNavigation*> controller,
		const QString &slug) {
	const auto done = [=](Api::GiftCode code) {
		if (!code) {
			controller->showToast(tr::lng_gift_link_expired(tr::now));
		} else {
			controller->uiShow()->showBox(Box(GiftCodeBox, controller, slug));
		}
	};
	controller->session().api().premium().checkGiftCode(
		slug,
		crl::guard(controller, done));
}

void GiveawayInfoBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> controller,
		Data::Giveaway giveaway,
		Api::GiveawayInfo info) {
	using State = Api::GiveawayState;
	const auto finished = (info.state == State::Finished)
		|| (info.state == State::Refunded);

	box->setTitle((finished
		? tr::lng_prizes_end_title
		: tr::lng_prizes_how_title)());

	const auto first = !giveaway.channels.empty()
		? giveaway.channels.front()->name()
		: u"channel"_q;
	auto text = (finished
		? tr::lng_prizes_end_text
		: tr::lng_prizes_how_text)(
			tr::now,
			lt_admins,
			tr::lng_prizes_admins(
				tr::now,
				lt_count,
				giveaway.quantity,
				lt_channel,
				Ui::Text::Bold(first),
				lt_duration,
				TextWithEntities{ GiftDuration(giveaway.months) },
				Ui::Text::RichLangValue),
			Ui::Text::RichLangValue);
	const auto many = (giveaway.channels.size() > 1);
	const auto count = info.winnersCount
		? info.winnersCount
		: giveaway.quantity;
	auto winners = giveaway.all
		? (many
			? tr::lng_prizes_winners_all_of_many
			: tr::lng_prizes_winners_all_of_one)(
				tr::now,
				lt_count,
				count,
				lt_channel,
				Ui::Text::Bold(first),
				Ui::Text::RichLangValue)
		: (many
			? tr::lng_prizes_winners_new_of_many
			: tr::lng_prizes_winners_new_of_one)(
				tr::now,
				lt_count,
				count,
				lt_channel,
				Ui::Text::Bold(first),
				lt_start_date,
				Ui::Text::Bold(
					langDateTime(base::unixtime::parse(info.startDate))),
				Ui::Text::RichLangValue);
	text.append("\n\n").append((finished
		? tr::lng_prizes_end_when_finish
		: tr::lng_prizes_how_when_finish)(
			tr::now,
			lt_date,
			Ui::Text::Bold(langDayOfMonthFull(
				base::unixtime::parse(giveaway.untilDate).date())),
			lt_winners,
			winners,
			Ui::Text::RichLangValue));
	if (info.activatedCount > 0) {
		text.append(' ').append(tr::lng_prizes_end_activated(
			tr::now,
			lt_count,
			info.activatedCount,
			Ui::Text::RichLangValue));
	}
	if (!info.giftCode.isEmpty()) {
		text.append("\n\n");
		text.append(tr::lng_prizes_you_won(
			tr::now,
			lt_cup,
			QString::fromUtf8("\xf0\x9f\x8f\x86")));
	} else if (info.state == State::Finished) {
		text.append("\n\n");
		text.append(tr::lng_prizes_you_didnt(tr::now));
	} else if (info.state == State::Preparing) {

	} else if (info.state != State::Refunded) {
		if (info.adminChannelId) {
			const auto channel = controller->session().data().channel(
				info.adminChannelId);
			text.append("\n\n").append(tr::lng_prizes_how_no_admin(
				tr::now,
				lt_channel,
				Ui::Text::Bold(channel->name()),
				Ui::Text::RichLangValue));
		} else if (info.tooEarlyDate) {
			text.append("\n\n").append(tr::lng_prizes_how_no_joined(
				tr::now,
				lt_date,
				Ui::Text::Bold(
					langDateTime(base::unixtime::parse(info.tooEarlyDate))),
				Ui::Text::RichLangValue));
		} else if (!info.disallowedCountry.isEmpty()) {
			text.append("\n\n").append(tr::lng_prizes_how_no_country(
				tr::now,
				Ui::Text::RichLangValue));
		} else if (info.participating) {
			text.append("\n\n").append((many
				? tr::lng_prizes_how_yes_joined_many
				: tr::lng_prizes_how_yes_joined_one)(
					tr::now,
					lt_channel,
					Ui::Text::Bold(first),
					Ui::Text::RichLangValue));
		} else {
			text.append("\n\n").append((many
				? tr::lng_prizes_how_participate_many
				: tr::lng_prizes_how_participate_one)(
					tr::now,
					lt_channel,
					Ui::Text::Bold(first),
					lt_date,
					Ui::Text::Bold(langDayOfMonthFull(
						base::unixtime::parse(giveaway.untilDate).date())),
					Ui::Text::RichLangValue));
		}
	}
	const auto padding = st::boxPadding;
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			rpl::single(std::move(text)),
			st::boxLabel),
		{ padding.left(), 0, padding.right(), padding.bottom() });

	if (info.state == State::Refunded) {
		const auto wrap = box->addRow(
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				box.get(),
				object_ptr<Ui::FlatLabel>(
					box.get(),
					tr::lng_prizes_cancelled(),
					st::giveawayRefundedLabel),
				st::giveawayRefundedPadding),
			{ padding.left(), 0, padding.right(), padding.bottom() });
		const auto bg = wrap->lifetime().make_state<Ui::RoundRect>(
			st::boxRadius,
			st::attentionBoxButton.textBgOver);
		wrap->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(wrap);
			bg->paint(p, wrap->rect());
		}, wrap->lifetime());
	}
	if (const auto slug = info.giftCode; !slug.isEmpty()) {
		box->addButton(tr::lng_prizes_view_prize(), [=] {
			ResolveGiftCode(controller, slug);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	} else {
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}
}

void ResolveGiveawayInfo(
		not_null<Window::SessionNavigation*> controller,
		not_null<PeerData*> peer,
		MsgId messageId,
		Data::Giveaway giveaway) {
	const auto show = [=](Api::GiveawayInfo info) {
		if (!info) {
			controller->showToast(
				tr::lng_confirm_phone_link_invalid(tr::now));
		} else {
			controller->uiShow()->showBox(
				Box(GiveawayInfoBox, controller, giveaway, info));
		}
	};
	controller->session().api().premium().resolveGiveawayInfo(
		peer,
		messageId,
		crl::guard(controller, show));
}
