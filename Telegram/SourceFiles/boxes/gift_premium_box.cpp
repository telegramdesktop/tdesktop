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
#include "boxes/peer_list_controllers.h" // ContactsBoxController.
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/peers/replace_boost_box.h" // BoostsForGift.
#include "boxes/premium_preview_box.h" // ShowPremiumPreviewBox.
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_boosts.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_media_types.h" // Data::GiveawayStart.
#include "data/data_peer_values.h" // Data::PeerPremiumValue.
#include "data/data_session.h"
#include "data/data_subscription_option.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_form.h"
#include "settings/settings_premium.h"
#include "ui/basic_click_handlers.h" // UrlClickHandler::Open.
#include "ui/boxes/boost_box.h" // StartFireworks.
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
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

constexpr auto kUserpicsMax = size_t(3);

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

[[nodiscard]] Fn<TextWithEntities(TextWithEntities)> BoostsForGiftText(
		const std::vector<not_null<UserData*>> users) {
	Expects(!users.empty());

	const auto session = &users.front()->session();
	const auto emoji = Ui::Text::SingleCustomEmoji(
		session->data().customEmojiManager().registerInternalEmoji(
			st::premiumGiftsBoostIcon,
			QMargins(0, st::premiumGiftsUserpicBadgeInner, 0, 0),
			false));

	return [=, count = users.size()](TextWithEntities text) {
		text.append('\n');
		text.append('\n');
		text.append(tr::lng_premium_gifts_about_reward(
			tr::now,
			lt_count,
			count * BoostsForGift(session),
			lt_emoji,
			emoji,
			Ui::Text::RichLangValue));
		return text;
	};
}

using TagUser1 = lngtag_user;
using TagUser2 = lngtag_second_user;
using TagUser3 = lngtag_name;
[[nodiscard]] rpl::producer<TextWithEntities> ComplexAboutLabel(
		const std::vector<not_null<UserData*>> &users,
		tr::phrase<TagUser1> phrase1,
		tr::phrase<TagUser1, TagUser2> phrase2,
		tr::phrase<TagUser1, TagUser2, TagUser3> phrase3,
		tr::phrase<lngtag_count, TagUser1, TagUser2, TagUser3> phraseMore) {
	Expects(!users.empty());

	const auto count = users.size();
	const auto nameValue = [&](not_null<UserData*> user) {
		return user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::Name
		) | rpl::map([=] { return TextWithEntities{ user->firstName }; });
	};
	if (count == 1) {
		return phrase1(
			lt_user,
			nameValue(users.front()),
			Ui::Text::RichLangValue);
	} else if (count == 2) {
		return phrase2(
			lt_user,
			nameValue(users.front()),
			lt_second_user,
			nameValue(users[1]),
			Ui::Text::RichLangValue);
	} else if (count == 3) {
		return phrase3(
			lt_user,
			nameValue(users.front()),
			lt_second_user,
			nameValue(users[1]),
			lt_name,
			nameValue(users[2]),
			Ui::Text::RichLangValue);
	} else {
		return phraseMore(
			lt_count,
			rpl::single(count - kUserpicsMax) | tr::to_count(),
			lt_user,
			nameValue(users.front()),
			lt_second_user,
			nameValue(users[1]),
			lt_name,
			nameValue(users[2]),
			Ui::Text::RichLangValue);
	}
}

[[nodiscard]] not_null<Ui::RpWidget*> CircleBadge(
		not_null<Ui::RpWidget*> parent,
		const QString &text) {
	const auto widget = Ui::CreateChild<Ui::RpWidget>(parent.get());

	const auto full = Rect(st::premiumGiftsUserpicBadgeSize);
	const auto inner = full - Margins(st::premiumGiftsUserpicBadgeInner);
	auto gradient = QLinearGradient(
		QPointF(0, full.height()),
		QPointF(full.width(), 0));
	gradient.setStops(Ui::Premium::GiftGradientStops());

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawEllipse(full);
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawEllipse(inner);
		p.setFont(st::premiumGiftsUserpicBadgeFont);
		p.setPen(st::premiumButtonFg);
		p.drawText(full, text, style::al_center);
	}, widget->lifetime());
	widget->resize(full.size());
	return widget;
}

[[nodiscard]] not_null<Ui::RpWidget*> UserpicsContainer(
		not_null<Ui::RpWidget*> parent,
		std::vector<not_null<UserData*>> users) {
	Expects(!users.empty());

	if (users.size() == 1) {
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			parent.get(),
			users.front(),
			st::defaultUserpicButton);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		return userpic;
	}

	const auto &singleSize = st::defaultUserpicButton.size;

	const auto container = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto single = singleSize.width();
	const auto shift = single - st::boostReplaceUserpicsShift;
	const auto maxWidth = users.size() * (single - shift) + shift;
	container->resize(maxWidth, singleSize.height());
	container->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto diff = (single - st::premiumGiftsUserpicButton.size.width())
		/ 2;
	for (auto i = 0; i < users.size(); i++) {
		const auto bg = Ui::CreateChild<Ui::RpWidget>(container);
		bg->resize(singleSize);
		bg->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(bg);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxBg);
			p.drawEllipse(bg->rect());
		}, bg->lifetime());
		bg->moveToLeft(std::max(0, i * (single - shift)), 0);

		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			bg,
			users[i],
			st::premiumGiftsUserpicButton);
		userpic->moveToLeft(diff, diff);
	}

	return container;
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
	const auto stars = box->lifetime().make_state<ColoredMiniStars>(
		top,
		true);

	const auto userpic = UserpicsContainer(top, { user });
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

	auto textLabel = object_ptr<Ui::FlatLabel>(box, st::premiumPreviewAbout);
	tr::lng_premium_gift_about(
		lt_user,
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::Name
		) | rpl::map([=] { return TextWithEntities{ user->firstName }; }),
		Ui::Text::RichLangValue
	) | rpl::map(
		BoostsForGiftText({ user })
	) | rpl::start_with_next([
			raw = textLabel.data(),
			session = &user->session()](const TextWithEntities &t) {
		raw->setMarkedText(t, Core::MarkedTextContext{ .session = session });
	}, textLabel->lifetime());
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
		[] { return u"gift"_q; },
		state->buttonText.events(),
		Ui::Premium::GiftGradientStops(),
		[=] {
			const auto value = group->current();
			return (value < options.size() && value >= 0)
				? options[value].botUrl
				: QString();
		},
	});
	auto button = object_ptr<Ui::GradientButton>::fromRaw(raw);
	button->resizeToWidth(boxWidth - rect::m::sum::h(stButton.buttonPadding));
	box->setShowFinishedCallback([raw = button.data()] {
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

void GiftsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::vector<not_null<UserData*>> users,
		not_null<Api::PremiumGiftCodeOptions*> api,
		const QString &ref) {
	Expects(!users.empty());

	const auto boxWidth = st::boxWideWidth;
	box->setWidth(boxWidth);
	box->setNoContentMargin(true);
	const auto buttonsParent = box->verticalLayout().get();
	const auto session = &users.front()->session();

	struct State {
		rpl::event_stream<QString> buttonText;
		rpl::variable<bool> confirmButtonBusy = false;
		rpl::variable<bool> isPaymentComplete = false;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto userpicPadding = st::premiumGiftUserpicPadding;
	const auto top = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		buttonsParent,
		userpicPadding.top()
			+ userpicPadding.bottom()
			+ st::defaultUserpicButton.size.height()));

	using ColoredMiniStars = Ui::Premium::ColoredMiniStars;
	const auto stars = box->lifetime().make_state<ColoredMiniStars>(
		top,
		true);

	const auto maxWithUserpic = std::min(users.size(), kUserpicsMax);
	const auto userpics = UserpicsContainer(
		top,
		{ users.begin(), users.begin() + maxWithUserpic });
	top->widthValue(
	) | rpl::start_with_next([=](int width) {
		userpics->moveToLeft(
			(width - userpics->width()) / 2,
			userpicPadding.top());

		const auto center = top->rect().center();
		const auto size = QSize(
			userpics->width() * Ui::Premium::MiniStars::kSizeFactor,
			userpics->height());
		const auto ministarsRect = QRect(
			QPoint(center.x() - size.width(), center.y() - size.height()),
			QPoint(center.x() + size.width(), center.y() + size.height()));
		stars->setPosition(ministarsRect.topLeft());
		stars->setSize(ministarsRect.size());
	}, userpics->lifetime());
	if (const auto rest = users.size() - maxWithUserpic; rest > 0) {
		const auto badge = CircleBadge(
			userpics,
			QChar('+') + QString::number(rest));
		badge->moveToRight(0, userpics->height() - badge->height());
	}

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
		rpl::conditional(
			state->isPaymentComplete.value(),
			tr::lng_premium_gifts_about_paid_title(),
			tr::lng_premium_gift_title()),
		stTitle);
	titleLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			box,
			std::move(titleLabel)),
		st::premiumGiftTitlePadding);

	// About.
	{
		auto text = rpl::conditional(
			state->isPaymentComplete.value(),
			ComplexAboutLabel(
				users,
				tr::lng_premium_gifts_about_paid1,
				tr::lng_premium_gifts_about_paid2,
				tr::lng_premium_gifts_about_paid3,
				tr::lng_premium_gifts_about_paid_more
			) | rpl::map([count = users.size()](TextWithEntities text) {
				text.append('\n');
				text.append('\n');
				text.append(tr::lng_premium_gifts_about_paid_below(
					tr::now,
					lt_count,
					float64(count),
					Ui::Text::RichLangValue));
				return text;
			}),
			ComplexAboutLabel(
				users,
				tr::lng_premium_gifts_about_user1,
				tr::lng_premium_gifts_about_user2,
				tr::lng_premium_gifts_about_user3,
				tr::lng_premium_gifts_about_user_more
			) | rpl::map(BoostsForGiftText(users))
		);
		const auto label = box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(box, st::premiumPreviewAbout)),
			padding)->entity();
		std::move(
			text
		) | rpl::start_with_next([=](const TextWithEntities &t) {
			using namespace Core;
			label->setMarkedText(t, MarkedTextContext{ .session = session });
		}, label->lifetime());
		label->setTextColorOverride(stTitle.textFg->c);
		label->resizeToWidth(available);
	}

	// List.
	const auto optionsContainer = buttonsParent->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			buttonsParent,
			object_ptr<Ui::VerticalLayout>(buttonsParent)));
	const auto options = api->options(users.size());
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
		optionsContainer->entity(),
		group,
		options,
		st::premiumGiftOption);
	optionsContainer->toggleOn(
		state->isPaymentComplete.value() | rpl::map(!rpl::mappers::_1),
		anim::type::instant);

	// Summary.
	{
		{
			// Will be hidden after payment.
			const auto content = optionsContainer->entity();
			Ui::AddSkip(content);
			Ui::AddDivider(content);
			Ui::AddSkip(content);
			Ui::AddSubsectionTitle(
				content,
				tr::lng_premium_gifts_summary_subtitle());
		}
		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			{});
		auto buttonCallback = [=](PremiumFeature section) {
			stars->setPaused(true);
			const auto previewBoxShown = [=](
					not_null<Ui::BoxContent*> previewBox) {
				previewBox->boxClosing(
				) | rpl::start_with_next(crl::guard(box, [=] {
					stars->setPaused(false);
				}), previewBox->lifetime());
			};

			ShowPremiumPreviewBox(
				controller->uiShow(),
				section,
				previewBoxShown,
				true);
		};
		Settings::AddSummaryPremium(
			content,
			controller,
			ref,
			std::move(buttonCallback));
	}

	// Footer.
	{
		box->addRow(
			object_ptr<Ui::DividerLabel>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_premium_gifts_terms(
						lt_link,
						tr::lng_payments_terms_link(
						) | rpl::map([](const QString &t) {
							using namespace Ui::Text;
							return Link(t, u"https://telegram.org/tos"_q);
						}),
						lt_policy,
						tr::lng_premium_gifts_terms_policy(
						) | rpl::map([](const QString &t) {
							using namespace Ui::Text;
							return Link(t, u"https://telegram.org/privacy"_q);
						}),
						Ui::Text::RichLangValue),
					st::premiumGiftTerms),
				st::defaultBoxDividerLabelPadding),
			{});
	}

	// Button.
	const auto &stButton = st::premiumGiftBox;
	box->setStyle(stButton);
	auto raw = Settings::CreateSubscribeButton({
		controller,
		box,
		[=] { return ref; },
		rpl::combine(
			state->buttonText.events(),
			state->confirmButtonBusy.value(),
			state->isPaymentComplete.value()
		) | rpl::map([](const QString &text, bool busy, bool paid) {
			return busy
				? QString()
				: paid
				? tr::lng_close(tr::now)
				: text;
		}),
		Ui::Premium::GiftGradientStops(),
	});
	raw->setClickedCallback([=] {
		if (state->confirmButtonBusy.current()) {
			return;
		}
		if (state->isPaymentComplete.current()) {
			return box->closeBox();
		}
		auto invoice = api->invoice(
			users.size(),
			api->monthsFromPreset(group->current()));
		invoice.purpose = Payments::InvoicePremiumGiftCodeUsers{ users };

		state->confirmButtonBusy = true;
		const auto show = box->uiShow();
		const auto weak = Ui::MakeWeak(box.get());
		const auto done = [=](Payments::CheckoutResult result) {
			if (const auto strong = weak.data()) {
				strong->window()->setFocus();
				state->confirmButtonBusy = false;
				if (result == Payments::CheckoutResult::Paid) {
					state->isPaymentComplete = true;
					Ui::StartFireworks(box->parentWidget());
				}
			}
		};

		Payments::CheckoutProcess::Start(std::move(invoice), done);
	});
	{
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			raw,
			raw->height() / 2);
		AddChildToWidgetCenter(raw, loadingAnimation);
		loadingAnimation->showOn(state->confirmButtonBusy.value());
	}
	auto button = object_ptr<Ui::GradientButton>::fromRaw(raw);
	button->resizeToWidth(boxWidth - rect::m::sum::h(stButton.buttonPadding));
	box->setShowFinishedCallback([raw = button.data()] {
		raw->startGlareAnimation();
	});
	box->addButton(std::move(button));

	groupValueChangedCallback(0);
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
	if (current.from) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_from(),
			controller,
			current.from);
	}
	if (current.from && current.to) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_to(),
			controller,
			current.to);
	} else if (current.from) {
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
	if (!skipReason && current.from) {
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

void ShareWithFriend(
		not_null<Window::SessionNavigation*> navigation,
		const QString &slug) {
	const auto chosen = [=](not_null<Data::Thread*> thread) {
		const auto content = navigation->parentController()->content();
		return content->shareUrl(
			thread,
			MakeGiftCodeLink(&navigation->session(), slug).link,
			QString());
	};
	Window::ShowChooseRecipientBox(navigation, chosen);
}

void ShowAlreadyPremiumToast(
		not_null<Window::SessionNavigation*> navigation,
		const QString &slug,
		TimeId date) {
	const auto instance = std::make_shared<
		base::weak_ptr<Ui::Toast::Instance>
	>();
	const auto shareLink = [=](
			const ClickHandlerPtr &,
			Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			if (const auto strong = instance->get()) {
				strong->hideAnimated();
			}
			ShareWithFriend(navigation, slug);
		}
		return false;
	};
	*instance = navigation->showToast({
		.title = tr::lng_gift_link_already_title(tr::now),
		.text = tr::lng_gift_link_already_about(
			tr::now,
			lt_date,
			Ui::Text::Bold(langDateTime(base::unixtime::parse(date))),
			lt_link,
			Ui::Text::Link(
				Ui::Text::Bold(tr::lng_gift_link_already_link(tr::now))),
			Ui::Text::WithEntities),
		.duration = 6 * crl::time(1000),
		.filter = crl::guard(navigation, shareLink),
	});
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

void GiftPremiumValidator::showChoosePeerBox(const QString &ref) {
	if (_manyGiftsLifetime) {
		return;
	}
	using namespace Api;
	const auto api = _manyGiftsLifetime.make_state<PremiumGiftCodeOptions>(
		_controller->session().user());
	const auto show = _controller->uiShow();
	api->request(
	) | rpl::start_with_error_done([=](const QString &error) {
		show->showToast(error);
	}, [=] {
		const auto maxAmount = *ranges::max_element(api->availablePresets());

		class Controller final : public ContactsBoxController {
		public:
			Controller(
				not_null<Main::Session*> session,
				Fn<bool(int)> checkErrorCallback)
			: ContactsBoxController(session)
			, _checkErrorCallback(std::move(checkErrorCallback)) {
			}

		protected:
			std::unique_ptr<PeerListRow> createRow(
					not_null<UserData*> user) override {
				if (user->isSelf()
					|| user->isBot()
					|| user->isServiceUser()
					|| user->isInaccessible()) {
					return nullptr;
				}
				return ContactsBoxController::createRow(user);
			}

			void rowClicked(not_null<PeerListRow*> row) override {
				const auto checked = !row->checked();
				if (checked
					&& _checkErrorCallback
					&& _checkErrorCallback(
						delegate()->peerListSelectedRowsCount())) {
					return;
				}
				delegate()->peerListSetRowChecked(row, checked);
			}

		private:
			const Fn<bool(int)> _checkErrorCallback;

		};
		auto initBox = [=](not_null<PeerListBox*> peersBox) {
			const auto ignoreClose = peersBox->lifetime().make_state<bool>(0);

			auto process = [=] {
				const auto selected = peersBox->collectSelectedRows();
				const auto users = ranges::views::all(
					selected
				) | ranges::views::transform([](not_null<PeerData*> p) {
					return p->asUser();
				}) | ranges::views::filter([](UserData *u) -> bool {
					return u;
				}) | ranges::to<std::vector<not_null<UserData*>>>();
				if (!users.empty()) {
					const auto giftBox = show->show(
						Box(GiftsBox, _controller, users, api, ref));
					giftBox->boxClosing(
					) | rpl::start_with_next([=] {
						_manyGiftsLifetime.destroy();
					}, giftBox->lifetime());
				}
				(*ignoreClose) = true;
				peersBox->closeBox();
			};

			peersBox->setTitle(tr::lng_premium_gift_title());
			peersBox->addButton(
				tr::lng_settings_gift_premium_users_confirm(),
				std::move(process));
			peersBox->addButton(tr::lng_cancel(), [=] {
				peersBox->closeBox();
			});
			peersBox->boxClosing(
			) | rpl::start_with_next([=] {
				if (!(*ignoreClose)) {
					_manyGiftsLifetime.destroy();
				}
			}, peersBox->lifetime());
		};

		auto listController = std::make_unique<Controller>(
			&_controller->session(),
			[=](int count) {
				if (count <= maxAmount) {
					return false;
				}
				show->showToast(tr::lng_settings_gift_premium_users_error(
					tr::now,
					lt_count,
					maxAmount));
				return true;
			});
		show->showBox(
			Box<PeerListBox>(
				std::move(listController),
				std::move(initBox)),
			Ui::LayerOption::KeepOther);

	}, _manyGiftsLifetime);
}

void GiftPremiumValidator::showChosenPeerBox(
		not_null<UserData*> user,
		const QString &ref) {
	if (_manyGiftsLifetime) {
		return;
	}
	using namespace Api;
	const auto api = _manyGiftsLifetime.make_state<PremiumGiftCodeOptions>(
		_controller->session().user());
	const auto show = _controller->uiShow();
	api->request(
	) | rpl::start_with_error_done([=](const QString &error) {
		show->showToast(error);
	}, [=] {
		const auto users = std::vector<not_null<UserData*>>{ user };
		const auto giftBox = show->show(
			Box(GiftsBox, _controller, users, api, ref));
		giftBox->boxClosing(
		) | rpl::start_with_next([=] {
			_manyGiftsLifetime.destroy();
		}, giftBox->lifetime());
	}, _manyGiftsLifetime);
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
		return data.used != 0;
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
		ShareWithFriend(controller, slug);
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
				const auto activePrefix = u"PREMIUM_SUB_ACTIVE_UNTIL_"_q;
				if (error.isEmpty()) {
					auto copy = state->data.current();
					copy.used = base::unixtime::now();
					state->data = std::move(copy);

					Ui::StartFireworks(box->parentWidget());
				} else if (error.startsWith(activePrefix)) {
					const auto date = error.mid(activePrefix.size()).toInt();
					ShowAlreadyPremiumToast(controller, slug, date);
					state->sent = false;
				} else {
					box->uiShow()->showToast(error);
					state->sent = false;
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
		const QString &slug,
		PeerId fromId,
		PeerId toId) {
	const auto done = [=](Api::GiftCode code) {
		const auto session = &controller->session();
		const auto selfId = session->userPeerId();
		if (!code) {
			controller->showToast(tr::lng_gift_link_expired(tr::now));
		} else if (!code.from && fromId == selfId) {
			code.from = fromId;
			code.to = toId;
			const auto self = (fromId == selfId);
			const auto peer = session->data().peer(self ? toId : fromId);
			const auto months = code.months;
			const auto parent = controller->parentController();
			Settings::ShowGiftPremium(parent, peer, months, self);
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
		std::optional<Data::GiveawayStart> start,
		std::optional<Data::GiveawayResults> results,
		Api::GiveawayInfo info) {
	Expects(start || results);

	using State = Api::GiveawayState;
	const auto finished = (info.state == State::Finished)
		|| (info.state == State::Refunded);

	box->setTitle((finished
		? tr::lng_prizes_end_title
		: tr::lng_prizes_how_title)());

	const auto first = results
		? results->channel->name()
		: !start->channels.empty()
		? start->channels.front()->name()
		: u"channel"_q;
	auto text = TextWithEntities();

	if (!info.giftCode.isEmpty()) {
		text.append("\n\n");
		text.append(Ui::Text::Bold(tr::lng_prizes_you_won(
			tr::now,
			lt_cup,
			QString::fromUtf8("\xf0\x9f\x8f\x86"))));
		text.append("\n\n");
	} else if (info.state == State::Finished) {
		text.append("\n\n");
		text.append(Ui::Text::Bold(tr::lng_prizes_you_didnt(tr::now)));
		text.append("\n\n");
	}

	const auto quantity = start
		? start->quantity
		: (results->winnersCount + results->unclaimedCount);
	const auto months = start ? start->months : results->months;
	const auto group = results
		? results->channel->isMegagroup()
		: (!start->channels.empty()
			&& start->channels.front()->isMegagroup());
	text.append((finished
		? tr::lng_prizes_end_text
		: tr::lng_prizes_how_text)(
			tr::now,
			lt_admins,
			(group
				? tr::lng_prizes_admins_group
				: tr::lng_prizes_admins)(
					tr::now,
					lt_count,
					quantity,
					lt_channel,
					Ui::Text::Bold(first),
					lt_duration,
					TextWithEntities{ GiftDuration(months) },
					Ui::Text::RichLangValue),
			Ui::Text::RichLangValue));
	const auto many = start
		? (start->channels.size() > 1)
		: (results->additionalPeersCount > 0);
	const auto count = info.winnersCount
		? info.winnersCount
		: quantity;
	const auto all = start ? start->all : results->all;
	auto winners = all
		? (many
			? (group
				? tr::lng_prizes_winners_all_of_many_group
				: tr::lng_prizes_winners_all_of_many)
			: (group
				? tr::lng_prizes_winners_all_of_one_group
				: tr::lng_prizes_winners_all_of_one))(
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
	const auto additionalPrize = results
		? results->additionalPrize
		: start->additionalPrize;
	if (!additionalPrize.isEmpty()) {
		text.append("\n\n").append((group
			? tr::lng_prizes_additional_added_group
			: tr::lng_prizes_additional_added)(
				tr::now,
				lt_count,
				count,
				lt_channel,
				Ui::Text::Bold(first),
				lt_prize,
				TextWithEntities{ additionalPrize },
				Ui::Text::RichLangValue));
	}
	const auto untilDate = start
		? start->untilDate
		: results->untilDate;
	text.append("\n\n").append((finished
		? tr::lng_prizes_end_when_finish
		: tr::lng_prizes_how_when_finish)(
			tr::now,
			lt_date,
			Ui::Text::Bold(langDayOfMonthFull(
				base::unixtime::parse(untilDate).date())),
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
	if (!info.giftCode.isEmpty()
		|| info.state == State::Finished
		|| info.state == State::Preparing) {
	} else if (info.state != State::Refunded) {
		if (info.adminChannelId) {
			const auto channel = controller->session().data().channel(
				info.adminChannelId);
			text.append("\n\n").append((channel->isMegagroup()
				? tr::lng_prizes_how_no_admin_group
				: tr::lng_prizes_how_no_admin)(
					tr::now,
					lt_channel,
					Ui::Text::Bold(channel->name()),
					Ui::Text::RichLangValue));
		} else if (info.tooEarlyDate) {
			const auto channel = controller->session().data().channel(
				info.adminChannelId);
			text.append("\n\n").append((channel->isMegagroup()
				? tr::lng_prizes_how_no_joined_group
				: tr::lng_prizes_how_no_joined)(
					tr::now,
					lt_date,
					Ui::Text::Bold(
						langDateTime(
							base::unixtime::parse(info.tooEarlyDate))),
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
						base::unixtime::parse(untilDate).date())),
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
					(group
						? tr::lng_prizes_cancelled_group()
						: tr::lng_prizes_cancelled()),
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
		std::optional<Data::GiveawayStart> start,
		std::optional<Data::GiveawayResults> results) {
	const auto show = [=](Api::GiveawayInfo info) {
		if (!info) {
			controller->showToast(
				tr::lng_confirm_phone_link_invalid(tr::now));
		} else {
			controller->uiShow()->showBox(
				Box(GiveawayInfoBox, controller, start, results, info));
		}
	};
	controller->session().api().premium().resolveGiveawayInfo(
		peer,
		messageId,
		crl::guard(controller, show));
}
