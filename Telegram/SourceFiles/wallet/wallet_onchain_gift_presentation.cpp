/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_onchain_gift_presentation.h"

#include "data/data_star_gift.h"
#include "lang/lang_keys.h"
#include "ui/controls/ton_common.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_content.h"
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

} // namespace

bool ShowsOnchainGift(const TransferItem &item) {
	return (item.onchainGift != TransferOnchainGift::None);
}

CollectibleRowLook ComputeCollectibleRowLook(const TransferItem &item) {
	if (!ShowsOnchainGift(item)) {
		return {
			.service = (ShowsGiftMove(item)
				? tr::lng_wallet_row_telegram(tr::now)
				: QString()),
			.kind = CollectibleRowKindText(item),
			.incoming = item.incoming,
		};
	}
	return {
		.service = tr::lng_wallet_row_fragment(tr::now),
		.amountNano = item.amountNano,
		.incoming = (item.onchainGift != TransferOnchainGift::List),
	};
}

void SetCollectibleChipSubtitle(
		Ui::Text::String &text,
		const CollectibleView &view,
		TransferOnchainGift action,
		int64 amountNano) {
	const auto &textStyle = st::walletRowDateLabel.style;
	if (action != TransferOnchainGift::Buy) {
		text.setText(
			textStyle,
			((action == TransferOnchainGift::List)
				? tr::lng_wallet_row_onchain_listed(tr::now)
				: (action == TransferOnchainGift::Delist)
				? tr::lng_wallet_row_onchain_delisted(tr::now)
				: CollectibleKindText(view)));
		return;
	}
	auto helper = Ui::Text::CustomEmojiHelper();
	auto amount = GramMark(helper, textStyle.font);
	amount.append(QChar(' '));
	amount.append(Ui::FormatTonAmount(amountNano).full);
	text.setMarkedText(
		textStyle,
		tr::lng_wallet_row_onchain_bought(
			tr::now,
			lt_amount,
			std::move(amount),
			tr::marked),
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

} // namespace Wallet
