/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_onchain_gift_presentation.h"

#include "base/unixtime.h"
#include "data/data_star_gift.h"
#include "lang/lang_keys.h"
#include "ui/controls/table_rows.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_gift_move_presentation.h"
#include "wallet/wallet_session.h"

#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

struct OnchainGiftPill {
	Ui::Text::String text;
	std::optional<Data::UniqueGiftBackdrop> backdrop;
};

[[nodiscard]] QString OnchainGiftPillText(TransferOnchainGift action) {
	switch (action) {
	case TransferOnchainGift::Buy:
		return tr::lng_wallet_details_onchain_purchased(tr::now);
	case TransferOnchainGift::List:
		return tr::lng_wallet_details_onchain_listed(tr::now);
	case TransferOnchainGift::Delist:
		return tr::lng_wallet_details_onchain_delisted(tr::now);
	case TransferOnchainGift::Reprice:
		return tr::lng_wallet_details_onchain_repriced(tr::now);
	case TransferOnchainGift::OfferCreate:
		return tr::lng_wallet_details_onchain_offer_sent(tr::now);
	case TransferOnchainGift::OfferAcceptBuyer:
	case TransferOnchainGift::OfferAcceptSeller:
		return tr::lng_wallet_details_onchain_offer_accepted(tr::now);
	case TransferOnchainGift::OfferCancel:
		return tr::lng_wallet_details_onchain_offer_cancelled(tr::now);
	case TransferOnchainGift::OfferDecline:
		return tr::lng_wallet_details_onchain_offer_declined(tr::now);
	case TransferOnchainGift::OfferExpire:
		return tr::lng_wallet_details_onchain_offer_expired(tr::now);
	case TransferOnchainGift::None:
		break;
	}
	return QString();
}

void PaintOnchainGiftPill(
		QPainter &p,
		QRect rect,
		const OnchainGiftPill &pill) {
	const auto &padding = st::giftBoxReleasedByMargin;
	const auto radius = rect.height() / 2.;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	if (const auto &backdrop = pill.backdrop) {
		p.setBrush(backdrop->edgeColor);
		p.drawRoundedRect(rect, radius, radius);
		p.setOpacity(0.5);
		p.setBrush(backdrop->patternColor);
		p.drawRoundedRect(rect, radius, radius);
		p.setOpacity(1.);
		p.setPen(QColor(255, 255, 255));
	} else {
		p.setBrush(st::windowActiveTextFg);
		p.drawRoundedRect(rect, radius, radius);
		p.setPen(st::windowBg);
	}
	pill.text.draw(p, {
		.position = rect.topLeft() + QPoint(padding.left(), padding.top()),
		.outerWidth = rect.width(),
		.availableWidth = rect.width() - padding.left() - padding.right(),
		.elisionLines = 1,
	});
}

[[nodiscard]] QString CollectibleChipFixedText(
		TransferOnchainGift action,
		const CollectibleView &view) {
	switch (action) {
	case TransferOnchainGift::List:
		return tr::lng_wallet_row_onchain_listed(tr::now);
	case TransferOnchainGift::Delist:
		return tr::lng_wallet_row_onchain_delisted(tr::now);
	case TransferOnchainGift::OfferCreate:
		return tr::lng_wallet_row_onchain_offer_sent(tr::now);
	case TransferOnchainGift::OfferAcceptBuyer:
		return tr::lng_wallet_row_onchain_offer_accepted(tr::now);
	case TransferOnchainGift::OfferCancel:
		return tr::lng_wallet_row_onchain_offer_cancelled(tr::now);
	case TransferOnchainGift::OfferDecline:
		return tr::lng_wallet_row_onchain_offer_declined(tr::now);
	case TransferOnchainGift::OfferExpire:
		return tr::lng_wallet_row_onchain_offer_expired(tr::now);
	case TransferOnchainGift::None:
	case TransferOnchainGift::Buy:
	case TransferOnchainGift::Reprice:
	case TransferOnchainGift::OfferAcceptSeller:
		break;
	}
	return CollectibleKindText(view);
}

[[nodiscard]] std::optional<tr::phrase<lngtag_amount>> ChipPricedPhrase(
		TransferOnchainGift action,
		int64 amountNano) {
	switch (action) {
	case TransferOnchainGift::Buy:
		return tr::lng_wallet_row_onchain_bought;
	case TransferOnchainGift::List:
		if (amountNano > 0) {
			return tr::lng_wallet_row_onchain_listed_for;
		}
		break;
	case TransferOnchainGift::Reprice:
		return tr::lng_wallet_row_onchain_repriced_to;
	case TransferOnchainGift::OfferAcceptSeller:
		return tr::lng_wallet_row_onchain_offer_accepted_for;
	case TransferOnchainGift::None:
	case TransferOnchainGift::Delist:
	case TransferOnchainGift::OfferCreate:
	case TransferOnchainGift::OfferAcceptBuyer:
	case TransferOnchainGift::OfferCancel:
	case TransferOnchainGift::OfferDecline:
	case TransferOnchainGift::OfferExpire:
		break;
	}
	return std::nullopt;
}

[[nodiscard]] bool ShowsOnchainOffer(const TransferItem &item) {
	switch (item.onchainGift) {
	case TransferOnchainGift::OfferCreate:
	case TransferOnchainGift::OfferAcceptBuyer:
	case TransferOnchainGift::OfferAcceptSeller:
	case TransferOnchainGift::OfferCancel:
	case TransferOnchainGift::OfferDecline:
	case TransferOnchainGift::OfferExpire:
		return true;
	case TransferOnchainGift::None:
	case TransferOnchainGift::Buy:
	case TransferOnchainGift::List:
	case TransferOnchainGift::Delist:
	case TransferOnchainGift::Reprice:
		return false;
	}
	Unexpected("Action in ShowsOnchainOffer.");
}

[[nodiscard]] CollectibleRowLook ComputeOfferRowLook(
		const TransferItem &item) {
	const auto action = item.onchainGift;
	const auto seller = (action == TransferOnchainGift::OfferAcceptSeller);
	const auto accepted = seller
		|| (action == TransferOnchainGift::OfferAcceptBuyer);
	const auto incoming = accepted ? !seller : item.incoming;
	return {
		.service = (!item.counterparty.isEmpty()
			? QString()
			: incoming
			? tr::lng_wallet_row_incoming(tr::now)
			: tr::lng_wallet_row_outgoing(tr::now)),
		.amountNano = (seller
			? item.onchainOffer.priceNano.value_or(item.amountNano)
			: item.amountNano),
		.incoming = incoming,
		.itemAmount = accepted,
	};
}

[[nodiscard]] std::optional<int64> OfferEscrowNano(
		const TransferOnchainOffer &offer) {
	if (!offer.fundingNano || !offer.priceNano || !offer.serviceFeeNano) {
		return std::nullopt;
	}
	const auto funding = *offer.fundingNano;
	const auto price = *offer.priceNano;
	const auto serviceFee = *offer.serviceFeeNano;
	if (price < 0
		|| serviceFee < 0
		|| funding <= price
		|| funding - price <= serviceFee) {
		return std::nullopt;
	}
	return funding - price - serviceFee;
}

void AddOfferMoneyRow(
		not_null<Ui::TableLayout*> table,
		not_null<Main::Session*> session,
		rpl::producer<QString> label,
		int64 amountNano) {
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto mark = GramMark(helper, table->st().defaultValue.style.font);
	auto value = FiatRateValue(session) | rpl::map([=](const FiatRate &rate) {
		return GramValueText(mark, amountNano, rate, kFiatCurrencyDecimals);
	});
	Ui::AddTableRow(
		table,
		std::move(label),
		object_ptr<Ui::FlatLabel>(
			table,
			std::move(value),
			st::walletDetailsFeeLabel,
			st::defaultPopupMenu,
			helper.context()));
}

} // namespace

bool ShowsOnchainGift(const TransferItem &item) {
	return (item.onchainGift != TransferOnchainGift::None);
}

CollectibleRowLook ComputeCollectibleRowLook(const TransferItem &item) {
	const auto action = item.onchainGift;
	if (!ShowsOnchainGift(item)) {
		return {
			.service = (ShowsGiftMove(item)
				? tr::lng_wallet_row_telegram(tr::now)
				: QString()),
			.kind = CollectibleRowKindText(item),
			.incoming = item.incoming,
		};
	} else if (ShowsOnchainOffer(item)) {
		return ComputeOfferRowLook(item);
	} else if (action == TransferOnchainGift::Reprice) {
		return {
			.service = tr::lng_wallet_row_fragment(tr::now),
			.amountNano = item.onchainOffer.priceNano.value_or(0),
			.incoming = item.incoming,
			.itemAmount = false,
			.neutral = true,
		};
	}
	const auto list = (action == TransferOnchainGift::List);
	return {
		.service = tr::lng_wallet_row_fragment(tr::now),
		.amountNano = (list
			? item.onchainOffer.priceNano.value_or(0)
			: item.amountNano),
		.incoming = !list,
	};
}

void SetCollectibleChipSubtitle(
		Ui::Text::String &text,
		const CollectibleView &view,
		TransferOnchainGift action,
		int64 amountNano) {
	const auto &textStyle = st::walletRowDateLabel.style;
	const auto phrase = ChipPricedPhrase(action, amountNano);
	if (!phrase) {
		text.setText(textStyle, CollectibleChipFixedText(action, view));
		return;
	}
	auto helper = Ui::Text::CustomEmojiHelper();
	auto amount = GramMark(helper, textStyle.font);
	amount.append(QChar(' '));
	amount.append(Data::FormatGiftGramAmount(amountNano));
	auto priced = (*phrase)(
		tr::now,
		lt_amount,
		std::move(amount),
		tr::marked);
	text.setMarkedText(
		textStyle,
		std::move(priced),
		kMarkupTextOptions,
		helper.context());
}

void AddOnchainGiftPill(
		not_null<Ui::VerticalLayout*> layout,
		const TransferItem &item,
		int bottomSkip) {
	const auto widget = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		style::margins(0, st::walletDetailsOnchainPillSkip, 0, bottomSkip),
		style::al_top);
	const auto pill = widget->lifetime().make_state<OnchainGiftPill>();
	const auto &record = item.collectibleRecord;
	if (record && record->gift && record->gift->unique) {
		pill->backdrop = record->gift->unique->backdrop;
	}
	const auto &textStyle = st::uniqueGiftOnSale.style;
	pill->text.setText(textStyle, OnchainGiftPillText(item.onchainGift));
	const auto &padding = st::giftBoxReleasedByMargin;
	const auto available = st::boxWideWidth
		- st::giveawayGiftCodeTableMargin.left()
		- st::giveawayGiftCodeTableMargin.right();
	const auto width = std::min(
		pill->text.maxWidth() + padding.left() + padding.right(),
		available);
	widget->resize(
		width,
		textStyle.font->height + padding.top() + padding.bottom());
	widget->setNaturalWidth(width);
	widget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(widget);
		PaintOnchainGiftPill(p, widget->rect(), *pill);
	}, widget->lifetime());
}

void AddOnchainGiftDetailsRows(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const TransferItem &item) {
	const auto &offer = item.onchainOffer;
	const auto money = [&](
			rpl::producer<QString> label,
			std::optional<int64> amountNano) {
		if (amountNano) {
			AddOfferMoneyRow(table, session, std::move(label), *amountNano);
		}
	};
	if (item.onchainGift == TransferOnchainGift::Reprice) {
		money(tr::lng_wallet_details_onchain_old_price(), offer.oldPriceNano);
		money(tr::lng_wallet_details_onchain_new_price(), offer.priceNano);
	} else {
		money(tr::lng_wallet_details_onchain_price(), offer.priceNano);
	}
	money(tr::lng_wallet_details_onchain_service_fee(), offer.serviceFeeNano);
	money(tr::lng_wallet_details_onchain_escrow(), OfferEscrowNano(offer));
	if (const auto expiresAt = offer.expiresAt) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_onchain_expires(),
			rpl::single(tr::marked(
				langDateTime(base::unixtime::parse(*expiresAt)))));
	}
	money(tr::lng_wallet_details_onchain_refund(), offer.refundNano);
	money(tr::lng_wallet_details_onchain_fine(), offer.fineNano);
	const auto address = item.counterparty.isEmpty()
		? QString()
		: FormatFriendly(item.counterparty, item.counterpartyBounceable);
	if (!address.isEmpty()) {
		Ui::AddTableRow(
			table,
			((item.onchainGift == TransferOnchainGift::OfferAcceptSeller)
				? tr::lng_wallet_details_onchain_buyer()
				: tr::lng_wallet_details_onchain_seller()),
			AddressValueLabel(table, std::move(show), address));
	}
}

} // namespace Wallet
