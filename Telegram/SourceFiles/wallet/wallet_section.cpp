/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_section.h"

#include "base/unixtime.h"
#include "core/credits_amount.h"
#include "data/data_user.h"
#include "gram/api/gram_api_history.h"
#include "gram/ton/gram_address.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "profile/profile_back_button.h"
#include "qr/qr_generate.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_session.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kAddressLength = 48;
constexpr auto kAddressGroup = 4;
constexpr auto kAddressGroupsPerLine = 6;
constexpr auto kDetailsGroupsPerLine = 4;
constexpr auto kShortAddressChars = 4;
constexpr auto kMinus = QChar(0x2212);

class Card final : public Ui::RpWidget {
public:
	Card(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void setupBalance();
	void setupQr();
	void updateLayout();

	const not_null<Window::SessionController*> _controller;
	Ui::FlatLabel *_major = nullptr;
	Ui::FlatLabel *_minor = nullptr;
	Ui::FlatLabel *_ticker = nullptr;
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

[[nodiscard]] QString ShortAddress(const Gram::Address &address) {
	if (address.hash.isEmpty()) {
		return QString();
	}
	const auto full = Gram::FormatFriendly(address, true);
	return full.left(kShortAddressChars)
		+ QChar(0x2026)
		+ full.right(kShortAddressChars);
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
	result.date = pending.posted;
	result.status = Gram::TransferItem::Status::Pending;
	return result;
}

void WalletQrBox(not_null<Ui::GenericBox*> box, const QString &address) {
	box->setTitle(tr::lng_wallet_qr_title());
	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });

	const auto data = Qr::Encode(address, Qr::Redundancy::Default);
	const auto max = st::boxWidth
		- st::boxRowPadding.left()
		- st::boxRowPadding.right();
	auto pixel = st::walletQrPixel;
	if (data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	const auto qr = Qr::Generate(
		data,
		pixel * style::DevicePixelRatio(),
		st::windowFg->c);
	const auto size = qr.width() / style::DevicePixelRatio();
	const auto height = st::walletQrSkip * 2 + size;
	const auto container = box->addRow(
		object_ptr<Ui::BoxContentDivider>(box, height),
		st::walletQrMargin);
	const auto button = Ui::CreateChild<Ui::AbstractButton>(container);
	button->resize(size, size);
	button->paintRequest(
	) | rpl::on_next([=] {
		QPainter(button).drawImage(QRect(0, 0, size, size), qr);
	}, button->lifetime());
	container->widthValue(
	) | rpl::on_next([=](int width) {
		button->move((width - size) / 2, st::walletQrSkip);
	}, button->lifetime());

	const auto copy = [=, show = box->uiShow()] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	};
	button->setClickedCallback(copy);
	box->addLeftButton(tr::lng_wallet_qr_copy(), copy);
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
		auto label = object_ptr<Ui::FlatLabel>(
			table,
			rpl::single(DetailsAddressValue(address)),
			st::walletDetailsAddressLabel);
		label->setClickHandlerFilter([=, show = box->uiShow()](
				const auto &...) {
			TextUtilities::SetClipboardText(
				TextForMimeData::Simple(address));
			show->showToast({
				.text = { tr::lng_gift_unique_address_copied(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
			return false;
		});
		Ui::AddTableRow(
			table,
			(item.incoming
				? tr::lng_wallet_details_sender()
				: tr::lng_wallet_details_recipient()),
			std::move(label));
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

void WalletTransactionBox(
		not_null<Ui::GenericBox*> box,
		Gram::TransferItem item) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddDetailsAmountHeader(box, item);
	AddDetailsTable(box, item);

	const auto close = Ui::CreateChild<Ui::IconButton>(
		box.get(),
		st::boxTitleClose);
	close->setClickedCallback([=] {
		box->closeBox();
	});
	box->widthValue(
	) | rpl::on_next([=](int width) {
		close->moveToRight(0, 0);
	}, box->lifetime());

	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

Card::Card(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	auto &wallet = controller->session().wallet();
	const auto address = wallet.addressFriendly(false);
	if (address.size() == kAddressLength) {
		_addressLine1 = GroupedAddressLine(address, 0);
		_addressLine2 = GroupedAddressLine(address, kAddressLength / 2);
	}
	Info::Profile::NameValue(
		controller->session().user()
	) | rpl::on_next([=](const QString &name) {
		_name = name.toUpper();
		update();
	}, lifetime());

	setupBalance();
	setupQr();
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
	_major->setAttribute(Qt::WA_TransparentForMouseEvents);
	_minor->setAttribute(Qt::WA_TransparentForMouseEvents);
	_ticker->setAttribute(Qt::WA_TransparentForMouseEvents);

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
		widthValue(),
		_major->sizeValue(),
		_major->naturalWidthValue(),
		_minor->sizeValue(),
		_ticker->sizeValue()
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
		auto &wallet = controller->session().wallet();
		const auto address = wallet.addressFriendly(false);
		if (!address.isEmpty()) {
			controller->show(Box(WalletQrBox, address));
		}
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

	const not_null<Window::SessionController*> _controller;
	object_ptr<Profile::BackButton> _backButton;
	bool _animatingMode = false;

};

FixedBar::FixedBar(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _backButton(this) {
	_backButton->moveToLeft(0, 0);
	_backButton->setClickedCallback([=] { goBack(); });

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
	column->add(object_ptr<Card>(column, controller()));

	const auto send = column->add(
		object_ptr<Ui::RoundButton>(
			column,
			tr::lng_wallet_send_button(),
			st::walletSendButton),
		st::walletSendButtonMargin,
		style::al_justify);
	send->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	send->setDisabled(true);
	send->setAttribute(Qt::WA_TransparentForMouseEvents);

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

	const auto wallet = &session().wallet();
	wrap->toggleOn(rpl::single(rpl::empty) | rpl::then(
		wallet->historyUpdates()
	) | rpl::map([=] {
		return wallet->history().empty();
	}));
	wrap->finishAnimating();

	const auto listWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto list = listWrap->entity();
	const auto controller = this->controller();
	const auto rebuildList = [=] {
		list->clear();
		const auto &history = wallet->history();
		if (!history.empty()) {
			Ui::AddSkip(list, st::walletRowsTopSkip);
			const auto &pending = wallet->pendingSend();
			const auto shown = pending
				&& ranges::any_of(history, [&](
						const Gram::TransferItem &item) {
					return item.externalHashNorm
						== pending->messageHashNorm;
				});
			if (pending && !shown) {
				const auto item = ItemFromPending(*pending);
				AddHistoryRow(list, RowContentFromPending(*pending), [=] {
					controller->show(Box(WalletTransactionBox, item));
				});
			}
			for (const auto &item : history) {
				AddHistoryRow(list, RowContentFromItem(item), [=] {
					controller->show(Box(WalletTransactionBox, item));
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
	listWrap->toggleOn(rpl::single(rpl::empty) | rpl::then(
		wallet->historyUpdates()
	) | rpl::map([=] {
		return !wallet->history().empty();
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

} // namespace Wallet
