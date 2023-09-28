/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/gift_premium_box.h"

#include "apiwrap.h"
#include "api/api_premium.h"
#include "api/api_premium_option.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h" // Data::PeerPremiumValue.
#include "data/data_session.h"
#include "data/data_subscription_option.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/basic_click_handlers.h" // UrlClickHandler::Open.
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/table_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
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
	const auto stars = box->lifetime().make_state<ColoredMiniStars>(top);

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

struct GiftCodeLink {
	QString text;
	QString link;
};
[[nodiscard]] GiftCodeLink MakeGiftCodeLink(
		not_null<Main::Session*> session,
		const QString &slug) {
	const auto path = u"giftcode/"_q + slug;
	return {
		session->createInternalLink(path),
		session->createInternalLinkFull(path),
	};
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeLinkLabel(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		rpl::producer<QString> link,
		std::shared_ptr<Ui::Show> show) {
	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	struct State {
		State(
			not_null<QWidget*> parent,
			rpl::producer<QString> value,
			rpl::producer<QString> link)
		: text(std::move(value))
		, link(std::move(link))
		, label(parent, text.value(), st::giveawayGiftCodeLink)
		, bg(st::roundRadiusLarge, st::windowBgOver) {
		}

		rpl::variable<QString> text;
		rpl::variable<QString> link;
		Ui::FlatLabel label;
		Ui::RoundRect bg;
	};

	const auto state = raw->lifetime().make_state<State>(
		raw,
		rpl::duplicate(text),
		std::move(link));
	state->label.setSelectable(true);

	rpl::combine(
		raw->widthValue(),
		std::move(text)
	) | rpl::start_with_next([=](int outer, const auto&) {
		const auto textWidth = state->label.textMaxWidth();
		const auto skipLeft = st::giveawayGiftCodeLink.margin.left();
		const auto skipRight = st::giveawayGiftCodeLinkCopyWidth;
		const auto available = outer - skipRight - skipLeft;
		const auto use = std::min(textWidth, available);
		state->label.resizeToWidth(use);
		const auto left = (outer >= 2 * skipRight + textWidth)
			? ((outer - textWidth) / 2)
			: (outer - skipRight - use - skipLeft);
		state->label.move(left, 0);
	}, raw->lifetime());

	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		state->bg.paint(p, raw->rect());
		const auto outer = raw->width();
		const auto width = st::giveawayGiftCodeLinkCopyWidth;
		const auto &icon = st::giveawayGiftCodeLinkCopy;
		const auto left = outer - width + (width - icon.width()) / 2;
		const auto top = (raw->height() - icon.height()) / 2;
		icon.paint(p, left, top, raw->width());
	}, raw->lifetime());

	state->label.setAttribute(Qt::WA_TransparentForMouseEvents);

	raw->resize(raw->width(), st::giveawayGiftCodeLinkHeight);
	raw->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(state->link.current());
		show->showToast(tr::lng_username_copied(tr::now));
	});

	return result;
}

[[nodiscard]] rpl::producer<QString> DurationValue(int months) {
	return (months < 12)
		? tr::lng_months(lt_count, rpl::single(float64(months)))
		: tr::lng_years(lt_count, rpl::single(float64(months / 12)));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePeerTableValue(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
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
		controller->show(PrepareShortInfoBox(peer, controller));
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

void AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		rpl::producer<QString> value) {
	AddTableRow(
		table,
		std::move(label),
		object_ptr<Ui::FlatLabel>(
			table,
			std::move(value),
			st::giveawayGiftCodeValue),
		st::giveawayGiftCodeValueMargin);
}

void AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		not_null<Window::SessionController*> controller,
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

void GiftCodeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const QString &slug) {
	struct State {
		rpl::variable<Api::GiftCode> data;
		rpl::variable<bool> used;
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
		MakeLinkLabel(
			box,
			rpl::single(link.text),
			rpl::single(link.link),
			box->uiShow()),
		st::giveawayGiftCodeLinkMargin);

	auto table = box->addRow(
		object_ptr<Ui::TableLayout>(
			box,
			st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	const auto current = state->data.current();
	AddTableRow(
		table,
		tr::lng_gift_link_label_from(),
		controller,
		current.from);
	AddTableRow(
		table,
		tr::lng_gift_link_label_to(),
		controller,
		current.to);
	AddTableRow(
		table,
		tr::lng_gift_link_label_gift(),
		tr::lng_gift_link_gift_premium(
			lt_duration,
			DurationValue(current.months)));
	AddTableRow(
		table,
		tr::lng_gift_link_label_reason(),
		tr::lng_gift_link_reason_giveaway());
	AddTableRow(
		table,
		tr::lng_gift_link_label_date(),
		rpl::single(langDateTime(base::unixtime::parse(current.date))));

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
		box->uiShow()->showToast(u"Sharing..."_q);
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
		} else {
			auto copy = state->data.current();
			copy.used = base::unixtime::now();
			state->data = std::move(copy);
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
