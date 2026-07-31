/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_section.h"

#include "base/event_filter.h"
#include "base/unixtime.h"
#include "calls/group/calls_group_common.h"
#include "core/credits_amount.h"
#include "core/ton_explorer_url.h"
#include "data/data_user.h"
#include "gram/api/gram_api_history.h"
#include "gram/crypto/gram_mnemonic.h"
#include "gram/ton/gram_address.h"
#include "gram/ton/gram_transfer_link.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "profile/profile_back_button.h"
#include "qr/qr_generate.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/emoji_stake_box.h"
#include "ui/controls/feature_list.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_session.h"
#include "window/window_session_controller.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QTextEdit>

namespace Wallet {
namespace {

constexpr auto kAddressLength = 48;
constexpr auto kAddressGroup = 4;
constexpr auto kAddressGroupsPerLine = 6;
constexpr auto kDetailsGroupsPerLine = 4;
constexpr auto kReceiveGroupsPerLine = 3;
constexpr auto kReceiveLines = kAddressLength
	/ kAddressGroup
	/ kReceiveGroupsPerLine;
constexpr auto kShortAddressChars = 4;
constexpr auto kMinus = QChar(0x2212);
constexpr auto kImportWordCountShort = 12;
constexpr auto kImportWordCountLong = 24;
constexpr auto kImportSuggestionsLimit = 3;
constexpr auto kIntroTooltipShownPref = "wallet_intro_tooltip_shown"_cs;
constexpr auto kIntroToastShownPref = "wallet_intro_toast_shown"_cs;
constexpr auto kIntroToastDuration = 4 * crl::time(1000);
constexpr auto kCommentMaxBytes = 960;

class Card final : public Ui::RpWidget {
public:
	Card(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void refreshAddress();
	void setupBalance();
	void setupQr();
	void updateLayout();

	const not_null<Window::SessionController*> _controller;
	Ui::FlatLabel *_major = nullptr;
	Ui::FlatLabel *_minor = nullptr;
	Ui::FlatLabel *_ticker = nullptr;
	Ui::FlatLabel *_fiat = nullptr;
	Ui::AbstractButton *_qr = nullptr;
	QString _name;
	QString _addressLine1;
	QString _addressLine2;

};

[[nodiscard]] QString GroupedAddressLine(
		const QString &address,
		int offset) {
	auto groups = QStringList();
	for (auto i = 0; i != kAddressGroupsPerLine; ++i) {
		groups.append(address.mid(
			offset + i * kAddressGroup,
			kAddressGroup));
	}
	return groups.join(QChar(' '));
}

[[nodiscard]] TextWithEntities DetailsAddressValue(
		const QString &address) {
	auto result = tr::marked();
	const auto perLine = kAddressGroup * kDetailsGroupsPerLine;
	for (auto offset = 0; offset < address.size(); offset += perLine) {
		auto groups = QStringList();
		for (auto i = 0; i != kDetailsGroupsPerLine; ++i) {
			groups.append(address.mid(
				offset + i * kAddressGroup,
				kAddressGroup));
		}
		if (!result.empty()) {
			result.append(QChar('\n'));
		}
		result.append(Ui::Text::Wrapped(
			{ groups.join(QChar(' ')) },
			EntityType::Code,
			{}));
	}
	return result;
}

[[nodiscard]] object_ptr<Ui::FlatLabel> AddressValueLabel(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(DetailsAddressValue(address)),
		st::walletDetailsAddressLabel);
	result->setClickHandlerFilter([=](const auto &...) {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
		return false;
	});
	return result;
}

void SetBalanceText(not_null<Ui::FlatLabel*> label, CreditsAmount amount) {
	auto helper = Ui::Text::CustomEmojiHelper();
	auto icon = helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyColored(
				st::walletCardBalanceMajorLabel.style.font,
				st::walletCardBalanceMajorLabel.textFg->c);
		},
		.margin = st::walletCardIconMargin
	});
	label->setMarkedText(
		icon.append(Info::ChannelEarn::MajorPart(amount)),
		helper.context());
}

struct HistoryRowContent {
	QString title;
	QString subtitle;
	QString date;
	int64 amountNano = 0;
	bool incoming = false;
	bool pending = false;
};

[[nodiscard]] QString ShortAddressForm(const QString &full) {
	return full.left(kShortAddressChars)
		+ QChar(0x2026)
		+ full.right(kShortAddressChars);
}

[[nodiscard]] QString ShortAddress(const Gram::Address &address) {
	if (address.hash.isEmpty()) {
		return QString();
	}
	const auto full = Gram::FormatFriendly(address, true);
	return ShortAddressForm(full);
}

void SetRowAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		bool incoming,
		bool pending) {
	const auto amount = CreditsAmount(
		amountNano / Ui::kNanosInOne,
		amountNano % Ui::kNanosInOne,
		CreditsType::Ton);
	major->setText((incoming ? QChar('+') : kMinus)
		+ Info::ChannelEarn::MajorPart(amount));
	auto helper = Ui::Text::CustomEmojiHelper();
	auto minorText = tr::marked(Info::ChannelEarn::MinorPart(amount));
	minorText.append(helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyColored(
				st::walletRowAmountMajorLabel.style.font,
				st::windowActiveTextFg->c);
		},
		.margin = st::walletRowIconMargin,
	}));
	minor->setMarkedText(std::move(minorText), helper.context());
	const auto color = pending
		? st::windowSubTextFg->c
		: incoming
		? st::boxTextFgGood->c
		: st::windowFg->c;
	major->setTextColorOverride(color);
	minor->setTextColorOverride(color);
}

void AddHistoryRow(
		not_null<Ui::VerticalLayout*> list,
		const HistoryRowContent &content,
		Fn<void()> clicked) {
	const auto wrap = list->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			list,
			object_ptr<Ui::VerticalLayout>(list),
			st::walletRowPadding));
	const auto inner = wrap->entity();
	inner->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto title = inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		content.title,
		st::walletRowTitleLabel));
	if (!content.subtitle.isEmpty()) {
		Ui::AddSkip(inner, st::walletRowSkip);
		inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			content.subtitle,
			st::walletRowSubtitleLabel));
	}
	Ui::AddSkip(inner, st::walletRowSkip);
	inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		content.date,
		st::walletRowDateLabel));

	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMajorLabel);
	major->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMinorLabel);
	minor->setAttribute(Qt::WA_TransparentForMouseEvents);
	SetRowAmount(
		major,
		minor,
		content.amountNano,
		content.incoming,
		content.pending);
	const auto circle = Ui::CreateChild<Ui::RpWidget>(wrap);
	circle->resize(st::walletRowIconSize, st::walletRowIconSize);
	circle->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto icon = content.incoming
		? &st::walletRowIconIn
		: &st::walletRowIconOut;
	circle->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(circle);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(circle->rect());
		icon->paintInCenter(p, circle->rect());
	}, circle->lifetime());
	const auto button = Ui::CreateChild<Ui::SettingsButton>(
		wrap,
		rpl::single(QString()));
	button->setClickedCallback(std::move(clicked));
	Ui::ToggleChildrenVisibility(wrap, true);
	wrap->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		circle->moveToLeft(
			st::walletRowIconLeft,
			(g.height() - circle->height()) / 2);
		const auto majorTop = st::walletRowPadding.top()
			+ (title->height() - major->height()) / 2;
		minor->moveToRight(
			st::walletRowPadding.right(),
			majorTop + st::walletRowAmountMinorSkip);
		major->moveToRight(
			st::walletRowPadding.right() + minor->width(),
			majorTop);
		button->resize(g.size());
		button->lower();
	}, wrap->lifetime());
}

[[nodiscard]] HistoryRowContent RowContentFromItem(
		const Gram::TransferItem &item) {
	const auto pending
		= (item.status == Gram::TransferItem::Status::Pending);
	const auto hasCounterparty = !item.counterparty.hash.isEmpty();
	const auto kindText = (item.kind
		== Gram::TransferItem::Kind::ContractInteraction)
		? tr::lng_wallet_row_contract(tr::now)
		: item.incoming
		? tr::lng_wallet_row_incoming(tr::now)
		: tr::lng_wallet_row_outgoing(tr::now);
	return {
		.title = (hasCounterparty
			? ShortAddress(item.counterparty)
			: kindText),
		.subtitle = (pending
			? tr::lng_wallet_row_pending(tr::now)
			: hasCounterparty
			? kindText
			: QString()),
		.date = langDateTime(base::unixtime::parse(item.date)),
		.amountNano = item.amountNano,
		.incoming = item.incoming,
		.pending = pending,
	};
}

[[nodiscard]] HistoryRowContent RowContentFromPending(
		const PendingSend &pending) {
	return {
		.title = ShortAddress(pending.destination),
		.subtitle = tr::lng_wallet_row_pending(tr::now),
		.date = langDateTime(base::unixtime::parse(pending.posted)),
		.amountNano = pending.amountNano,
		.incoming = false,
		.pending = true,
	};
}

[[nodiscard]] Gram::TransferItem ItemFromPending(
		const PendingSend &pending) {
	auto result = Gram::TransferItem();
	result.incoming = false;
	result.counterparty = pending.destination;
	result.amountNano = pending.amountNano;
	result.comment = pending.comment;
	result.date = pending.posted;
	result.status = Gram::TransferItem::Status::Pending;
	return result;
}

void AddDetailsAmountHeader(
		not_null<Ui::GenericBox*> box,
		const Gram::TransferItem &item) {
	const auto container = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(
			0,
			st::boxTitleHeight + st::walletDetailsAmountTopSkip,
			0,
			st::walletDetailsAmountBottomSkip),
		style::al_top);
	const auto formatted = Ui::FormatTonAmount(item.amountNano);
	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		container,
		(item.incoming ? QChar('+') : kMinus) + formatted.wholeString,
		st::walletDetailsAmountMajorLabel);
	auto helper = Ui::Text::CustomEmojiHelper();
	auto minorText = formatted.nanoString.isEmpty()
		? tr::marked()
		: tr::marked(formatted.separator + formatted.nanoString);
	minorText.append(helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyColored(
				st::walletDetailsAmountMajorLabel.style.font,
				st::windowActiveTextFg->c);
		},
		.margin = st::walletDetailsIconMargin,
	}));
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		container,
		st::walletDetailsAmountMinorLabel);
	minor->setMarkedText(std::move(minorText), helper.context());
	const auto pending
		= (item.status == Gram::TransferItem::Status::Pending);
	const auto color = pending
		? st::windowSubTextFg->c
		: item.incoming
		? st::boxTextFgGood->c
		: st::windowFg->c;
	major->setTextColorOverride(color);
	minor->setTextColorOverride(color);
	rpl::combine(
		major->sizeValue(),
		minor->sizeValue()
	) | rpl::on_next([=](
			const QSize &majorSize,
			const QSize &minorSize) {
		const auto width = majorSize.width() + minorSize.width();
		container->resize(
			width,
			std::max(
				majorSize.height(),
				st::walletDetailsAmountMinorSkip + minorSize.height()));
		container->setNaturalWidth(width);
		major->moveToLeft(0, 0, width);
		minor->moveToLeft(
			majorSize.width(),
			st::walletDetailsAmountMinorSkip,
			width);
	}, container->lifetime());
}

[[nodiscard]] object_ptr<Ui::PaddingWrap<Ui::FlatLabel>> MakeCommentBubble(
		not_null<QWidget*> parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		parent,
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(text),
			st::walletCommentLabel),
		st::giveawayGiftCodeValueMargin);
	const auto raw = result.data();
	const auto bg = raw->lifetime().make_state<Ui::RoundRect>(
		st::boxRadius,
		st::windowBgOver);
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		bg->paint(p, raw->rect());
	}, raw->lifetime());
	return result;
}

void AddDetailsComment(
		not_null<Ui::GenericBox*> box,
		const Gram::TransferItem &item) {
	const auto comment = item.comment.trimmed();
	if (comment.isEmpty()) {
		return;
	}
	box->addRow(
		MakeCommentBubble(box, rpl::single(comment)),
		style::margins(
			st::giveawayGiftCodeTableMargin.left(),
			0,
			st::giveawayGiftCodeTableMargin.right(),
			st::walletDetailsAmountBottomSkip),
		style::al_top);
}

void AddDetailsTable(
		not_null<Ui::GenericBox*> box,
		const Gram::TransferItem &item) {
	const auto table = box->addRow(
		object_ptr<Ui::TableLayout>(
			box,
			st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	if (!item.counterparty.hash.isEmpty()) {
		const auto address = Gram::FormatFriendly(item.counterparty, true);
		Ui::AddTableRow(
			table,
			(item.incoming
				? tr::lng_wallet_details_sender()
				: tr::lng_wallet_details_recipient()),
			AddressValueLabel(table, box->uiShow(), address));
	}
	const auto pending
		= (item.status == Gram::TransferItem::Status::Pending);
	if (item.feeNano > 0 && !pending) {
		auto helper = Ui::Text::CustomEmojiHelper();
		auto fee = helper.paletteDependent({
			.factory = [] {
				return Ui::Earn::IconCurrencyColored(
					st::defaultTableValue.style.font,
					st::windowActiveTextFg->c);
			},
		});
		fee.append(QChar(' '));
		fee.append(Ui::FormatTonAmount(item.feeNano).full);
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_fee(),
			rpl::single(std::move(fee)),
			helper.context());
	}
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_date(),
		rpl::single(tr::marked(
			langDateTime(base::unixtime::parse(item.date)))));
}

void AddBoxCloseButton(not_null<Ui::GenericBox*> box) {
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
}

[[nodiscard]] QImage ReceiveQrCenter(int side) {
	auto result = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto svg = QSvgRenderer(
		Ui::Earn::CurrencySvgColored(st::windowActiveTextFg->c));
	svg.render(&p, QRectF(0, 0, side, side));
	return result;
}

[[nodiscard]] QImage ReceiveQrImage(const QString &address) {
	const auto data = Qr::Encode(address, Qr::Redundancy::Quartile);
	const auto ratio = style::DevicePixelRatio();
	const auto pixel = std::max(
		st::walletReceiveQrSize / std::max(data.size, 1),
		1);
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);
	image = Qr::ReplaceCenter(
		std::move(image),
		ReceiveQrCenter(Qr::ReplaceSize(data, pixel * ratio)));
	image.setDevicePixelRatio(ratio);
	return image;
}

void WalletReceiveBox(
		not_null<Ui::GenericBox*> box,
		const QString &address) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		rpl::variable<bool> qrShown = true;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto image = ReceiveQrImage(address);
	const auto qrSide = image.width() / style::DevicePixelRatio();
	const auto plateSide = qrSide + 2 * st::introQrBackgroundSkip;
	const auto panelHeight = st::walletReceivePanelPadding.top()
		+ plateSide
		+ st::walletReceiveActionSkip
		+ st::defaultLightButton.height
		+ st::walletReceivePanelPadding.bottom();

	const auto panel = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, panelHeight),
		st::walletReceivePanelMargin,
		style::al_justify);
	panel->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(panel);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			panel->rect(),
			st::walletReceivePanelRadius,
			st::walletReceivePanelRadius);
	}, panel->lifetime());

	const auto addSide = [&](rpl::producer<bool> shown) {
		const auto wrap = Ui::CreateChild<Ui::FadeWrap<Ui::RpWidget>>(
			panel,
			object_ptr<Ui::RpWidget>(panel));
		const auto inner = wrap->entity();
		panel->sizeValue(
		) | rpl::on_next([=](QSize size) {
			inner->resize(size);
		}, inner->lifetime());
		wrap->toggleOn(std::move(shown));
		return inner;
	};
	const auto addAction = [](
			not_null<Ui::RpWidget*> side,
			rpl::producer<QString> text,
			Fn<void()> callback) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			side.get(),
			std::move(text),
			st::defaultLightButton);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setFullRadius(true);
		button->setClickedCallback(std::move(callback));
		button->show();
		side->sizeValue(
		) | rpl::on_next([=](QSize size) {
			const auto &padding = st::walletReceivePanelPadding;
			button->setFullWidth(
				size.width() - padding.left() - padding.right());
			button->moveToLeft(
				padding.left(),
				(size.height()
					- padding.bottom()
					- st::defaultLightButton.height),
				size.width());
		}, button->lifetime());
	};

	const auto qrInner = addSide(state->qrShown.value());
	qrInner->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(qrInner);
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (qrInner->width() - plateSide) / 2;
		const auto top = st::walletReceivePanelPadding.top();
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			QRect(left, top, plateSide, plateSide),
			st::introQrBackgroundRadius,
			st::introQrBackgroundRadius);
		p.drawImage(
			left + st::introQrBackgroundSkip,
			top + st::introQrBackgroundSkip,
			image);
	}, qrInner->lifetime());
	addAction(qrInner, tr::lng_wallet_qr_copy(), [=, show = box->uiShow()] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
		state->qrShown = false;
	});

	const auto font = st::walletReceiveAddressFont->monospace();
	const auto groupWidth = font->width(address.left(kAddressGroup));
	const auto spaceWidth = font->width(QChar(' '));
	const auto lineWidth = kReceiveGroupsPerLine * groupWidth
		+ (kReceiveGroupsPerLine - 1) * spaceWidth;
	const auto lineHeight = font->height + st::walletReceiveAddressLineSkip;
	const auto blockHeight = kReceiveLines * font->height
		+ (kReceiveLines - 1) * st::walletReceiveAddressLineSkip;
	const auto rowHeight = std::max(
		st::walletReceiveCopiedIcon.height(),
		st::normalFont->height);
	const auto contentTop = st::walletReceivePanelPadding.top()
		+ (plateSide
			- blockHeight
			- st::walletReceiveCopiedTopSkip
			- rowHeight) / 2;

	const auto textInner = addSide(state->qrShown.value(
	) | rpl::map([](bool shown) {
		return !shown;
	}));
	textInner->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(textInner);
		p.setFont(font);
		const auto left = (textInner->width() - lineWidth) / 2;
		for (auto i = 0; i != kAddressLength / kAddressGroup; ++i) {
			const auto line = i / kReceiveGroupsPerLine;
			const auto column = i % kReceiveGroupsPerLine;
			p.setPen((i % 2) ? st::windowSubTextFg : st::windowFg);
			p.drawText(
				left + column * (groupWidth + spaceWidth),
				contentTop + line * lineHeight + font->ascent,
				address.mid(i * kAddressGroup, kAddressGroup));
		}
		const auto &icon = st::walletReceiveCopiedIcon;
		const auto copied = tr::lng_wallet_receive_copied(tr::now);
		const auto rowWidth = icon.width()
			+ st::walletReceiveCopiedSkip
			+ st::normalFont->width(copied);
		const auto rowLeft = (textInner->width() - rowWidth) / 2;
		const auto rowTop = contentTop
			+ blockHeight
			+ st::walletReceiveCopiedTopSkip;
		icon.paint(
			p,
			rowLeft,
			rowTop + (rowHeight - icon.height()) / 2,
			textInner->width());
		p.setFont(st::normalFont);
		p.setPen(st::windowActiveTextFg);
		p.drawText(
			rowLeft + icon.width() + st::walletReceiveCopiedSkip,
			rowTop
				+ (rowHeight - st::normalFont->height) / 2
				+ st::normalFont->ascent,
			copied);
	}, textInner->lifetime());
	addAction(
		textInner,
		tr::lng_wallet_receive_show_qr(),
		[=] { state->qrShown = true; });

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_receive_about(),
			st::walletBoxAboutLabel),
		st::walletReceiveAboutMargin,
		style::al_top);

	AddBoxCloseButton(box);
}

void ShowWalletReceiveBox(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Ui::Show> show) {
	auto &wallet = controller->session().wallet();
	const auto address = wallet.addressFriendly(false);
	if (address.size() != kAddressLength) {
		return;
	}
	show->showBox(Box(WalletReceiveBox, address));
}

void WalletHowItWorksBox(not_null<Ui::GenericBox*> box) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	box->addRow(
		Calls::Group::MakeRoundActiveLogo(
			box,
			st::walletHowLogoIcon,
			st::walletHowLogoPadding),
		st::walletHowLogoMargin);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_how_title(),
			st::walletPhraseTitleLabel),
		st::boxRowPadding,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_how_subtitle(),
			st::walletPhraseTextLabel),
		st::walletPhraseTextMargin,
		style::al_top);

	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			st::walletAboutInstantIcon,
			tr::lng_wallet_about_instant_title(tr::now),
			tr::lng_wallet_about_instant_text(tr::now, tr::marked),
		},
		{
			st::walletAboutFeesIcon,
			tr::lng_wallet_about_fees_title(tr::now),
			tr::lng_wallet_about_fees_text(tr::now, tr::marked),
		},
		{
			st::walletAboutChainIcon,
			tr::lng_wallet_about_chain_title(tr::now),
			tr::lng_wallet_about_chain_text(tr::now, tr::marked),
		},
	};
	for (const auto &feature : features) {
		box->addRow(Ui::MakeFeatureListEntry(box, feature));
	}

	AddBoxCloseButton(box);

	box->addButton(tr::lng_wallet_how_button(), [=] { box->closeBox(); });
}

[[nodiscard]] TextWithEntities WalletIntroText(const QString &text) {
	return Ui::Text::IconEmoji(&st::walletIntroEmoji).append(text);
}

void SetupIntroTooltip(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> card) {
	struct State {
		Ui::ImportantTooltip *tooltip = nullptr;
		bool dismissed = false;
	};
	const auto state = parent->lifetime().make_state<State>();
	state->tooltip = Ui::CreateChild<Ui::ImportantTooltip>(
		parent.get(),
		Ui::MakeTooltipWithClose(
			parent,
			tr::lng_wallet_intro_text() | rpl::map(WalletIntroText),
			st::walletIntroTooltipMaxWidth,
			st::defaultImportantTooltipLabel,
			st::importantTooltipHide,
			st::defaultImportantTooltip.padding,
			[=] {
				state->dismissed = true;
				state->tooltip->toggleAnimated(false);
			}),
		st::historyRecordTooltip);
	state->tooltip->toggleFast(false);

	rpl::combine(
		card->geometryValue(),
		parent->widthValue()
	) | rpl::on_next([=](const QRect &geometry, int width) {
		if (state->dismissed || geometry.isEmpty() || !width) {
			return;
		}
		const auto area = Ui::MapFrom(parent, card, card->rect());
		const auto countPosition = [=](QSize size) {
			return QPoint(
				area.x() + (area.width() - size.width()) / 2,
				area.y()
					+ st::walletCardBalanceTop
					+ st::walletCardQrSize
					+ st::walletIntroTooltipSkip);
		};
		state->tooltip->pointAt(area, RectPart::Bottom, countPosition);
		state->tooltip->toggleFast(true);
		state->tooltip->updateGeometry();
	}, parent->lifetime());
}

[[nodiscard]] QString ExplorerTransactionUrl(
		not_null<Main::Session*> session,
		const QByteArray &traceId) {
	if (traceId.isEmpty()) {
		return QString();
	}
	return Core::TonExplorerUrl(
		session,
		u"transaction/"_q + QString::fromLatin1(traceId.toHex()));
}

void WalletTransactionBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Gram::TransferItem item) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddDetailsAmountHeader(box, item);
	AddDetailsComment(box, item);
	AddDetailsTable(box, item);

	AddBoxCloseButton(box);
	const auto toggle = box->addTopButton(st::boxTitleMenu);
	const auto menu = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	const auto url = ExplorerTransactionUrl(session, item.traceId);
	const auto show = box->uiShow();
	toggle->setClickedCallback([=] {
		if (*menu) {
			return;
		}
		*menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto raw = menu->get();
		raw->setDestroyedCallback(crl::guard(toggle, [=] {
			toggle->setForceRippled(false);
		}));
		toggle->setForceRippled(true);
		if (!url.isEmpty()) {
			raw->addAction(
				Ui::Text::FixAmpersandInAction(
					tr::lng_wallet_details_explorer(tr::now)),
				[=] { UrlClickHandler::Open(url); },
				&st::menuIconSearch);
		}
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_details_gram(tr::now)),
			[=] { show->showBox(Box(WalletHowItWorksBox)); },
			&st::menuIconFaq);
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		raw->popup(toggle->mapToGlobal(QPoint(
			toggle->width(),
			toggle->height())));
	});

	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

void ShowWalletTransactionBox(
		not_null<Window::SessionController*> controller,
		const Gram::TransferItem &item) {
	controller->show(Box(WalletTransactionBox, &controller->session(), item));

	const auto local = &controller->session().local();
	if (local->readPref<bool>(kIntroToastShownPref)) {
		return;
	}
	local->writePref<bool>(kIntroToastShownPref, true);
	controller->showToast({
		.text = WalletIntroText(tr::lng_wallet_intro_text(tr::now)),
		.duration = kIntroToastDuration,
	});
}

[[nodiscard]] int CommentBytes(const QString &text) {
	return int(text.trimmed().toUtf8().size());
}

[[nodiscard]] bool CommentFits(const QString &text) {
	return CommentBytes(text) <= kCommentMaxBytes;
}

struct SendFlow {
	Gram::Address destination;
	bool bounce = true;
	QString displayForm;
	int64 amountNano = 0;
	int64 feeNano = 0;
	bool feeApproximate = true;
	QString comment;
};

[[nodiscard]] std::optional<SendFlow> ParseRecipientFlow(
		const QString &text) {
	auto address = text;
	auto amountNano = int64(0);
	auto comment = QString();
	if (const auto link = Gram::ParseTransferLink(text)) {
		address = link->address;
		amountNano = link->amountNano;
		comment = link->comment.trimmed();
	}
	const auto parsed = Gram::ParseAddress(address);
	if (!parsed || parsed->testnet) {
		return std::nullopt;
	}
	return SendFlow{
		.destination = parsed->address,
		.bounce = parsed->bounceable,
		.displayForm = (parsed->friendly
			? address
			: Gram::FormatFriendly(parsed->address, parsed->bounceable)),
		.amountNano = amountNano,
		.comment = (CommentFits(comment) ? comment : QString()),
	};
}

not_null<Ui::FlatLabel*> AddSendFlowLabel(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text,
		const style::FlatLabel &st) {
	return box->addRow(
		object_ptr<Ui::FlatLabel>(box, std::move(text), st),
		style::margins(
			st::boxRowPadding.left(),
			st::walletSendRowSkip,
			st::boxRowPadding.right(),
			0),
		style::al_top);
}

[[nodiscard]] not_null<Ui::InputField*> AddCommentField(
		not_null<Ui::GenericBox*> box,
		const QString &comment) {
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::walletCommentField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_wallet_comment_placeholder(),
			comment),
		st::walletCommentFieldMargin);
	Ui::AddLengthLimitLabel(field, kCommentMaxBytes, {
		.customCharactersCount = [=] {
			return CommentBytes(field->getLastText());
		},
	});
	field->setMaxLength(-1);
	return field;
}

void WalletSendConfirmBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		SendFlow flow,
		Fn<void(QString)> commentEdited) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	auto item = Gram::TransferItem();
	item.incoming = false;
	item.amountNano = flow.amountNano;
	item.status = Gram::TransferItem::Status::Success;
	AddDetailsAmountHeader(box, item);

	const auto table = box->addRow(
		object_ptr<Ui::TableLayout>(
			box,
			st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_send_address_label(),
		AddressValueLabel(table, box->uiShow(), flow.displayForm));
	auto helper = Ui::Text::CustomEmojiHelper();
	auto fee = helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyColored(
				st::defaultTableValue.style.font,
				st::windowActiveTextFg->c);
		},
	});
	fee.append(QChar(' '));
	if (flow.feeApproximate) {
		fee.append(QChar('~'));
	}
	fee.append(Ui::FormatTonAmount(flow.feeNano).full);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_fee(),
		rpl::single(std::move(fee)),
		helper.context());
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_date(),
		rpl::single(tr::marked(
			langDateTime(base::unixtime::parse(base::unixtime::now())))));

	const auto field = AddCommentField(box, flow.comment);
	field->changes() | rpl::on_next([=] {
		const auto text = field->getLastText().trimmed();
		if (CommentFits(text)) {
			commentEdited(text);
		}
	}, field->lifetime());

	struct State {
		rpl::variable<bool> confirmButtonBusy = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto wallet = &controller->session().wallet();
	const auto weak = base::make_weak(box.get());
	const auto weakController = base::make_weak(controller.get());
	const auto submit = [=] {
		if (state->confirmButtonBusy.current()) {
			return;
		}
		const auto text = field->getLastText().trimmed();
		if (!CommentFits(text)) {
			field->showError();
			return;
		}
		state->confirmButtonBusy = true;
		auto args = SendArgs{
			.destination = flow.destination,
			.amountNano = flow.amountNano,
			.comment = text,
			.bounce = flow.bounce,
		};
		wallet->send(args, [=](QString error) {
			if (!error.isEmpty()) {
				if (weak.get()) {
					state->confirmButtonBusy = false;
				}
				if (const auto strong = weakController.get()) {
					strong->showToast(error);
				}
				return;
			}
			const auto strong = weakController.get();
			if (!strong) {
				return;
			}
			strong->hideLayer();
			strong->showToast(tr::lng_wallet_sent_toast(
				tr::now,
				lt_address,
				ShortAddressForm(flow.displayForm)));
			if (const auto &pending = wallet->pendingSend()) {
				ShowWalletTransactionBox(strong, ItemFromPending(*pending));
			}
		});
	};
	const auto button = box->addButton(rpl::combine(
		tr::lng_wallet_send_amount(
			lt_amount,
			rpl::single(Ui::FormatTonAmount(flow.amountNano).full)),
		state->confirmButtonBusy.value()
	) | rpl::map([](QString &&text, bool busy) {
		return busy ? QString() : std::move(text);
	}), submit);
	{
		using namespace Info::Statistics;
		const auto loading = InfiniteRadialAnimationWidget(
			button,
			st::giveawayGiftCodeBoxButton.height / 2);
		AddChildToWidgetCenter(button.data(), loading);
		loading->showOn(state->confirmButtonBusy.value());
	}

	AddBoxCloseButton(box);
}

void SetButtonDisabledLook(
		not_null<Ui::RoundButton*> button,
		bool disabled) {
	button->setBrushOverride(disabled
		? std::optional(st::windowSubTextFg)
		: std::nullopt);
	button->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
}

void WalletCommentBox(
		not_null<Ui::GenericBox*> box,
		const QString &comment,
		Fn<void(QString)> done) {
	box->setTitle(tr::lng_wallet_comment_title());
	box->setWidth(st::boxWideWidth);

	const auto field = AddCommentField(box, comment);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_comment_public(),
			st::walletBoxAboutLabel),
		st::walletPhraseTextMargin,
		style::al_top);
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	const auto submit = [=] {
		const auto text = field->getLastText().trimmed();
		if (!CommentFits(text)) {
			field->showError();
			return;
		}
		box->closeBox();
		done(text);
	};
	field->submits() | rpl::on_next(submit, field->lifetime());
	box->addButton(tr::lng_wallet_comment_add(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void WalletSendAmountBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		SendFlow flow) {
	box->setTitle(tr::lng_wallet_send_to_title(
		lt_address,
		rpl::single(ShortAddressForm(flow.displayForm))));
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	AddBoxCloseButton(box);
	const auto toggle = box->addTopButton(st::boxTitleMenu);

	const auto wallet = &controller->session().wallet();
	const auto showDeposit = [=] {
		ShowWalletReceiveBox(controller, box->uiShow());
	};
	Ui::AddSkip(box->verticalLayout(), st::walletSendAmountTopSkip);
	const auto field = Ui::AddTonInputField(box->verticalLayout(), {
		.value = flow.amountNano,
	});
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	struct State {
		rpl::variable<int64> amount = 0;
		rpl::variable<int64> fee = 0;
		rpl::variable<bool> insufficient = false;
		rpl::variable<bool> canSend = false;
		rpl::variable<QString> comment;
		base::unique_qptr<Ui::PopupMenu> menu;
		bool feeApproximate = true;
	};
	const auto state = box->lifetime().make_state<State>();
	state->comment = flow.comment;
	const auto setComment = crl::guard(box, [=](QString comment) {
		state->comment = comment;
	});
	const auto updateAmount = [=] {
		state->amount = Ui::ParseTonAmountString(
			field->getLastText()).value_or(0);
	};
	field->changes() | rpl::on_next(updateAmount, field->lifetime());
	updateAmount();

	const auto refreshFee = [=] {
		auto args = SendArgs{
			.destination = flow.destination,
			.amountNano = state->amount.current(),
			.bounce = flow.bounce,
		};
		wallet->estimateFee(args, crl::guard(box, [=](FeeResult result) {
			if (result.error.isEmpty()) {
				state->feeApproximate = result.approximate;
				state->fee = result.feeNano;
			}
		}));
	};
	state->amount.value() | rpl::on_next([=] {
		refreshFee();
	}, box->lifetime());
	state->insufficient = rpl::combine(
		state->amount.value(),
		state->fee.value(),
		wallet->balanceNanoValue(),
		wallet->stateKnownValue()
	) | rpl::map([](int64 amount, int64 fee, int64 balance, bool known) {
		return known && (amount > 0) && (amount > balance - fee);
	});
	state->canSend = rpl::combine(
		state->amount.value(),
		state->insufficient.value(),
		wallet->stateKnownValue()
	) | rpl::map([](int64 amount, bool insufficient, bool known) {
		return known && (amount > 0) && !insufficient;
	});

	const auto error = AddSendFlowLabel(
		box,
		tr::lng_wallet_send_insufficient(),
		st::walletSendErrorLabel);
	error->setVisible(false);
	state->insufficient.value() | rpl::on_next([=](bool insufficient) {
		error->setVisible(insufficient);
	}, error->lifetime());
	const auto depositWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins(),
		style::al_justify);
	const auto depositInner = depositWrap->entity();
	Ui::AddSkip(depositInner, st::walletSendRowSkip);
	const auto deposit = depositInner->add(
		object_ptr<Ui::RoundButton>(
			depositInner,
			tr::lng_wallet_send_deposit(),
			st::walletSendPaste),
		style::margins(),
		style::al_top);
	deposit->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	deposit->setClickedCallback(showDeposit);
	depositWrap->toggleOn(state->insufficient.value());
	depositWrap->finishAnimating();

	const auto commentWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins(),
		style::al_justify);
	const auto commentInner = commentWrap->entity();
	const auto editComment = [=] {
		box->uiShow()->showBox(Box(
			WalletCommentBox,
			state->comment.current(),
			setComment));
	};
	Ui::AddSkip(commentInner, st::walletSendRowSkip);
	const auto bubble = commentInner->add(
		MakeCommentBubble(
			commentInner,
			state->comment.value() | rpl::filter([](const QString &text) {
				return !text.isEmpty();
			})),
		st::boxRowPadding,
		style::al_top);
	bubble->entity()->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto commentButton = Ui::CreateChild<Ui::AbstractButton>(bubble);
	commentButton->setPointerCursor(true);
	commentButton->setClickedCallback(editComment);
	Ui::ToggleChildrenVisibility(bubble, true);
	bubble->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		commentButton->resize(g.size());
		commentButton->lower();
	}, bubble->lifetime());
	commentWrap->toggleOn(state->comment.value(
	) | rpl::map([](const QString &comment) {
		return !comment.isEmpty();
	}));
	commentWrap->finishAnimating();

	AddSendFlowLabel(
		box,
		tr::lng_wallet_send_balance(
			lt_amount,
			wallet->balanceNanoValue() | rpl::map([](int64 nano) {
				return Ui::FormatTonAmount(nano).full;
			})),
		st::walletSendBalanceLabel);

	toggle->setClickedCallback([=] {
		if (state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto raw = state->menu.get();
		raw->setDestroyedCallback(crl::guard(toggle, [=] {
			toggle->setForceRippled(false);
		}));
		toggle->setForceRippled(true);
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_send_deposit(tr::now)),
			showDeposit,
			&st::menuIconAdd);
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_comment_title(tr::now)),
			editComment,
			&st::menuIconChatBubble);
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		raw->popup(toggle->mapToGlobal(QPoint(
			toggle->width(),
			toggle->height())));
	});

	const auto submit = [=] {
		if (!state->canSend.current()) {
			field->showError();
			return;
		}
		auto next = flow;
		next.amountNano = state->amount.current();
		next.feeNano = state->fee.current();
		next.feeApproximate = state->feeApproximate;
		next.comment = state->comment.current();
		box->uiShow()->showBox(Box(
			WalletSendConfirmBox,
			controller,
			next,
			setComment));
	};
	field->submits() | rpl::on_next(submit, field->lifetime());
	const auto button = box->addButton(tr::lng_wallet_send_amount(
		lt_amount,
		state->amount.value() | rpl::map([](int64 amount) {
			return Ui::FormatTonAmount(amount).full;
		})), submit);
	state->canSend.value() | rpl::on_next([=](bool canSend) {
		SetButtonDisabledLook(button.data(), !canSend);
	}, button->lifetime());
}

void WalletSendRecipientBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_wallet_send_title());
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	AddBoxCloseButton(box);

	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		tr::lng_wallet_send_recipient());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::walletSendAddressField,
		Ui::InputField::Mode::SingleLine,
		tr::lng_wallet_send_address()));
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto paste = Ui::CreateChild<Ui::RoundButton>(
		field,
		tr::lng_wallet_send_paste(),
		st::walletSendPaste);
	paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	paste->setClickedCallback([=] {
		field->setText(QGuiApplication::clipboard()->text().trimmed());
		field->setFocusFast();
	});
	field->widthValue(
	) | rpl::on_next([=](int) {
		paste->moveToRight(0, st::walletSendPasteTop);
	}, paste->lifetime());

	const auto error = AddSendFlowLabel(
		box,
		tr::lng_wallet_send_invalid_address(),
		st::walletSendErrorLabel);
	error->setVisible(false);

	struct State {
		std::optional<SendFlow> flow;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto submit = [=] {
		if (!state->flow) {
			field->showError();
			return;
		}
		const auto flow = *state->flow;
		box->closeBox();
		controller->show(Box(WalletSendAmountBox, controller, flow));
	};
	field->submits() | rpl::on_next(submit, field->lifetime());
	const auto button = box->addButton(
		tr::lng_wallet_send_continue(),
		submit);
	field->changes() | rpl::on_next([=] {
		const auto text = field->getLastText().trimmed();
		state->flow = ParseRecipientFlow(text);
		const auto valid = state->flow.has_value();
		error->setVisible(!text.isEmpty() && !valid);
		SetButtonDisabledLook(button.data(), !valid);
	}, field->lifetime());
	SetButtonDisabledLook(button.data(), true);
}

void AddPhraseBoxHeader(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<QString> text) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::walletPhraseTitleLabel),
		st::walletPhraseTitleMargin,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::walletPhraseTextLabel),
		st::walletPhraseTextMargin,
		style::al_top);
}

void WalletPhraseBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto stored = controller->session().local().readWallet();
	if (!stored || stored->words.empty()) {
		box->closeBox();
		return;
	}
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	controller->session().wallet().markPhraseViewed();

	const auto count = int(stored->words.size());
	AddPhraseBoxHeader(
		box,
		tr::lng_wallet_phrase_title(),
		tr::lng_wallet_phrase_text(
			lt_count,
			rpl::single(count * 1.) | tr::to_count()));

	const auto grid = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::walletPhraseGridPadding);
	const auto rows = count / 2;
	auto numberLabels = std::vector<Ui::FlatLabel*>();
	auto wordLabels = std::vector<Ui::FlatLabel*>();
	for (auto i = 0; i != count; ++i) {
		numberLabels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			grid,
			QString::number(i + 1) + QChar('.'),
			st::walletPhraseNumberLabel));
		wordLabels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			grid,
			stored->words[i],
			st::walletPhraseWordLabel));
	}
	const auto rowHeight = wordLabels.front()->height();
	grid->resize(
		grid->width(),
		rows * rowHeight + (rows - 1) * st::walletPhraseRowSkip);
	grid->widthValue(
	) | rpl::on_next([=](int width) {
		const auto column = (width - st::walletPhraseColumnSkip) / 2;
		for (auto i = 0; i != int(wordLabels.size()); ++i) {
			const auto x = (i < rows)
				? 0
				: (column + st::walletPhraseColumnSkip);
			const auto y = (i % rows)
				* (rowHeight + st::walletPhraseRowSkip);
			numberLabels[i]->moveToLeft(x, y, width);
			wordLabels[i]->moveToLeft(
				x + st::walletPhraseNumberWidth,
				y,
				width);
		}
	}, grid->lifetime());

	AddBoxCloseButton(box);
	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
}

void WalletPhraseWarningBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		tr::lng_wallet_phrase_intro_title(),
		tr::lng_wallet_phrase_intro_text());

	const auto container = box->verticalLayout();
	const auto addWarning = [&](
			rpl::producer<TextWithEntities> text,
			const style::icon &icon) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::walletPhraseWarnLabel),
			st::walletPhraseWarnPadding);
		const auto left = Ui::CreateChild<Ui::RpWidget>(container);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon.paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon.size());
		label->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			left->moveToLeft(
				(g.left() - left->width()) / 2,
				g.top() + st::walletPhraseWarnIconSkip);
		}, left->lifetime());
	};
	Ui::AddSkip(container);
	addWarning(
		tr::lng_wallet_phrase_warn_share(tr::rich),
		st::walletPhraseWarnShareIcon);
	Ui::AddSkip(container, st::walletPhraseWarnRowSkip);
	addWarning(
		tr::lng_wallet_phrase_warn_steal(tr::rich),
		st::walletPhraseWarnStealIcon);
	Ui::AddSkip(container, st::walletPhraseWarnRowSkip);
	addWarning(
		tr::lng_wallet_phrase_warn_support(tr::rich),
		st::walletPhraseWarnSupportIcon);
	Ui::AddSkip(container);

	AddBoxCloseButton(box);
	box->addButton(tr::lng_wallet_keys_show_phrase(), [=] {
		const auto stored = controller->session().local().readWallet();
		if (!stored || stored->words.empty()) {
			return;
		}
		box->closeBox();
		controller->show(Box(WalletPhraseBox, controller));
	});
}

void WalletPasscodeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_passcode_check_title());
	const auto &fieldSt = st::settingLocalPasscodeInputField;
	const auto wrap = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::walletPasscodeFieldMargin);
	wrap->resize(wrap->width(), fieldSt.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		wrap,
		fieldSt,
		tr::lng_passcode_enter());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->moveToLeft((width - field->width()) / 2, 0);
	}, wrap->lifetime());
	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::settingLocalPasscodeError),
		st::walletPasscodeErrorMargin,
		style::al_top);
	error->hide();
	QObject::connect(field, &Ui::MaskedInputField::changed, [=] {
		error->hide();
	});
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto submit = [=] {
		if (!passcodeCanTry()) {
			field->setFocus();
			field->showError();
			error->show();
			error->setText(tr::lng_flood_error(tr::now));
			return;
		}
		const auto &domain = controller->session().domain();
		if (domain.local().checkPasscode(field->text().toUtf8())) {
			cSetPasscodeBadTries(0);
			box->closeBox();
			controller->show(Box(WalletPhraseWarningBox, controller));
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(crl::now());
			field->selectAll();
			field->setFocus();
			field->showError();
			error->show();
			error->setText(tr::lng_passcode_wrong(tr::now));
		}
	};
	QObject::connect(field, &Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_passcode_submit(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void WalletRevealFlow(not_null<Window::SessionController*> controller) {
	const auto &domain = controller->session().domain();
	if (domain.local().hasLocalPasscode()) {
		controller->show(Box(WalletPasscodeBox, controller));
	} else {
		controller->show(Box(WalletPhraseWarningBox, controller));
	}
}

[[nodiscard]] QStringList SplitPhraseWords(const QString &text) {
	return text.simplified().split(QChar(' '), Qt::SkipEmptyParts);
}

void WalletImportBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		std::vector<Ui::InputField*> fields;
		std::vector<Ui::RoundButton*> pasteButtons;
		std::vector<Ui::CrossButton*> clearButtons;
		rpl::variable<int> count = kImportWordCountShort;
		rpl::variable<QString> error;
		std::vector<Ui::AbstractButton*> suggestionRows;
		std::vector<QString> suggestionWords;
		int suggestionField = -1;
		int suggestionSelected = 0;
		bool importing = false;
	};
	const auto state = box->lifetime().make_state<State>();

	AddPhraseBoxHeader(
		box,
		tr::lng_wallet_import_title(),
		tr::lng_wallet_import_text());

	const auto toggle = box->addRow(
		object_ptr<Ui::SettingsSlider>(box, st::settingsSlider),
		st::walletImportToggleMargin);
	toggle->setSections({
		tr::lng_wallet_import_words(
			tr::now,
			lt_count,
			kImportWordCountShort),
		tr::lng_wallet_import_words(
			tr::now,
			lt_count,
			kImportWordCountLong),
	});
	toggle->setActiveSectionFast(0);

	const auto addWordField = [=](
			not_null<Ui::VerticalLayout*> container,
			int index) {
		const auto field = container->add(
			object_ptr<Ui::InputField>(
				container,
				st::walletImportField,
				Ui::InputField::Mode::SingleLine),
			st::walletImportFieldMargin);
		const auto number = Ui::CreateChild<Ui::FlatLabel>(
			field,
			QString::number(index + 1) + QChar('.'),
			st::walletPhraseNumberLabel);
		number->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto paste = Ui::CreateChild<Ui::RoundButton>(
			field,
			tr::lng_wallet_send_paste(),
			st::walletSendPaste);
		paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		paste->hide();
		const auto clear = Ui::CreateChild<Ui::CrossButton>(
			field,
			st::walletImportClear);
		field->widthValue(
		) | rpl::on_next([=](int width) {
			number->moveToLeft(
				st::walletImportNumberLeft,
				st::walletImportNumberTop,
				width);
			paste->moveToRight(0, st::walletSendPasteTop);
			clear->moveToRight(
				st::walletImportClearPosition.x(),
				st::walletImportClearPosition.y(),
				width);
		}, field->lifetime());
		state->fields.push_back(field);
		state->pasteButtons.push_back(paste);
		state->clearButtons.push_back(clear);
	};
	for (auto i = 0; i != kImportWordCountShort; ++i) {
		addWordField(box->verticalLayout(), i);
	}
	const auto extraWrap = box->verticalLayout()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box->verticalLayout(),
			object_ptr<Ui::VerticalLayout>(box->verticalLayout())));
	const auto extra = extraWrap->entity();
	for (auto i = kImportWordCountShort; i != kImportWordCountLong; ++i) {
		addWordField(extra, i);
	}
	extraWrap->toggleOn(state->count.value(
	) | rpl::map([](int count) { return count == kImportWordCountLong; }));
	extraWrap->finishAnimating();

	const auto error = AddSendFlowLabel(
		box,
		state->error.value(),
		st::walletImportErrorLabel);
	error->setVisible(false);
	state->error.value() | rpl::on_next([=](const QString &text) {
		error->setVisible(!text.isEmpty());
	}, error->lifetime());

	AddBoxCloseButton(box);

	const auto wordAt = [=](int index) {
		return state->fields[index]->getLastText().trimmed().toLower();
	};
	const auto formValid = [=] {
		const auto count = state->count.current();
		for (auto i = 0; i != count; ++i) {
			const auto word = wordAt(i);
			if (word.isEmpty() || !Gram::IsWordlistWord(word)) {
				return false;
			}
		}
		return true;
	};
	const auto refreshAccessories = [=](int index) {
		const auto field = state->fields[index];
		const auto focused = field->hasFocus();
		const auto empty = field->getLastText().isEmpty();
		state->pasteButtons[index]->setVisible(focused && empty);
		state->clearButtons[index]->toggle(
			focused && !empty,
			anim::type::instant);
	};
	const auto applyCount = [=](int count) {
		if (state->count.current() != count) {
			state->count = count;
		}
		const auto section = (count == kImportWordCountLong) ? 1 : 0;
		if (toggle->activeSection() != section) {
			toggle->setActiveSection(section);
		}
	};
	const auto distributePaste = [=](const QStringList &words) {
		const auto count = int(words.size());
		if (count != kImportWordCountShort
			&& count != kImportWordCountLong) {
			state->error = tr::lng_wallet_import_paste_error(tr::now);
			return;
		}
		applyCount(count);
		for (auto i = 0; i != count; ++i) {
			state->fields[i]->setText(words[i].toLower());
			state->fields[i]->forceProcessContentsChanges();
		}
		state->error = QString();
		const auto last = state->fields[count - 1];
		crl::on_main(last, [=] {
			last->setFocus();
			last->setCursorPosition(last->getLastText().size());
		});
	};
	const auto submit = [=] {
		if (state->importing || !formValid()) {
			return;
		}
		auto &wallet = controller->session().wallet();
		const auto count = state->count.current();
		auto words = std::vector<QString>();
		words.reserve(count);
		for (auto i = 0; i != count; ++i) {
			words.push_back(wordAt(i));
		}
		state->importing = true;
		if (!wallet.import(std::move(words))) {
			state->importing = false;
			state->error = tr::lng_wallet_import_error(tr::now);
			return;
		}
		wallet.startPolling();
		controller->hideLayer();
		controller->showToast({
			.title = tr::lng_wallet_imported_title(tr::now),
			.text = { tr::lng_wallet_imported_text(tr::now) },
			.icon = &st::toastCheckIcon,
		});
	};
	const auto button = box->addButton(
		tr::lng_wallet_import_button(),
		submit);
	const auto updateButton = [=] {
		SetButtonDisabledLook(button.data(), !formValid());
	};
	SetButtonDisabledLook(button.data(), true);

	box->setFocusCallback([=] {
		state->fields.front()->setFocusFast();
	});

	const auto suggestions = Ui::CreateChild<Ui::RpWidget>(box.get());
	suggestions->hide();
	suggestions->setFocusPolicy(Qt::NoFocus);
	suggestions->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(suggestions);
		auto hq = PainterHighQualityEnabler(p);
		const auto inner = suggestions->rect().marginsRemoved(
			st::defaultRoundShadow.extend);
		Ui::Shadow::paint(
			p,
			inner,
			suggestions->width(),
			st::defaultRoundShadow);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(inner, st::boxRadius, st::boxRadius);
	}, suggestions->lifetime());

	const auto hideSuggestions = [=] {
		state->suggestionField = -1;
		state->suggestionWords.clear();
		suggestions->hide();
	};
	const auto repositionSuggestions = [=] {
		const auto index = state->suggestionField;
		if (index < 0) {
			return;
		}
		const auto field = state->fields[index];
		const auto &extend = st::defaultRoundShadow.extend;
		const auto &padding = st::walletImportSuggestionsPadding;
		const auto &rowPadding = st::walletImportSuggestionRowPadding;
		auto textWidth = 0;
		for (const auto &word : state->suggestionWords) {
			textWidth = std::max(textWidth, st::normalFont->width(word));
		}
		const auto innerWidth = std::min(
			padding.left()
				+ rowPadding.left()
				+ textWidth
				+ rowPadding.right()
				+ padding.right(),
			field->width());
		const auto count = int(state->suggestionWords.size());
		const auto innerHeight = padding.top()
			+ count * st::walletImportSuggestionRowHeight
			+ padding.bottom();
		suggestions->resize(
			extend.left() + innerWidth + extend.right(),
			extend.top() + innerHeight + extend.bottom());
		for (auto i = 0; i != int(state->suggestionRows.size()); ++i) {
			const auto row = state->suggestionRows[i];
			row->setVisible(i < count);
			row->setGeometry(
				extend.left() + padding.left(),
				extend.top()
					+ padding.top()
					+ i * st::walletImportSuggestionRowHeight,
				innerWidth - padding.left() - padding.right(),
				st::walletImportSuggestionRowHeight);
		}
		const auto fieldTopLeft = field->mapTo(box, QPoint(0, 0));
		const auto fieldTop = fieldTopLeft.y();
		const auto below = fieldTop
			+ field->height()
			+ st::walletImportSuggestionsSkip;
		const auto flip = (below + innerHeight > box->height());
		const auto top = flip
			? (fieldTop
				- st::walletImportSuggestionsSkip
				- innerHeight
				- extend.top())
			: (below - extend.top());
		suggestions->move(fieldTopLeft.x() - extend.left(), top);
		if (fieldTop + field->height() <= 0 || fieldTop >= box->height()) {
			suggestions->hide();
		} else {
			suggestions->show();
		}
	};
	const auto refreshSuggestions = [=](int index) {
		const auto field = state->fields[index];
		const auto typed = wordAt(index);
		auto words = typed.isEmpty()
			? std::vector<QString>()
			: Gram::WordlistSuggestions(typed, kImportSuggestionsLimit);
		if (!field->hasFocus()
			|| words.empty()
			|| (words.size() == 1 && words.front() == typed)) {
			hideSuggestions();
			return;
		}
		state->suggestionField = index;
		state->suggestionWords = std::move(words);
		state->suggestionSelected = 0;
		suggestions->raise();
		repositionSuggestions();
		suggestions->update();
	};
	const auto acceptSuggestion = [=] {
		const auto index = state->suggestionField;
		if (index < 0
			|| state->suggestionWords.empty()
			|| suggestions->isHidden()) {
			return false;
		}
		const auto selected = std::clamp(
			state->suggestionSelected,
			0,
			int(state->suggestionWords.size()) - 1);
		const auto word = state->suggestionWords[selected];
		const auto field = state->fields[index];
		const auto typed = wordAt(index);
		hideSuggestions();
		if (word == typed) {
			return false;
		}
		field->setText(word);
		field->forceProcessContentsChanges();
		if (index + 1 < state->count.current()) {
			state->fields[index + 1]->setFocus();
		} else {
			field->setCursorPosition(word.size());
		}
		return true;
	};
	const auto moveSuggestionSelection = [=](int delta) {
		const auto count = int(state->suggestionWords.size());
		if (!count) {
			return;
		}
		state->suggestionSelected = std::clamp(
			state->suggestionSelected + delta,
			0,
			count - 1);
		suggestions->update();
	};
	for (auto i = 0; i != kImportSuggestionsLimit; ++i) {
		const auto row = Ui::CreateChild<Ui::AbstractButton>(suggestions);
		row->setPointerCursor(true);
		row->setFocusPolicy(Qt::NoFocus);
		row->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(row);
			if (i == state->suggestionSelected) {
				p.fillRect(row->rect(), st::windowBgOver);
			}
			if (i >= int(state->suggestionWords.size())
				|| state->suggestionField < 0) {
				return;
			}
			const auto &word = state->suggestionWords[i];
			const auto typed = wordAt(state->suggestionField);
			const auto prefix = word.startsWith(typed)
				? typed
				: QString();
			const auto font = st::normalFont;
			p.setFont(font);
			const auto left = st::walletImportSuggestionRowPadding.left();
			const auto baseline = (row->height() - font->height) / 2
				+ font->ascent;
			p.setPen(st::windowSubTextFg);
			p.drawText(left, baseline, prefix);
			p.setPen(st::windowFg);
			p.drawText(
				left + font->width(prefix),
				baseline,
				word.mid(prefix.size()));
		}, row->lifetime());
		row->setClickedCallback([=] {
			state->suggestionSelected = i;
			acceptSuggestion();
		});
		state->suggestionRows.push_back(row);
	}

	toggle->sectionActivated(
	) | rpl::on_next([=](int section) {
		applyCount((section == 1)
			? kImportWordCountLong
			: kImportWordCountShort);
	}, toggle->lifetime());
	state->count.changes() | rpl::on_next([=] {
		state->error = QString();
		updateButton();
		hideSuggestions();
	}, box->lifetime());

	for (auto i = 0; i != kImportWordCountLong; ++i) {
		const auto field = state->fields[i];
		state->pasteButtons[i]->setClickedCallback([=] {
			const auto text = QGuiApplication::clipboard()->text();
			const auto words = SplitPhraseWords(text);
			if (words.size() > 1) {
				distributePaste(words);
			} else {
				field->setText(text.trimmed().toLower());
				field->forceProcessContentsChanges();
				field->setFocusFast();
			}
		});
		state->clearButtons[i]->setClickedCallback([=] {
			field->setText(QString());
			field->forceProcessContentsChanges();
			field->setFocusFast();
		});
		field->setMimeDataHook([=](
				not_null<const QMimeData*> data,
				Ui::InputField::MimeAction action) {
			const auto text = data->hasText() ? data->text() : QString();
			const auto words = SplitPhraseWords(text);
			if (words.size() < 2) {
				return false;
			}
			if (action == Ui::InputField::MimeAction::Check) {
				return true;
			}
			distributePaste(words);
			return true;
		});
		field->submits() | rpl::on_next([=] {
			if (acceptSuggestion()) {
				return;
			}
			if (i + 1 < state->count.current()) {
				state->fields[i + 1]->setFocus();
			} else {
				submit();
			}
		}, field->lifetime());
		field->tabbed() | rpl::on_next([=](not_null<bool*> handled) {
			*handled = true;
			if (acceptSuggestion()) {
				return;
			}
			if (i + 1 < state->count.current()) {
				state->fields[i + 1]->setFocus();
			}
		}, field->lifetime());
		base::install_event_filter(field->rawTextEdit(), [=](
				not_null<QEvent*> event) {
			if (event->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(event.get())->key();
			const auto shown = (state->suggestionField == i)
				&& !suggestions->isHidden();
			if (shown && key == Qt::Key_Down) {
				moveSuggestionSelection(1);
				return base::EventFilterResult::Cancel;
			} else if (shown && key == Qt::Key_Up) {
				moveSuggestionSelection(-1);
				return base::EventFilterResult::Cancel;
			} else if (shown && key == Qt::Key_Escape) {
				hideSuggestions();
				return base::EventFilterResult::Cancel;
			} else if (key == Qt::Key_Backspace
				&& field->getLastText().isEmpty()
				&& i > 0) {
				state->fields[i - 1]->setFocus();
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
		field->changes() | rpl::on_next([=] {
			state->error = QString();
			refreshAccessories(i);
			refreshSuggestions(i);
			updateButton();
		}, field->lifetime());
		field->focusedChanges() | rpl::on_next([=](bool focused) {
			refreshAccessories(i);
			if (focused) {
				refreshSuggestions(i);
			} else if (state->suggestionField == i) {
				hideSuggestions();
			}
		}, field->lifetime());
		refreshAccessories(i);
	}

	rpl::merge(
		box->scrolls(),
		box->widthValue() | rpl::to_empty
	) | rpl::on_next([=] {
		repositionSuggestions();
	}, box->lifetime());
}

void WalletReplaceBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_replace_title(),
			st::walletReplaceTitleLabel),
		st::walletReplaceTitleMargin);
	const auto create = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_wallet_replace_create(),
			st::walletSendButton),
		st::walletReplaceButtonMargin,
		style::al_justify);
	create->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	create->setClickedCallback([=] {
		auto &wallet = controller->session().wallet();
		if (wallet.keyState() != KeyState::None) {
			return;
		}
		if (!wallet.create()) {
			controller->showToast(u"Wallet create failed."_q);
			return;
		}
		wallet.startPolling();
		controller->hideLayer();
		controller->showToast({
			.title = tr::lng_wallet_created_title(tr::now),
			.text = { tr::lng_wallet_created_text(tr::now) },
			.icon = &st::toastCheckIcon,
		});
	});
	const auto import = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_wallet_replace_import(),
			st::walletSendButton),
		st::walletReplaceButtonMargin,
		style::al_justify);
	import->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	import->setClickedCallback([=] {
		box->closeBox();
		controller->show(Box(WalletImportBox, controller));
	});
	Ui::AddSkip(box->verticalLayout());
}

void WalletKeysBackupBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto stored = controller->session().local().readWallet();
	if (!stored || stored->words.empty()) {
		box->closeBox();
		return;
	}
	const auto count = int(stored->words.size());
	box->setTitle(tr::lng_wallet_keys_title());
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_wallet_keys_show_phrase(),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		WalletRevealFlow(controller);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		tr::lng_wallet_keys_phrase_about(
			lt_count,
			rpl::single(count * 1.) | tr::to_count()));
	Ui::AddSkip(container);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_wallet_keys_delete(),
		st::settingsAttentionButton
	)->addClickHandler([=] {
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_wallet_delete_text(tr::now, lt_count, count),
			.confirmed = [=](Fn<void()> close) {
				close();
				controller->hideLayer();
				controller->session().wallet().remove();
				controller->show(Box(WalletReplaceBox, controller));
			},
			.confirmText = tr::lng_wallet_delete_confirm(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_delete_title(),
		}));
	});
	Ui::AddSkip(container);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

Card::Card(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	controller->session().wallet().keyStateValue(
	) | rpl::on_next([=](KeyState) {
		refreshAddress();
	}, lifetime());

	Info::Profile::NameValue(
		controller->session().user()
	) | rpl::on_next([=](const QString &name) {
		_name = name.toUpper();
		update();
	}, lifetime());

	setupBalance();
	setupQr();
}

void Card::refreshAddress() {
	auto &wallet = _controller->session().wallet();
	const auto address = wallet.addressFriendly(false);
	_addressLine1 = _addressLine2 = QString();
	if (address.size() == kAddressLength) {
		_addressLine1 = GroupedAddressLine(address, 0);
		_addressLine2 = GroupedAddressLine(address, kAddressLength / 2);
	}
	update();
}

int Card::resizeGetHeight(int newWidth) {
	return st::walletCardHeight;
}

void Card::setupBalance() {
	_major = Ui::CreateChild<Ui::FlatLabel>(
		this,
		st::walletCardBalanceMajorLabel);
	_minor = Ui::CreateChild<Ui::FlatLabel>(
		this,
		st::walletCardBalanceMinorLabel);
	_ticker = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_wallet_card_ticker(),
		st::walletCardTickerLabel);
	_ticker->setOpacity(st::walletCardSecondaryOpacity);
	_fiat = Ui::CreateChild<Ui::FlatLabel>(
		this,
		st::walletCardFiatLabel);
	_fiat->setOpacity(st::walletCardSecondaryOpacity);
	_major->setAttribute(Qt::WA_TransparentForMouseEvents);
	_minor->setAttribute(Qt::WA_TransparentForMouseEvents);
	_ticker->setAttribute(Qt::WA_TransparentForMouseEvents);
	_fiat->setAttribute(Qt::WA_TransparentForMouseEvents);

	_controller->session().wallet().balanceNanoValue(
	) | rpl::on_next([=](int64 nano) {
		const auto fraction = nano % Ui::kNanosInOne;
		const auto amount = CreditsAmount(
			nano / Ui::kNanosInOne,
			fraction,
			CreditsType::Ton);
		SetBalanceText(_major, amount);
		_minor->setText(fraction
			? Info::ChannelEarn::MinorPart(amount)
			: QString());
	}, lifetime());

	rpl::combine(
		_controller->session().wallet().balanceNanoValue(),
		TonUsdRateValue(&_controller->session())
	) | rpl::on_next([=](int64 nano, float64 rate) {
		const auto text = FormatUsd(nano, rate);
		_fiat->setText(text);
		_fiat->setVisible(!text.isEmpty());
	}, lifetime());

	rpl::combine(
		widthValue(),
		_major->sizeValue(),
		_major->naturalWidthValue(),
		_minor->sizeValue(),
		_ticker->sizeValue(),
		_fiat->sizeValue()
	) | rpl::on_next([=] {
		updateLayout();
	}, lifetime());
}

void Card::setupQr() {
	_qr = Ui::CreateChild<Ui::AbstractButton>(this);
	_qr->resize(st::walletCardQrSize, st::walletCardQrSize);
	_qr->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_qr);
		auto hq = PainterHighQualityEnabler(p);
		auto bg = st::premiumButtonFg->c;
		bg.setAlphaF(st::walletCardQrBgOpacity);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(
			_qr->rect(),
			st::walletCardQrRadius,
			st::walletCardQrRadius);
		st::walletCardQrIcon.paintInCenter(p, _qr->rect());
	}, _qr->lifetime());

	const auto controller = _controller;
	_qr->setClickedCallback([=] {
		ShowWalletReceiveBox(controller, controller->uiShow());
	});
}

void Card::updateLayout() {
	if (!_qr) {
		return;
	}
	const auto qrLeft = width()
		- st::walletCardQrRight
		- st::walletCardQrSize;
	const auto available = qrLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft
		- _minor->width()
		- st::walletCardTickerSkip
		- _ticker->width();
	if (available > 0) {
		_major->resizeToNaturalWidth(available);
	}
	_major->moveToLeft(
		st::walletCardContentLeft,
		st::walletCardBalanceTop,
		width());
	auto left = st::walletCardContentLeft + _major->width();
	_minor->moveToLeft(
		left,
		st::walletCardBalanceTop + st::walletCardBalanceMinorSkip,
		width());
	left += _minor->width() + st::walletCardTickerSkip;
	_ticker->moveToLeft(left, st::walletCardBalanceTop, width());
	_fiat->moveToLeft(
		st::walletCardContentLeft,
		st::walletCardFiatTop,
		width());
	_qr->moveToLeft(
		width() - st::walletCardQrRight - _qr->width(),
		st::walletCardBalanceTop + (_major->height() - _qr->height()) / 2,
		width());
}

void Card::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	auto gradient = QLinearGradient(
		QPointF(0, 0),
		QPointF(width(), height()));
	gradient.setStops(Ui::Premium::ButtonGradientStops());
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(rect(), st::walletCardRadius, st::walletCardRadius);

	p.setPen(st::premiumButtonFg);
	p.setOpacity(st::walletCardSecondaryOpacity);

	const auto nameFont = st::walletCardNameFont->monospace();
	const auto addressFont = st::walletCardAddressFont->monospace();
	const auto addressBaseline = width()
		- st::walletCardAddressRight
		- addressFont->height
		- addressFont->ascent;
	const auto stripLeft = addressBaseline - addressFont->descent;
	const auto nameMax = stripLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft;
	p.setFont(nameFont);
	p.drawText(
		st::walletCardContentLeft,
		height() - st::walletCardNameBottom - nameFont->descent,
		nameFont->elided(_name, nameMax));

	if (!_addressLine1.isEmpty()) {
		p.setFont(addressFont);
		p.save();
		p.translate(addressBaseline, st::walletCardAddressSkip);
		p.rotate(90);
		p.drawText(0, 0, _addressLine1);
		p.drawText(0, -addressFont->height, _addressLine2);
		p.restore();
	}
}

} // namespace

class FixedBar final : public Ui::RpWidget {
public:
	FixedBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setAnimatingMode(bool enabled);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void goBack();
	void showMenu();

	const not_null<Window::SessionController*> _controller;
	object_ptr<Profile::BackButton> _backButton;
	object_ptr<Ui::IconButton> _menuToggle;
	base::unique_qptr<Ui::PopupMenu> _menu;
	bool _animatingMode = false;

};

FixedBar::FixedBar(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _backButton(this)
, _menuToggle(this, st::topBarMenuToggle) {
	_backButton->moveToLeft(0, 0);
	_backButton->setClickedCallback([=] { goBack(); });
	_menuToggle->setClickedCallback([=] { showMenu(); });

	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		_backButton.data(),
		tr::lng_wallet_title(),
		st::walletTopBarTitleLabel);
	title->setAttribute(Qt::WA_TransparentForMouseEvents);
	title->show();
	rpl::combine(
		_backButton->sizeValue(),
		title->sizeValue()
	) | rpl::on_next([=](QSize bar, QSize label) {
		title->moveToLeft(
			st::topBarArrowPadding.left(),
			(bar.height() - label.height()) / 2,
			bar.width());
	}, title->lifetime());
}

void FixedBar::goBack() {
	_controller->showBackFromStack();
}

void FixedBar::showMenu() {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->setDestroyedCallback([
			weak = base::make_weak(this),
			weakToggle = base::make_weak(_menuToggle.data()),
			menu = _menu.get()] {
		if (weak && weak->_menu == menu) {
			if (weakToggle) {
				weakToggle->setForceRippled(false);
			}
		}
	});
	_menuToggle->setForceRippled(true);
	const auto controller = _controller;
	_menu->addAction(
		Ui::Text::FixAmpersandInAction(tr::lng_wallet_keys_title(tr::now)),
		[=] {
			const auto stored = controller->session().local().readWallet();
			if (!stored || stored->words.empty()) {
				return;
			}
			controller->show(Box(WalletKeysBackupBox, controller));
		},
		&st::menuIconPermissions);
	_menu->addSeparator();
	_menu->addAction(
		Ui::Text::FixAmpersandInAction(tr::lng_wallet_how_menu(tr::now)),
		[=] { controller->show(Box(WalletHowItWorksBox)); },
		&st::menuIconFaq);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(mapToGlobal(QPoint(
		width() + st::topBarMenuPosition.x(),
		st::topBarMenuPosition.y())));
}

void FixedBar::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setCursor(_animatingMode ? style::cur_pointer : style::cur_default);
		if (_animatingMode) {
			setAttribute(Qt::WA_OpaquePaintEvent, false);
			hideChildren();
		} else {
			setAttribute(Qt::WA_OpaquePaintEvent);
			showChildren();
		}
		show();
	}
}

void FixedBar::paintEvent(QPaintEvent *e) {
	if (!_animatingMode) {
		auto p = QPainter(this);
		p.fillRect(e->rect(), st::topBarBg);
	}
}

void FixedBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		goBack();
	} else {
		RpWidget::mousePressEvent(e);
	}
}

int FixedBar::resizeGetHeight(int newWidth) {
	_backButton->resizeToWidth(newWidth);
	_menuToggle->moveToRight(0, 0, newWidth);
	return _backButton->height();
}

object_ptr<Window::SectionWidget> SectionMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<SectionWidget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _scroll(this, st::defaultScrollArea)
, _fixedBar(this, controller)
, _fixedBarShadow(this) {
	auto &wallet = session().wallet();
	if (wallet.keyState() == KeyState::None) {
		wallet.create();
	}
	wallet.startPolling();

	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	controller->adaptive().value(
	) | rpl::on_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	setupContent();
	_scroll->move(0, _fixedBar->height());
	_scroll->show();
	_fixedBarShadow->raise();
}

SectionWidget::~SectionWidget() {
	session().wallet().stopPolling();
}

void SectionWidget::setupContent() {
	_container = _scroll->setOwnedWidget(
		object_ptr<Ui::RpWidget>(_scroll.data()));
	const auto column = Ui::CreateChild<Ui::VerticalLayout>(_container);
	column->show();

	Ui::AddSkip(column, st::walletCardTopSkip);
	const auto card = column->add(object_ptr<Card>(column, controller()));

	const auto controller = this->controller();
	const auto buttons = column->add(
		object_ptr<Ui::FixedHeightWidget>(
			column,
			st::walletSendButton.height),
		st::walletSendButtonMargin,
		style::al_justify);
	const auto addPill = [&](
			rpl::producer<QString> text,
			Fn<void()> callback) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			buttons,
			std::move(text),
			st::walletSendButton);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setClickedCallback(std::move(callback));
		button->show();
		return button;
	};
	const auto addFunds = addPill(tr::lng_wallet_add_funds(), [=] {
		ShowWalletReceiveBox(controller, controller->uiShow());
	});
	const auto send = addPill(tr::lng_wallet_send_button(), [=] {
		controller->show(Box(WalletSendRecipientBox, controller));
	});
	buttons->widthValue(
	) | rpl::on_next([=](int width) {
		const auto single = (width - st::walletButtonsSkip) / 2;
		addFunds->setFullWidth(single);
		addFunds->moveToLeft(0, 0, width);
		const auto left = single + st::walletButtonsSkip;
		send->setFullWidth(width - left);
		send->moveToLeft(left, 0, width);
	}, buttons->lifetime());

	const auto wallet = &session().wallet();
	const auto bannerWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto bannerInner = bannerWrap->entity();
	Ui::AddSkip(bannerInner, st::walletBannerTopSkip);
	Settings::AddButtonWithIcon(
		bannerInner,
		tr::lng_wallet_protect_banner(),
		st::settingsAttentionButtonWithIcon,
		{ &st::menuIconReportAttention }
	)->addClickHandler([=] {
		WalletRevealFlow(controller);
	});
	bannerWrap->toggleOn(wallet->phraseUnviewedValue());
	bannerWrap->finishAnimating();

	const auto wrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto about = wrap->entity();
	Ui::AddSkip(about, st::walletAboutTopSkip);
	const auto addEntry = [&](
			rpl::producer<QString> title,
			rpl::producer<QString> text,
			const style::icon &icon) {
		const auto top = about->add(
			object_ptr<Ui::FlatLabel>(
				about,
				std::move(title),
				st::walletAboutTitleLabel),
			st::walletAboutPadding);
		Ui::AddSkip(about, st::walletAboutTitleSkip);
		about->add(
			object_ptr<Ui::FlatLabel>(
				about,
				std::move(text),
				st::walletAboutTextLabel),
			st::walletAboutPadding);
		const auto left = Ui::CreateChild<Ui::RpWidget>(about);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon.paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon.size());
		top->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			left->moveToLeft(
				(g.left() - left->width()) / 2,
				g.top() + st::walletAboutIconSkip);
		}, left->lifetime());
	};
	addEntry(
		tr::lng_wallet_about_instant_title(),
		tr::lng_wallet_about_instant_text(),
		st::walletAboutInstantIcon);
	Ui::AddSkip(about, st::walletAboutRowSkip);
	addEntry(
		tr::lng_wallet_about_fees_title(),
		tr::lng_wallet_about_fees_text(),
		st::walletAboutFeesIcon);
	Ui::AddSkip(about, st::walletAboutRowSkip);
	addEntry(
		tr::lng_wallet_about_chain_title(),
		tr::lng_wallet_about_chain_text(),
		st::walletAboutChainIcon);

	wrap->toggleOn(rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->historyUpdates(),
		wallet->sendStateValue() | rpl::to_empty
	)) | rpl::map([=] {
		return wallet->history().empty() && !wallet->pendingSend();
	}));
	wrap->finishAnimating();

	const auto listWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto list = listWrap->entity();
	const auto rebuildList = [=] {
		list->clear();
		const auto &history = wallet->history();
		const auto &pending = wallet->pendingSend();
		if (!history.empty() || pending) {
			Ui::AddSkip(list, st::walletRowsTopSkip);
			const auto shown = pending
				&& ranges::any_of(history, [&](
						const Gram::TransferItem &item) {
					return item.externalHashNorm
						== pending->messageHashNorm;
				});
			if (pending && !shown) {
				const auto item = ItemFromPending(*pending);
				AddHistoryRow(list, RowContentFromPending(*pending), [=] {
					ShowWalletTransactionBox(controller, item);
				});
			}
			for (const auto &item : history) {
				AddHistoryRow(list, RowContentFromItem(item), [=] {
					ShowWalletTransactionBox(controller, item);
				});
			}
			Ui::AddSkip(list, st::walletRowsTopSkip);
		}
		list->resizeToWidth(st::walletContentWidth);
		checkLoadMore();
	};
	rpl::merge(
		wallet->historyUpdates(),
		wallet->sendStateValue() | rpl::to_empty
	) | rpl::on_next(rebuildList, list->lifetime());
	listWrap->toggleOn(rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->historyUpdates(),
		wallet->sendStateValue() | rpl::to_empty
	)) | rpl::map([=] {
		return !wallet->history().empty()
			|| wallet->pendingSend().has_value();
	}));
	listWrap->finishAnimating();

	_scroll->scrolls(
	) | rpl::on_next([=] {
		checkLoadMore();
	}, lifetime());

	column->resizeToWidth(st::walletContentWidth);
	rpl::combine(
		_container->widthValue(),
		column->heightValue()
	) | rpl::on_next([=](int width, int height) {
		column->moveToLeft((width - column->width()) / 2, 0);
		_container->resize(width, height);
	}, column->lifetime());

	const auto local = &session().local();
	if (!local->readPref<bool>(kIntroTooltipShownPref)) {
		local->writePref<bool>(kIntroTooltipShownPref, true);
		SetupIntroTooltip(_container, card);
	}
}

void SectionWidget::checkLoadMore() {
	auto &wallet = session().wallet();
	if (!wallet.historyHasNext()) {
		return;
	}
	if (_scroll->scrollTop() + _scroll->height() >= _scroll->scrollTopMax()) {
		wallet.loadMoreHistory();
	}
}

QPixmap SectionWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_fixedBarShadow->hide();
	}
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) {
		_fixedBarShadow->show();
	}
	return result;
}

bool SectionWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return (dynamic_cast<SectionMemento*>(memento.get()) != nullptr);
}

std::shared_ptr<Window::SectionMemento> SectionWidget::createMemento() {
	return std::make_shared<SectionMemento>();
}

void SectionWidget::setInternalState(
		const QRect &geometry,
		not_null<SectionMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
}

void SectionWidget::updateAdaptiveLayout() {
	_fixedBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_fixedBar->height());
}

void SectionWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_fixedBar->resizeToWidth(width());
	_fixedBarShadow->resize(width(), st::lineWidth);
	updateAdaptiveLayout();
	_scroll->setGeometry(
		0,
		_fixedBar->height(),
		width(),
		height() - _fixedBar->height());
	if (_container) {
		_container->resize(width(), _container->height());
	}
	checkLoadMore();
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	Window::SectionWidget::paintEvent(e);
	if (!animatingShow()) {
		QPainter(this).fillRect(e->rect(), st::windowBg);
	}
}

void SectionWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_fixedBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_fixedBarShadow->show();
	}
}

void SectionWidget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
}

void SectionWidget::doSetInnerFocus() {
	_scroll->setFocus();
}

QRect SectionWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

bool SectionWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

void OpenTransferLink(
		not_null<Window::SessionController*> controller,
		const QString &url) {
	const auto flow = ParseRecipientFlow(url);
	if (!flow) {
		controller->showToast(tr::lng_wallet_send_invalid_address(tr::now));
		return;
	}
	controller->showSection(std::make_shared<SectionMemento>());
	controller->show(Box(WalletSendAmountBox, controller, *flow));
}

} // namespace Wallet
