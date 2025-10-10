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
#include "base/event_filter.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "boxes/peer_list_controllers.h" // ContactsBoxController.
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/peers/replace_boost_box.h" // BoostsForGift.
#include "boxes/premium_preview_box.h" // ShowPremiumPreviewBox.
#include "boxes/star_gift_box.h" // ShowStarGiftBox.
#include "core/ui_integration.h"
#include "data/data_boosts.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_emoji_statuses.h"
#include "data/data_media_types.h" // Data::GiveawayStart.
#include "data/data_peer_values.h" // Data::PeerPremiumValue.
#include "data/data_session.h"
#include "data/data_premium_subscription_option.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_form.h"
#include "settings/settings_credits_graphics.h"
#include "settings/settings_premium.h"
#include "ui/basic_click_handlers.h" // UrlClickHandler::Open.
#include "ui/boxes/boost_box.h" // StartFireworks.
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "window/window_peer_menu.h" // ShowChooseRecipientBox.
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

#include <QtGui/QGuiApplication>

namespace {

constexpr auto kTooltipDuration = 3 * crl::time(1000);
constexpr auto kPriceTooltipDuration = 6 * crl::time(1000);
constexpr auto kHorizontalBar = QChar(0x2015);

struct InfoTooltipData {
	not_null<Ui::RpWidget*> parent;
	Ui::ImportantTooltip *raw = nullptr;
};

void ShowInfoTooltip(
		std::shared_ptr<InfoTooltipData> data,
		not_null<QWidget*> target,
		rpl::producer<TextWithEntities> text,
		int duration) {
	if (data->raw) {
		data->raw->toggleAnimated(false);
	}
	const auto parent = data->parent;
	const auto tooltip = Ui::CreateChild<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			std::move(text),
			st::boxWideWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	tooltip->toggleFast(false);

	base::install_event_filter(tooltip, qApp, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			tooltip->toggleAnimated(false);
		}
		return base::EventFilterResult::Continue;
	});

	const auto update = [=] {
		const auto geometry = Ui::MapFrom(parent, target, target->rect());
		const auto countPosition = [=](QSize size) {
			const auto left = geometry.x()
				+ (geometry.width() - size.width()) / 2;
			const auto right = parent->width()
				- st::normalFont->spacew;
			return QPoint(
				std::max(std::min(left, right - size.width()), 0),
				geometry.y() - size.height() - st::normalFont->descent);
		};
		tooltip->pointAt(geometry, RectPart::Top, countPosition);
	};
	parent->widthValue(
	) | rpl::start_with_next(update, tooltip->lifetime());

	update();
	tooltip->toggleAnimated(true);

	data->raw = tooltip;
	tooltip->shownValue() | rpl::filter(
		!rpl::mappers::_1
	) | rpl::start_with_next([=] {
		crl::on_main(tooltip, [=] {
			if (tooltip->isHidden()) {
				if (data->raw == tooltip) {
					data->raw = nullptr;
				}
				delete tooltip;
			}
		});
	}, tooltip->lifetime());

	base::timer_once(
		duration
	) | rpl::start_with_next([=] {
		tooltip->toggleAnimated(false);
	}, tooltip->lifetime());
}

[[nodiscard]] QString CreateMessageLink(
		not_null<Main::Session*> session,
		PeerId peerId,
		uint64 messageId) {
	if (const auto msgId = MsgId(peerId ? messageId : 0)) {
		const auto peer = session->data().peer(peerId);
		if (const auto channel = peer->asBroadcast()) {
			const auto username = channel->username();
			const auto base = username.isEmpty()
				? u"c/%1"_q.arg(peerToChannel(channel->id).bare)
				: username;
			const auto query = base + '/' + QString::number(msgId.bare);
			return session->createInternalLink(query);
		}
	}
	return QString();
};

[[nodiscard]] QString FixupTransactionId(QString origin) {
	return origin.replace(kHorizontalBar, QChar('-'));
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

[[nodiscard]] TextWithEntities FormatValuePrice(
		int64 price,
		QString currency,
		bool approximately = false) {
	auto result = TextWithEntities();
	if (approximately) {
		result.append('~');
	}
	return result.append(Ui::FillAmountAndCurrency(price, currency));
}

[[nodiscard]] TextWithEntities FormatValueDate(TimeId date) {
	const auto parsed = base::unixtime::parse(date).date();
	const auto day = parsed.day();
	const auto month = parsed.month();
	const auto year = parsed.year();
	return { tr::lng_month_day_year(
			tr::now,
			lt_month,
			Lang::MonthDay(month)(tr::now),
			lt_day,
			QString::number(day),
			lt_year,
			QString::number(year))
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

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeMaybeMultilineTokenValue(
		not_null<Ui::TableLayout*> table,
		QString token,
		Settings::CreditsEntryBoxStyleOverrides st) {
	constexpr auto kOneLineCount = 24;
	token = token.replace(QChar('-'), kHorizontalBar);
	const auto oneLine = token.length() <= kOneLineCount;
	return object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(
			Ui::Text::Wrapped({ token }, EntityType::Code, {})),
		(oneLine
			? table->st().defaultValue
			: st.tableValueMultiline
			? *st.tableValueMultiline
			: st::giveawayGiftCodeValueMultiline));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeValueWithSmallButton(
		not_null<Ui::TableLayout*> table,
		not_null<Ui::RpWidget*> value,
		rpl::producer<QString> buttonText,
		Fn<void(not_null<Ui::RpWidget*> button)> handler = nullptr,
		int topSkip = 0) {
	class MarginedWidget final : public Ui::RpWidget {
	public:
		using RpWidget::RpWidget;
		QMargins getMargins() const override {
			return { 0, 0, 0, st::giveawayGiftCodePeerMargin.bottom() };
		}
	};
	auto result = object_ptr<MarginedWidget>(table);
	const auto raw = result.data();

	value->setParent(raw);
	value->show();

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		raw,
		std::move(buttonText),
		table->st().smallButton);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	if (handler) {
		button->setClickedCallback([button, handler = std::move(handler)] {
			handler(button);
		});
	} else {
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	rpl::combine(
		raw->widthValue(),
		button->widthValue(),
		value->naturalWidthValue()
	) | rpl::start_with_next([=](int width, int buttonWidth, int) {
		const auto buttonSkip = st::normalFont->spacew + buttonWidth;
		value->resizeToNaturalWidth(width - buttonSkip);
		value->moveToLeft(0, 0, width);
		button->moveToLeft(
			rect::right(value) + st::normalFont->spacew,
			(topSkip
				+ (table->st().defaultValue.style.font->ascent
					- table->st().smallButton.style.font->ascent)),
			width);
	}, value->lifetime());

	value->heightValue() | rpl::start_with_next([=](int height) {
		const auto bottom = st::giveawayGiftCodePeerMargin.bottom();
		raw->resize(raw->width(), height + bottom);
	}, raw->lifetime());

	return result;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePeerTableValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id,
		rpl::producer<QString> button = nullptr,
		Fn<void()> handler = nullptr) {
	auto result = object_ptr<Ui::AbstractButton>(table);
	const auto raw = result.data();

	const auto &st = st::giveawayGiftCodeUserpic;
	raw->resize(raw->width(), st.photoSize);

	const auto peer = show->session().data().peer(id);
	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(raw, peer, st);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		(button && handler) ? peer->shortName() : peer->name(),
		table->st().defaultValue);

	raw->widthValue() | rpl::start_with_next([=](int width) {
		const auto position = st::giveawayGiftCodeNamePosition;
		label->resizeToNaturalWidth(width - position.x());
		label->moveToLeft(position.x(), position.y(), width);
		const auto top = (raw->height() - userpic->height()) / 2;
		userpic->moveToLeft(0, top, width);
	}, label->lifetime());

	label->naturalWidthValue() | rpl::start_with_next([=](int width) {
		raw->setNaturalWidth(st::giveawayGiftCodeNamePosition.x() + width);
	}, label->lifetime());
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setTextColorOverride(table->st().defaultValue.palette.linkFg->c);

	raw->setClickedCallback([=] {
		show->showBox(PrepareShortInfoBox(peer, show));
	});

	if (!button || !handler) {
		return result;
	}
	return MakeValueWithSmallButton(
		table,
		result.release(),
		std::move(button),
		[=](not_null<Ui::RpWidget*> button) { handler(); },
		st::giveawayGiftCodeNamePosition.y());
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePeerWithStatusValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id,
		Fn<void(not_null<Ui::RpWidget*>, EmojiStatusId)> pushStatusId) {
	auto result = object_ptr<Ui::RpWidget>(table);
	const auto raw = result.data();

	const auto peerLabel = MakePeerTableValue(table, show, id).release();
	peerLabel->setParent(raw);
	peerLabel->show();

	raw->resize(raw->width(), peerLabel->height());

	using namespace Info::Profile;
	struct State {
		rpl::variable<Badge::Content> content;
	};
	const auto peer = show->session().data().peer(id);
	const auto state = peerLabel->lifetime().make_state<State>();
	state->content = EmojiStatusIdValue(
		peer
	) | rpl::map([=](EmojiStatusId emojiStatusId) {
		if (!peer->session().premium()
			|| (!peer->isSelf() && !emojiStatusId)) {
			return Badge::Content();
		}
		return Badge::Content{
			.badge = BadgeType::Premium,
			.emojiStatusId = emojiStatusId,
		};
	});
	const auto badge = peerLabel->lifetime().make_state<Badge>(
		raw,
		st::infoPeerBadge,
		&peer->session(),
		state->content.value(),
		nullptr,
		[=] { return show->paused(ChatHelpers::PauseReason::Layer); });
	state->content.value(
	) | rpl::start_with_next([=](const Badge::Content &content) {
		if (const auto widget = badge->widget()) {
			pushStatusId(widget, content.emojiStatusId);
		}
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		rpl::single(rpl::empty) | rpl::then(badge->updated())
	) | rpl::start_with_next([=](int width, const auto &) {
		const auto badgeWidget = badge->widget();
		const auto badgeSkip = badgeWidget
			? (st::normalFont->spacew + badgeWidget->width())
			: 0;
		peerLabel->resizeToNaturalWidth(width - badgeSkip);
		peerLabel->moveToLeft(0, 0, width);
		if (badgeWidget) {
			badgeWidget->moveToLeft(
				peerLabel->width() + st::normalFont->spacew,
				st::giftBoxByStarsStarTop,
				width);
		}
	}, raw->lifetime());

	return result;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeHiddenPeerTableValue(
		not_null<Ui::TableLayout*> table) {
	auto result = object_ptr<Ui::RpWidget>(table);
	const auto raw = result.data();

	const auto &st = st::giveawayGiftCodeUserpic;
	raw->resize(raw->width(), st.photoSize);

	const auto userpic = Ui::CreateChild<Ui::RpWidget>(raw);
	const auto usize = st.photoSize;
	userpic->resize(usize, usize);
	userpic->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(userpic);
		Ui::EmptyUserpic::PaintHiddenAuthor(p, 0, 0, usize, usize);
	}, userpic->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		tr::lng_gift_from_hidden(),
		table->st().defaultValue);
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
	label->setTextColorOverride(st::windowFg->c);

	return result;
}

void AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		object_ptr<Ui::RpWidget> value,
		style::margins valueMargins = st::giveawayGiftCodeValueMargin) {
	table->addRow(
		(label
			? object_ptr<Ui::FlatLabel>(
				table,
				std::move(label),
				table->st().defaultLabel)
			: object_ptr<Ui::FlatLabel>(nullptr)),
		std::move(value),
		st::giveawayGiftCodeLabelMargin,
		valueMargins);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePriceWithChangePercentValue(
		not_null<Ui::TableLayout*> table,
		const std::shared_ptr<Data::UniqueGiftValue> &value) {
	auto label = object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(FormatValuePrice(value->lastSalePrice, value->currency)),
		table->st().defaultValue);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto initial = value->initialSalePrice;
	if (!initial) {
		return label;
	}

	const auto diff = (100 * (value->lastSalePrice - initial))
		/ float64(initial);
	const auto use = (std::abs(diff) >= 10.)
		? base::SafeRound(diff)
		: (int(base::SafeRound(diff * 100)) / 100.);
	const auto prefix = (use > 0) ? u"+"_q : QString();
	const auto percent = Lang::FormatExactCountDecimal(use) + '%';
	auto text = rpl::single(prefix + percent);
	return MakeValueWithSmallButton(table, label, std::move(text));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePriceValueWithTooltip(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<InfoTooltipData> data,
		TextWithEntities price,
		TextWithEntities tooltip) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		table,
		rpl::single(price),
		table->st().defaultValue);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto handler = [=](not_null<Ui::RpWidget*> button) {
		ShowInfoTooltip(
			data,
			button,
			rpl::single(tooltip),
			kPriceTooltipDuration);
	};
	auto text = rpl::single(u"?"_q);
	return MakeValueWithSmallButton(table, label, std::move(text), handler);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeMinimumPriceValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<InfoTooltipData> tooltip,
		const std::shared_ptr<Data::UniqueGift> &unique) {
	const auto &value = unique->value;
	const auto text = FormatValuePrice(value->minimumPrice, value->currency);
	return MakePriceValueWithTooltip(
		table,
		std::move(tooltip),
		text,
		tr::lng_gift_value_minimum_price_tooltip(
			tr::now,
			lt_amount,
			Ui::Text::Bold(text.text),
			lt_gift,
			Ui::Text::Bold(unique->title),
			Ui::Text::WithEntities));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeAveragePriceValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<InfoTooltipData> tooltip,
		const std::shared_ptr<Data::UniqueGift> &unique) {
	const auto &value = unique->value;
	const auto text = FormatValuePrice(value->averagePrice, value->currency);
	return MakePriceValueWithTooltip(
		table,
		std::move(tooltip),
		text,
		tr::lng_gift_value_average_price_tooltip(
			tr::now,
			lt_amount,
			Ui::Text::Bold(text.text),
			lt_gift,
			Ui::Text::Bold(unique->title),
			Ui::Text::WithEntities));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeAttributeValue(
		not_null<Ui::TableLayout*> table,
		const Data::UniqueGiftAttribute &attribute,
		Fn<void(not_null<Ui::RpWidget*>, int)> showTooltip) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		table,
		attribute.name,
		table->st().defaultValue);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto permille = attribute.rarityPermille;
	auto text = rpl::single(QString::number(permille / 10.) + '%');

	const auto handler = [=](not_null<Ui::RpWidget*> button) {
		showTooltip(button, permille);
	};
	return MakeValueWithSmallButton(table, label, std::move(text), handler);
}

void AddUniqueGiftPropertyRows(
		not_null<Ui::RpWidget*> container,
		not_null<Ui::TableLayout*> table,
		not_null<Data::UniqueGift*> unique) {
	const auto tooltip = std::make_shared<InfoTooltipData>(InfoTooltipData{
		.parent = container,
	});
	const auto showTooltip = [=](
			not_null<Ui::RpWidget*> widget,
			rpl::producer<TextWithEntities> text) {
		ShowInfoTooltip(tooltip, widget, std::move(text), kTooltipDuration);
	};
	const auto showRarity = [=](
			not_null<Ui::RpWidget*> widget,
			int rarity) {
		const auto percent = QString::number(rarity / 10.) + '%';
		showTooltip(widget, tr::lng_gift_unique_rarity(
			lt_percent,
			rpl::single(TextWithEntities{ percent }),
			Ui::Text::WithEntities));
	};
	AddTableRow(
		table,
		tr::lng_gift_unique_model(),
		MakeAttributeValue(table, unique->model, showRarity));
	AddTableRow(
		table,
		tr::lng_gift_unique_backdrop(),
		MakeAttributeValue(table, unique->backdrop, showRarity));
	AddTableRow(
		table,
		tr::lng_gift_unique_symbol(),
		MakeAttributeValue(table, unique->pattern, showRarity));
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeStarGiftStarsValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::CreditsHistoryEntry &entry,
		Fn<void()> convertToStars) {
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto price = helper.paletteDependent(Ui::Earn::IconCreditsEmoji(
	)).append(' ').append(Lang::FormatCreditsAmountDecimal(entry.credits));
	auto label = object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(price),
		table->st().defaultValue,
		st::defaultPopupMenu,
		helper.context());
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	if (!convertToStars) {
		return label;
	}
	const auto handler = [=](not_null<Ui::RpWidget*> button) {
		convertToStars();
	};
	auto text = tr::lng_gift_sell_small(
		lt_count_decimal,
		rpl::single(entry.starsConverted * 1.));
	return MakeValueWithSmallButton(
		table,
		label.release(),
		std::move(text),
		handler);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeUniqueGiftValueValue(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::CreditsHistoryEntry &entry,
		Settings::CreditsEntryBoxStyleOverrides st) {
	const auto unique = entry.uniqueGift;
	const auto value = unique ? unique->value : nullptr;
	const auto loading = std::make_shared<bool>(false);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		table,
		rpl::single(
			FormatValuePrice(value->valuePrice, value->currency, true)),
		table->st().defaultValue,
		st::defaultPopupMenu);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto handler = [=](not_null<Ui::RpWidget*> button) {
		if (value->initialPriceStars) {
			show->show(Box(Settings::UniqueGiftValueBox, show, entry, st));
		} else if (*loading) {
			return;
		}
		*loading = true;
		show->session().api().request(MTPpayments_GetUniqueStarGiftValueInfo(
			MTP_string(unique->slug)
		)).done([=](const MTPpayments_UniqueStarGiftValueInfo &result) {
			*loading = false;

			const auto &data = result.data();
			value->currency = qs(data.vcurrency());
			value->valuePrice = data.vvalue().v;
			value->initialSaleDate = data.vinitial_sale_date().v;
			value->initialPriceStars = CreditsAmount(
				data.vinitial_sale_stars().v);
			value->initialSalePrice = data.vinitial_sale_price().v;
			value->lastSaleDate = data.vlast_sale_date().value_or_empty();
			value->lastSalePrice = data.vlast_sale_price().value_or_empty();
			value->lastSaleFragment = data.is_last_sale_on_fragment();
			value->minimumPrice = data.vfloor_price().value_or_empty();
			value->averagePrice = data.vaverage_price().value_or_empty();
			value->forSaleOnTelegram = data.vlisted_count().value_or_empty();
			value->forSaleOnFragment = int(
				data.vfragment_listed_count().value_or_empty());
			value->fragmentUrl = qs(
				data.vfragment_listed_url().value_or_empty());

			show->show(Box(Settings::UniqueGiftValueBox, show, entry, st));
		}).send();
	};
	return MakeValueWithSmallButton(
		table,
		label,
		tr::lng_gift_unique_value_learn_more(),
		handler);
}

not_null<Ui::FlatLabel*> AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		rpl::producer<TextWithEntities> value,
		const Ui::Text::MarkedContext &context = {}) {
	auto widget = object_ptr<Ui::FlatLabel>(
		table,
		std::move(value),
		table->st().defaultValue,
		st::defaultPopupMenu,
		context);
	const auto result = widget.data();
	AddTableRow(table, std::move(label), std::move(widget));
	return result;
}

void AddTableRow(
		not_null<Ui::TableLayout*> table,
		rpl::producer<QString> label,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id) {
	if (!id) {
		return;
	}
	AddTableRow(
		table,
		std::move(label),
		MakePeerTableValue(table, show, id),
		st::giveawayGiftCodePeerMargin);
}

void AddTable(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<ChatHelpers::Show> show,
		Settings::CreditsEntryBoxStyleOverrides st,
		const Api::GiftCode &current,
		bool skipReason) {
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	if (current.from) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_from(),
			show,
			current.from);
	}
	if (current.from && current.to) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_to(),
			show,
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
			if (const auto window = show->resolveWindow()) {
				window->showPeerHistory(
					current.from,
					Window::SectionShow::Way::Forward,
					current.giveawayId);
			}
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
		.filter = crl::guard(navigation, shareLink),
		.duration = 6 * crl::time(1000),
	});
}

} // namespace

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
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = nullptr,
				.title = rpl::conditional(
					state->used.value(),
					tr::lng_gift_link_used_title(),
					tr::lng_gift_link_title()),
				.about = rpl::conditional(
					state->used.value(),
					tr::lng_gift_link_used_about(Ui::Text::RichLangValue),
					tr::lng_gift_link_about(Ui::Text::RichLangValue)),
				.light = true,
			}));

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

	const auto show = controller->uiShow();
	AddTable(box->verticalLayout(), show, {}, state->data.current(), false);

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
		st::giveawayGiftCodeFooterMargin,
		style::al_top);
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

	box->addButton(rpl::conditional(
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
				Ui::Premium::TopBarDescriptor{
					.clickContextOther = clickContext,
					.title = tr::lng_gift_link_title(),
					.about = tr::lng_gift_link_pending_about(
						lt_user,
						rpl::single(Ui::Text::Link(resultToName)),
						Ui::Text::RichLangValue),
					.light = true,
				}));

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

	const auto show = controller->uiShow();
	AddTable(box->verticalLayout(), show, {}, data, true);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_link_pending_footer(),
			st::giveawayGiftCodeFooter),
		st::giveawayGiftCodeFooterMargin,
		style::al_top);

	const auto close = Ui::CreateChild<Ui::IconButton>(
		box.get(),
		st::boxTitleClose);
	const auto closeCallback = [=] { box->closeBox(); };
	close->setClickedCallback(closeCallback);
	box->widthValue(
	) | rpl::start_with_next([=](int width) {
		close->moveToRight(0, 0);
	}, box->lifetime());

	box->addButton(tr::lng_close(), closeCallback);
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

	auto resultText = (!info.giftCode.isEmpty())
		? tr::lng_prizes_you_won(
			lt_cup,
			rpl::single(
				TextWithEntities{ QString::fromUtf8("\xf0\x9f\x8f\x86") }),
			Ui::Text::WithEntities)
		: (info.credits)
		? tr::lng_prizes_you_won_credits(
			lt_amount,
			tr::lng_prizes_you_won_credits_amount(
				lt_count,
				rpl::single(float64(info.credits)),
				Ui::Text::Bold),
			lt_cup,
			rpl::single(
				TextWithEntities{ QString::fromUtf8("\xf0\x9f\x8f\x86") }),
			Ui::Text::WithEntities)
		: (info.state == State::Finished)
		? tr::lng_prizes_you_didnt(Ui::Text::WithEntities)
		: (rpl::producer<TextWithEntities>)(nullptr);

	if (resultText) {
		const auto &st = st::changePhoneDescription;
		const auto skip = st.style.font->height * 0.5;
		auto label = object_ptr<Ui::FlatLabel>(
			box.get(),
			std::move(resultText),
			st);
		if ((!info.giftCode.isEmpty()) || info.credits) {
			label->setTextColorOverride(st::windowActiveTextFg->c);
		}
		const auto result = box->addRow(
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				box.get(),
				std::move(label),
				QMargins(0, skip, 0, skip)),
			style::al_justify);
		result->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(result);
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxDividerBg);
			p.drawRoundedRect(result->rect(), st::boxRadius, st::boxRadius);
		}, result->lifetime());
		Ui::AddSkip(box->verticalLayout());
	}

	auto text = TextWithEntities();

	const auto quantity = start
		? start->quantity
		: (results->winnersCount + results->unclaimedCount);
	const auto months = start ? start->months : results->months;
	const auto group = results
		? results->channel->isMegagroup()
		: (!start->channels.empty()
			&& start->channels.front()->isMegagroup());
	const auto credits = start
		? start->credits
		: (results ? results->credits : 0);
	text.append((finished
		? tr::lng_prizes_end_text
		: tr::lng_prizes_how_text)(
			tr::now,
			lt_admins,
			credits
				? (group
					? tr::lng_prizes_credits_admins_group
					: tr::lng_prizes_credits_admins)(
						tr::now,
						lt_channel,
						Ui::Text::Bold(first),
						lt_amount,
						tr::lng_prizes_credits_admins_amount(
							tr::now,
							lt_count_decimal,
							float64(credits),
							Ui::Text::Bold),
						Ui::Text::RichLangValue)
				: (group
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
			{ padding.left(), 0, padding.right(), padding.bottom() },
			style::al_top);
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

QString TonAddressUrl(
		not_null<Main::Session*> session,
		const QString &address) {
	const auto prefix = session->appConfig().get<QString>(
		u"ton_blockchain_explorer_url"_q,
		u"https://tonviewer.com/"_q);
	return prefix + address;
}

struct AddedUniqueDetails {
	object_ptr<Ui::RpWidget> widget;
	not_null<Ui::FlatLabel*> label;
};
[[nodiscard]] AddedUniqueDetails MakeUniqueDetails(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<QWidget*> parent,
		rpl::producer<TextWithEntities> text,
		Settings::CreditsEntryBoxStyleOverrides st,
		Ui::Text::MarkedContext context,
		int removeCost,
		Fn<void(Fn<void()> removed)> remove) {
	auto owned = object_ptr<Ui::FlatLabel>(
		parent,
		rpl::duplicate(text),
		(st.tableValueMessage
			? *st.tableValueMessage
			: st::giveawayGiftMessage),
		st::defaultPopupMenu,
		context);
	const auto label = owned.data();
	if (!remove) {
		return {
			.widget = std::move(owned),
			.label = label,
		};
	}
	owned.release();

	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();

	label->setParent(raw);
	label->show();
	const auto icon = Ui::CreateChild<Ui::IconButton>(
		raw,
		st::giveawayGiftMessageRemove);
	raw->widthValue() | rpl::start_with_next([=](int outer) {
		label->resizeToWidth(outer - icon->width());
		const auto height = std::max(label->height(), icon->height());

		icon->moveToRight(0, (height - icon->height()) / 2);
		label->move(0, (height - label->height()) / 2);

		raw->resize(outer, height);
	}, raw->lifetime());
	icon->setClickedCallback([=] {
		const auto weak = base::make_weak(raw);
		show->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto confirming = base::make_weak(box);
			const auto confirmed = [=] {
				remove([=] {
					delete weak.get();
					if (const auto strong = confirming.get()) {
						strong->closeBox();
					}
				});
			};
			Ui::ConfirmBox(box, {
				.text = tr::lng_gift_unique_info_remove_text(),
				.confirmed = confirmed,
				.confirmText = tr::lng_gift_unique_info_remove_confirm(
					lt_cost,
					rpl::single(
						Ui::Text::IconEmoji(&st::starIconEmoji).append(
							Lang::FormatCountDecimal(removeCost))),
					Ui::Text::RichLangValue),
				.title = tr::lng_gift_unique_info_remove_title(),
			});
			box->addRow(
				object_ptr<Ui::TableLayout>(
					box,
					st.table ? *st.table : st::giveawayGiftCodeTable)
			)->addRow(
				object_ptr<Ui::FlatLabel>(
					parent,
					rpl::duplicate(text),
					(st.tableValueMessage
						? *st.tableValueMessage
						: st::giveawayGiftMessage),
					st::defaultPopupMenu,
					context),
				nullptr,
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
		}));
	});

	return {
		.widget = std::move(result),
		.label = label,
	};
}

void AddStarGiftTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		Settings::CreditsEntryBoxStyleOverrides st,
		const Data::CreditsHistoryEntry &entry,
		Fn<void()> convertToStars,
		Fn<void()> startUpgrade,
		Fn<void(Fn<void()> removed)> removeDetails) {
	const auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	const auto peerId = PeerId(entry.barePeerId);
	const auto session = &show->session();
	const auto unique = entry.uniqueGift.get();
	const auto selfBareId = session->userPeerId().value;
	const auto giftToSelf = (peerId == session->userPeerId())
		&& (entry.in || entry.bareGiftOwnerId == selfBareId);
	const auto giftToChannel = entry.giftChannelSavedId
		&& peerIsChannel(PeerId(entry.bareEntryOwnerId));

	const auto tooltip = std::make_shared<InfoTooltipData>(InfoTooltipData{
		.parent = container,
	});
	const auto showTooltip = [=](
			not_null<Ui::RpWidget*> widget,
			rpl::producer<TextWithEntities> text) {
		ShowInfoTooltip(tooltip, widget, std::move(text), kTooltipDuration);
	};

	if (unique && entry.bareGiftResaleRecipientId) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_peer(),
			MakePeerTableValue(
				table,
				show,
				PeerId(entry.bareGiftResaleRecipientId)),
			st::giveawayGiftCodePeerMargin);
	} else if (unique && entry.bareGiftOwnerId) {
		const auto ownerId = PeerId(entry.bareGiftOwnerId);
		const auto was = std::make_shared<std::optional<CollectibleId>>();
		const auto handleChange = [=](
				not_null<Ui::RpWidget*> badge,
				EmojiStatusId emojiStatusId) {
			const auto id = emojiStatusId.collectible
				? emojiStatusId.collectible->id
				: 0;
			const auto show = [&](const auto &phrase) {
				showTooltip(badge, phrase(
					lt_name,
					rpl::single(Ui::Text::Bold(UniqueGiftName(*unique))),
					Ui::Text::WithEntities));
			};
			if (!*was || *was == id) {
				*was = id;
				return;
			} else if (*was == unique->id) {
				show(tr::lng_gift_wear_end_toast);
			} else if (id == unique->id) {
				show(tr::lng_gift_wear_start_toast);
			}
			*was = id;
		};
		AddTableRow(
			table,
			tr::lng_gift_unique_owner(),
			MakePeerWithStatusValue(table, show, ownerId, handleChange),
			st::giveawayGiftCodePeerMargin);
	} else if (unique) {
		if (!unique->ownerName.isEmpty()) {
			AddTableRow(
				table,
				tr::lng_gift_unique_owner(),
				rpl::single(TextWithEntities{ unique->ownerName }));
		} else if (auto address = unique->ownerAddress; !address.isEmpty()) {
			auto label = MakeMaybeMultilineTokenValue(table, address, st);
			label->setClickHandlerFilter([=](const auto &...) {
				TextUtilities::SetClipboardText(
					TextForMimeData::Simple(FixupTransactionId(address)));
				show->showToast(
					tr::lng_gift_unique_address_copied(tr::now));
				return false;
			});
			AddTableRow(
				table,
				tr::lng_gift_unique_owner(),
				std::move(label));
		}

		if (const auto hostId = PeerId(entry.bareGiftHostId)) {
			const auto was = std::make_shared<std::optional<CollectibleId>>();
			const auto handleChange = [=](
					not_null<Ui::RpWidget*> badge,
					EmojiStatusId emojiStatusId) {
				const auto id = emojiStatusId.collectible
					? emojiStatusId.collectible->id
					: 0;
				const auto show = [&](const auto &phrase) {
					showTooltip(badge, phrase(
						lt_name,
						rpl::single(Ui::Text::Bold(UniqueGiftName(*unique))),
						Ui::Text::WithEntities));
				};
				if (!*was || *was == id) {
					*was = id;
					return;
				} else if (*was == unique->id) {
					show(tr::lng_gift_wear_end_toast);
				} else if (id == unique->id) {
					show(tr::lng_gift_wear_start_toast);
				}
				*was = id;
			};
			AddTableRow(
				table,
				tr::lng_gift_unique_telegram(),
				MakePeerWithStatusValue(table, show, hostId, handleChange),
				st::giveawayGiftCodePeerMargin);
		}

	} else if (giftToChannel) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_peer_in(),
			(entry.bareActorId
				? MakePeerTableValue(table, show, PeerId(entry.bareActorId))
				: MakeHiddenPeerTableValue(table)),
			st::giveawayGiftCodePeerMargin);
		if (entry.bareEntryOwnerId) {
			AddTableRow(
				table,
				tr::lng_credits_box_history_entry_peer(),
				MakePeerTableValue(
					table,
					show,
					PeerId(entry.bareEntryOwnerId)),
				st::giveawayGiftCodePeerMargin);
		}
	} else if (peerId && !giftToSelf) {
		const auto user = session->data().peer(peerId)->asUser();
		const auto withSendButton = entry.in && user && !user->isBot();
		auto send = withSendButton ? tr::lng_gift_send_small() : nullptr;
		auto handler = send ? Fn<void()>([=] {
			if (const auto window = show->resolveWindow()) {
				Ui::ShowStarGiftBox(window, user);
			}
		}) : nullptr;
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_peer_in(),
			MakePeerTableValue(table, show, peerId, send, handler),
			st::giveawayGiftCodePeerMargin);
	} else if (!entry.soldOutInfo) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_peer_in(),
			MakeHiddenPeerTableValue(table),
			st::giveawayGiftCodePeerMargin);
	}
	if (!unique && !entry.firstSaleDate.isNull()) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_first_sale(),
			rpl::single(Ui::Text::WithEntities(
				langDateTime(entry.firstSaleDate))));
	}
	if (!unique && !entry.lastSaleDate.isNull()) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_last_sale(),
			rpl::single(Ui::Text::WithEntities(
				langDateTime(entry.lastSaleDate))));
	}
	if (!unique && !entry.date.isNull()) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_date(),
			rpl::single(Ui::Text::WithEntities(langDateTime(entry.date))));
	}
	if (unique) {
		AddUniqueGiftPropertyRows(container, table, unique);
	} else {
		AddTableRow(
			table,
			tr::lng_gift_link_label_value(),
			MakeStarGiftStarsValue(
				table,
				show,
				entry,
				std::move(convertToStars)));
	}
	if (entry.limitedCount > 0 && !entry.giftRefunded) {
		auto amount = rpl::single(TextWithEntities{
			Lang::FormatCountDecimal(entry.limitedCount)
		});
		const auto count = unique
			? (entry.limitedCount - entry.limitedLeft)
			: entry.limitedLeft;
		AddTableRow(
			table,
			(unique
				? tr::lng_gift_unique_availability_label()
				: tr::lng_gift_availability()),
			((!unique && !count)
				? tr::lng_gift_availability_none(
					lt_amount,
					std::move(amount),
					Ui::Text::WithEntities)
				: (unique
					? tr::lng_gift_unique_availability
					: tr::lng_gift_availability_left)(
						lt_count_decimal,
						rpl::single(count * 1.),
						lt_amount,
						std::move(amount),
						Ui::Text::WithEntities)));
	}
	if (!unique && !entry.soldOutInfo && startUpgrade) {
		AddTableRow(
			table,
			tr::lng_gift_unique_status(),
			tr::lng_gift_unique_status_non(Ui::Text::WithEntities));
	}
	if (unique) {
		if (unique->value) {
			AddTableRow(
				table,
				tr::lng_gift_unique_value(),
				MakeUniqueGiftValueValue(table, show, entry, st));
		}
		const auto &original = unique->originalDetails;
		if (original.recipientId) {
			const auto owner = &show->session().data();
			const auto to = owner->peer(original.recipientId);
			const auto from = original.senderId
				? owner->peer(original.senderId).get()
				: nullptr;
			const auto date = base::unixtime::parse(original.date).date();
			const auto dateText = TextWithEntities{ langDayOfMonth(date) };
			auto details = from
				? (original.message.empty()
					? tr::lng_gift_unique_info_sender(
						lt_from,
						rpl::single(Ui::Text::Link(from->name(), 2)),
						lt_recipient,
						rpl::single(Ui::Text::Link(to->name(), 1)),
						lt_date,
						rpl::single(dateText),
						Ui::Text::WithEntities)
					: tr::lng_gift_unique_info_sender_comment(
						lt_from,
						rpl::single(Ui::Text::Link(from->name(), 2)),
						lt_recipient,
						rpl::single(Ui::Text::Link(to->name(), 1)),
						lt_date,
						rpl::single(dateText),
						lt_text,
						rpl::single(original.message),
						Ui::Text::WithEntities))
				: (original.message.empty()
					? tr::lng_gift_unique_info_reciever(
						lt_recipient,
						rpl::single(Ui::Text::Link(to->name(), 1)),
						lt_date,
						rpl::single(dateText),
						Ui::Text::WithEntities)
					: tr::lng_gift_unique_info_reciever_comment(
						lt_recipient,
						rpl::single(Ui::Text::Link(to->name(), 1)),
						lt_date,
						rpl::single(dateText),
						lt_text,
						rpl::single(original.message),
						Ui::Text::WithEntities));
			const auto tmp = std::make_shared<Ui::RpWidget*>(nullptr);
			auto made = MakeUniqueDetails(
				show,
				table,
				std::move(details),
				st,
				Core::TextContext({ .session = session }),
				entry.starsForDetailsRemove,
				std::move(removeDetails));
			const auto showBoxLink = [=](not_null<PeerData*> peer) {
				return std::make_shared<LambdaClickHandler>([=] {
					show->showBox(PrepareShortInfoBox(peer, show));
				});
			};
			made.label->setLink(1, showBoxLink(to));
			if (from) {
				made.label->setLink(2, showBoxLink(from));
			}
			made.label->setSelectable(true);
			*tmp = made.widget.data();
			table->addRow(
				std::move(made.widget),
				nullptr,
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
		}
	} else if (!entry.description.empty()) {
		auto label = object_ptr<Ui::FlatLabel>(
			table,
			rpl::single(entry.description),
			(st.tableValueMessage
				? *st.tableValueMessage
				: st::giveawayGiftMessage),
			st::defaultPopupMenu,
			Core::TextContext({ .session = session }));
		label->setSelectable(true);
		table->addRow(
			nullptr,
			std::move(label),
			st::giveawayGiftCodeLabelMargin,
			st::giveawayGiftCodeValueMargin);
	}
}

void AddTransferGiftTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Data::UniqueGift> unique) {
	const auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	AddUniqueGiftPropertyRows(container, table, unique.get());
	if (const auto value = unique->value.get()) {
		AddTableRow(
			table,
			tr::lng_gift_unique_value(),
			rpl::single(
				FormatValuePrice(value->valuePrice, value->currency, true)));
	}
}

void AddCreditsHistoryEntryTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		Settings::CreditsEntryBoxStyleOverrides st,
		const Data::CreditsHistoryEntry &entry) {
	if (!entry) {
		return;
	}
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	const auto peerId = PeerId(entry.barePeerId);
	const auto actorId = PeerId(entry.bareActorId);
	const auto starrefRecipientId = PeerId(entry.starrefRecipientId);
	const auto session = &show->session();
	if (entry.starrefCommission) {
		if (entry.giftResale && entry.starrefCommission < 1000) {
			const auto full = int(base::SafeRound(entry.credits.value()
				/ (1. - (entry.starrefCommission / 1000.))));
			auto value = Ui::Text::IconEmoji(&st::starIconEmojiColored);
			const auto starsText = Lang::FormatCreditsAmountDecimal(
				CreditsAmount{ full });
			AddTableRow(
				table,
				tr::lng_credits_box_history_entry_gift_full_price(),
				rpl::single(value.append(' ' + starsText)));
		} else if (entry.starrefAmount) {
			AddTableRow(
				table,
				tr::lng_star_ref_commission_title(),
				rpl::single(TextWithEntities{
					QString::number(entry.starrefCommission / 10.) + '%' }));
		} else {
			AddTableRow(
				table,
				tr::lng_gift_link_label_reason(),
				tr::lng_credits_box_history_entry_reason_star_ref(
					Ui::Text::WithEntities));
		}
	}
	if (starrefRecipientId && entry.starrefAmount && !entry.giftResale) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_affiliate(),
			show,
			starrefRecipientId);
	}
	if (peerId && entry.starrefCommission) {
		AddTableRow(
			table,
			(entry.giftResale
				? tr::lng_credits_box_history_entry_gift_sold_to
				: entry.starrefAmount
				? tr::lng_credits_box_history_entry_referred
				: tr::lng_credits_box_history_entry_miniapp)(),
			show,
			peerId);
	}
	if (!entry.postsSearch
		&& (actorId || (!entry.starrefCommission && peerId))) {
		auto text = entry.starrefCommission
			? tr::lng_credits_box_history_entry_referred()
			: entry.in
			? tr::lng_credits_box_history_entry_peer_in()
			: entry.giftResale
			? tr::lng_credits_box_history_entry_gift_bought_from()
			: entry.giftUpgraded
			? tr::lng_credits_box_history_entry_gift_from()
			: tr::lng_credits_box_history_entry_peer();
		AddTableRow(
			table,
			std::move(text),
			show,
			actorId ? actorId : peerId);
	}
	if (const auto msgId = MsgId(peerId ? entry.bareMsgId : 0)) {
		const auto peer = session->data().peer(peerId);
		if (const auto channel = peer->asBroadcast()) {
			const auto link = CreateMessageLink(
				session,
				peerId,
				entry.bareMsgId);
			auto label = object_ptr<Ui::FlatLabel>(
				table,
				rpl::single(Ui::Text::Link(link)),
				table->st().defaultValue);
			label->setClickHandlerFilter([=](const auto &...) {
				if (const auto window = show->resolveWindow()) {
					window->showPeerHistory(channel, {}, msgId);
				}
				return false;
			});
			AddTableRow(
				table,
				(entry.reaction
					? tr::lng_credits_box_history_entry_message
					: tr::lng_credits_box_history_entry_media)(),
				std::move(label));
		}
	}
	using Type = Data::CreditsHistoryEntry::PeerType;
	if (entry.peerType == Type::AppStore) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_via(),
			tr::lng_credits_box_history_entry_app_store(
				Ui::Text::RichLangValue));
	} else if (entry.peerType == Type::PlayMarket) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_via(),
			tr::lng_credits_box_history_entry_play_market(
				Ui::Text::RichLangValue));
	} else if (entry.peerType == Type::Fragment) {
		AddTableRow(
			table,
			(entry.gift
				? tr::lng_credits_box_history_entry_peer_in
				: tr::lng_credits_box_history_entry_via)(),
			((entry.gift && entry.credits.stars())
				? tr::lng_credits_box_history_entry_anonymous
				: tr::lng_credits_box_history_entry_fragment)(
					Ui::Text::RichLangValue));
	} else if (entry.peerType == Type::Ads) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_via(),
			tr::lng_credits_box_history_entry_ads(Ui::Text::RichLangValue));
	} else if (entry.peerType == Type::PremiumBot) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_via(),
			tr::lng_credits_box_history_entry_via_premium_bot(
				Ui::Text::RichLangValue));
	}
	if (entry.bareGiveawayMsgId) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_to(),
			show,
			show->session().userId());
	}
	if (entry.bareGiveawayMsgId && entry.credits) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_gift(),
			tr::lng_gift_stars_title(
				lt_count,
				rpl::single(entry.credits.value()),
				Ui::Text::RichLangValue));
	}
	{
		const auto link = CreateMessageLink(
			session,
			peerId,
			entry.bareGiveawayMsgId);
		if (!link.isEmpty()) {
			AddTableRow(
				table,
				tr::lng_gift_link_label_reason(),
				tr::lng_gift_link_reason_giveaway(
				) | rpl::map([link](const QString &text) {
					return Ui::Text::Link(text, link);
				}));
		}
	}
	if (!entry.subscriptionUntil.isNull() && !entry.title.isEmpty()) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_reason(),
			tr::lng_credits_box_history_entry_subscription(
				Ui::Text::WithEntities));
	}
	if (entry.paidMessagesAmount) {
		auto value = Ui::Text::IconEmoji(&st::starIconEmojiColored);
		const auto full = (entry.in ? 1 : -1)
			* (entry.credits + entry.paidMessagesAmount);
		const auto starsText = Lang::FormatCreditsAmountDecimal(full);
		AddTableRow(
			table,
			tr::lng_credits_paid_messages_full(),
			rpl::single(value.append(' ' + starsText)));
	}
	if (const auto months = entry.premiumMonthsForStars) {
		AddTableRow(
			table,
			tr::lng_credits_premium_gift_duration(),
			tr::lng_months(
				lt_count,
				rpl::single(1. * months),
				Ui::Text::WithEntities));
	}
	if (!entry.id.isEmpty()) {
		auto label = MakeMaybeMultilineTokenValue(table, entry.id, st);
		label->setClickHandlerFilter([=](const auto &...) {
			TextUtilities::SetClipboardText(
				TextForMimeData::Simple(FixupTransactionId(entry.id)));
			show->showToast(
				tr::lng_credits_box_history_entry_id_copied(tr::now));
			return false;
		});
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_id(),
			std::move(label));
	}
	if (entry.floodSkip) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_floodskip_row(),
			rpl::single(
				Ui::Text::WithEntities(
					Lang::FormatCountDecimal(entry.floodSkip))));
	}
	if (!entry.date.isNull()) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_date(),
			rpl::single(Ui::Text::WithEntities(langDateTime(entry.date))));
	}
	if (!entry.successDate.isNull()) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_success_date(),
			rpl::single(Ui::Text::WithEntities(langDateTime(entry.date))));
	}
	if (!entry.successLink.isEmpty()) {
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_success_url(),
			rpl::single(
				Ui::Text::Link(entry.successLink, entry.successLink)));
	}
}

void AddSubscriptionEntryTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		Settings::CreditsEntryBoxStyleOverrides st,
		const Data::SubscriptionEntry &s) {
	if (!s) {
		return;
	}
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	const auto peerId = PeerId(s.barePeerId);
	const auto user = peerIsUser(peerId)
		? show->session().data().peer(peerId)->asUser()
		: nullptr;
	AddTableRow(
		table,
		(!s.title.isEmpty() && user && user->botInfo)
			? tr::lng_credits_subscription_row_to_bot()
			: (!s.title.isEmpty() && user && !user->botInfo)
			? tr::lng_credits_subscription_row_to_business()
			: tr::lng_credits_subscription_row_to(),
		show,
		peerId);
	if (!s.title.isEmpty()) {
		AddTableRow(
			table,
			tr::lng_credits_subscription_row_to(),
			rpl::single(Ui::Text::WithEntities(s.title)));
	}
	if (!s.until.isNull()) {
		if (s.subscription.period > 0) {
			const auto subscribed = s.until.addSecs(-s.subscription.period);
			if (subscribed.isValid()) {
				AddTableRow(
					table,
					tr::lng_group_invite_joined_row_date(),
					rpl::single(
						Ui::Text::WithEntities(langDateTime(subscribed))));
			}
		}
		AddTableRow(
			table,
			s.expired
				? tr::lng_credits_subscription_row_next_none()
				: s.cancelled
				? tr::lng_credits_subscription_row_next_off()
				: tr::lng_credits_subscription_row_next_on(),
			rpl::single(Ui::Text::WithEntities(langDateTime(s.until))));
	}
}

void AddSubscriberEntryTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		Settings::CreditsEntryBoxStyleOverrides st,
		not_null<PeerData*> peer,
		TimeId date) {
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	AddTableRow(
		table,
		tr::lng_group_invite_joined_row_subscriber(),
		show,
		peer->id);
	if (const auto d = base::unixtime::parse(date); !d.isNull()) {
		AddTableRow(
			table,
			tr::lng_group_invite_joined_row_date(),
			rpl::single(Ui::Text::WithEntities(langDateTime(d))));
	}
}

void AddCreditsBoostTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		Settings::CreditsEntryBoxStyleOverrides st,
		const Data::Boost &b) {
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	const auto peerId = b.giveawayMessage.peer;
	if (!peerId) {
		return;
	}
	const auto from = show->session().data().peer(peerId);
	AddTableRow(
		table,
		tr::lng_credits_box_history_entry_peer_in(),
		show,
		from->id);
	if (b.credits) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_gift(),
			tr::lng_gift_stars_title(
				lt_count,
				rpl::single(float64(b.credits)),
				Ui::Text::RichLangValue));
	}
	{
		const auto link = CreateMessageLink(
			&show->session(),
			peerId,
			b.giveawayMessage.msg.bare);
		if (!link.isEmpty()) {
			AddTableRow(
				table,
				tr::lng_gift_link_label_reason(),
				tr::lng_gift_link_reason_giveaway(
				) | rpl::map([link](const QString &text) {
					return Ui::Text::Link(text, link);
				}));
		}
	}
	if (!b.date.isNull()) {
		AddTableRow(
			table,
			tr::lng_gift_link_label_date(),
			rpl::single(Ui::Text::WithEntities(langDateTime(b.date))));
	}
	if (!b.expiresAt.isNull()) {
		AddTableRow(
			table,
			tr::lng_gift_until(),
			rpl::single(Ui::Text::WithEntities(langDateTime(b.expiresAt))));
	}
}

void AddChannelEarnTable(
		std::shared_ptr<Ui::Show> show,
		not_null<Ui::VerticalLayout*> container,
		const Data::CreditsHistoryEntry &entry) {
	const auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	if (!entry.id.isEmpty()) {
		auto label = MakeMaybeMultilineTokenValue(table, entry.id, {});
		label->setClickHandlerFilter([=](const auto &...) {
			TextUtilities::SetClipboardText(
				TextForMimeData::Simple(FixupTransactionId(entry.id)));
			show->showToast(
				tr::lng_credits_box_history_entry_id_copied(tr::now));
			return false;
		});
		AddTableRow(
			table,
			tr::lng_credits_box_history_entry_id(),
			std::move(label));
	}
}

void AddUniqueGiftValueTable(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::VerticalLayout*> container,
		Settings::CreditsEntryBoxStyleOverrides st,
		const Data::CreditsHistoryEntry &entry) {
	const auto value = entry.uniqueGift ? entry.uniqueGift->value : nullptr;
	auto table = container->add(
		object_ptr<Ui::TableLayout>(
			container,
			st.table ? *st.table : st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	if (value->initialSaleDate) {
		AddTableRow(
			table,
			tr::lng_gift_value_initial_sale(),
			rpl::single(FormatValueDate(value->initialSaleDate)));
	}
	auto helper = Ui::Text::CustomEmojiHelper();
	auto starIcon = helper.paletteDependent(
		Ui::Earn::IconCreditsEmoji());
	AddTableRow(
		table,
		tr::lng_gift_value_initial_price(),
		tr::lng_gift_value_initial_price_value(
			lt_stars,
			rpl::single(starIcon.append(' ').append(
				Lang::FormatCreditsAmountDecimal(value->initialPriceStars)
			)),
			lt_amount,
			rpl::single(FormatValuePrice(
				value->initialSalePrice,
				value->currency,
				true)),
			Ui::Text::WithEntities),
		helper.context());
	if (value->lastSaleDate) {
		AddTableRow(
			table,
			tr::lng_gift_value_last_sale(),
			rpl::single(FormatValueDate(value->lastSaleDate)));
	}
	if (value->lastSalePrice) {
		AddTableRow(
			table,
			tr::lng_gift_value_last_price(),
			MakePriceWithChangePercentValue(table, value));
	}

	const auto tooltip = std::make_shared<InfoTooltipData>(InfoTooltipData{
		.parent = container,
	});
	if (value->minimumPrice) {
		AddTableRow(
			table,
			tr::lng_gift_value_minimum_price(),
			MakeMinimumPriceValue(table, tooltip, entry.uniqueGift));
	}
	if (value->averagePrice) {
		AddTableRow(
			table,
			tr::lng_gift_vlaue_average_price(),
			MakeAveragePriceValue(table, tooltip, entry.uniqueGift));
	}
}
