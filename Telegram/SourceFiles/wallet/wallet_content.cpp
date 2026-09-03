/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_content.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "boxes/passcode_box.h"
#include "core/credits_amount.h"
#include "core/file_utilities.h"
#include "core/ton_explorer_url.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_pill.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/session/session_show.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "qr/qr_generate.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/feature_list.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/glare_tooltip.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
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
#include "wallet/wallet_address.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_collectibles.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_user_addresses.h"
#include "window/themes/window_theme.h"

#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QTextEdit>

#include <array>

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kAddressLength = 48;
constexpr auto kAddressGroup = 4;
constexpr auto kAddressGroupsPerLine = 6;
constexpr auto kDetailsGroupsPerLine = 4;
constexpr auto kReceiveGroupsPerLine = 4;
constexpr auto kReceiveLines = kAddressLength
	/ kAddressGroup
	/ kReceiveGroupsPerLine;
constexpr auto kQrQuietZoneModules = 4;
constexpr auto kShortAddressChars = 4;
constexpr auto kMinus = QChar(0x2212);
constexpr auto kImportWordCountShort = 12;
constexpr auto kImportWordCountLong = 24;
constexpr auto kImportSuggestionsLimit = 3;
constexpr auto kBackupWriteDownDelay = 30 * crl::time(1000);
constexpr auto kBackupQuizWordCount = 3;
constexpr auto kCoverBodyPart = 0.90;
constexpr auto kCoverTitleScale = 0.05;
constexpr auto kCardFadePart = 0.45;
constexpr auto kCardMotionPart = 0.55;
constexpr auto kIntroTooltipShownPref = "wallet_intro_tooltip_shown"_cs;
constexpr auto kIntroToastShownPref = "wallet_intro_toast_shown"_cs;
constexpr auto kIntroToastDuration = 4 * crl::time(1000);
constexpr auto kWalletIntroGlares = 2;
constexpr auto kCommentMaxBytes = 960;
constexpr auto kFeeFiatDecimals = 5;
constexpr auto kMaxFiatUnits = 999'999'999LL;
constexpr auto kMaxAmountNano = 999'999'999'999'999'999LL;
constexpr auto kRowAmountPreciseBelowNano = Ui::kNanosInOne / 100;

[[nodiscard]] float64 AlignedMarkTop(
		const style::font &font,
		const QImage &image) {
	Expects(!image.isNull());
	Expects(image.hasAlphaChannel());
	const auto rowHasAlpha = [&](int y) {
		for (auto x = 0; x != image.width(); ++x) {
			if (qAlpha(image.pixel(x, y))) {
				return true;
			}
		}
		return false;
	};
	auto first = 0;
	while (first != image.height() && !rowHasAlpha(first)) {
		++first;
	}
	Expects(first != image.height());
	auto last = image.height() - 1;
	while (!rowHasAlpha(last)) {
		--last;
	}
	const auto zero = font->metrics().tightBoundingRect(u"0"_q);
	const auto digitCenter = font->ascent
		+ (zero.top() + zero.bottom()) / 2.;
	const auto markCenter = (first + last + 1.)
		/ (2. * image.devicePixelRatio());
	return digitCenter - markCenter;
}

class BalanceInk;
class Card;

class Content final : public Ui::RpWidget {
public:
	Content(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show);
	~Content();

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void setupContent();
	void setupPinned();
	void setupBalance();
	void setupTabs(rpl::producer<bool> collectiblesShown);
	void setupStrip();
	void setupListsLoading();
	void setupCustodyBar();
	void updateRegions();
	void updatePinned();
	void updateVisibleArea();
	void checkLoadMore();
	[[nodiscard]] int pinnedMax() const;
	[[nodiscard]] int pinnedMin() const;
	[[nodiscard]] int barHeight() const;
	[[nodiscard]] float64 collapseProgress() const;
	[[nodiscard]] QRect cardVisible();

	const std::shared_ptr<Main::SessionShow> _show;
	object_ptr<Ui::ScrollArea> _scroll;
	SingleQueuedInvokation _loadMoreCheck;
	std::unique_ptr<BalanceInk> _ink;
	base::unique_qptr<Ui::RpWidget> _titleBalance;
	Ui::RpWidget *_container = nullptr;
	Ui::PaddingWrap<Ui::VerticalLayout> *_column = nullptr;
	Ui::RpWidget *_pinnedBackground = nullptr;
	Ui::RpWidget *_pinned = nullptr;
	Ui::VerticalLayout *_pinnedInner = nullptr;
	Ui::RpWidget *_pinnedBalance = nullptr;
	Ui::PlainShadow *_headerShadow = nullptr;
	Ui::SlideWrap<> *_headerBottomSkip = nullptr;
	Ui::SlideWrap<Ui::SettingsSlider> *_tabsWrap = nullptr;
	Ui::PlainShadow *_tabsShadow = nullptr;
	Ui::PlainShadow *_stripShadow = nullptr;
	Ui::RpWidget *_strip = nullptr;
	Ui::RpWidget *_listsLoading = nullptr;
	Ui::SlideWrap<Ui::AbstractButton> *_custodyBar = nullptr;
	Ui::FlatLabel *_custodyBarLabel = nullptr;
	Ui::PlainShadow *_custodyBarShadow = nullptr;
	Ui::FixedHeightWidget *_cardPlaceholder = nullptr;
	Card *_card = nullptr;
	Ui::AbstractButton *_cardQr = nullptr;
	Ui::RpWidget *_cardFade = nullptr;
	QRect _paintedInk;
	QRect _paintedCard;
	float64 _cardFadeOpacity = 0.;
	int _reserve = 0;
	int _paintedHeight = -1;
	int _paintedMin = -1;
	int _titleRight = 0;
	bool _tabsShown = false;
	bool _stripShown = false;

};

class Card final : public Ui::RpWidget {
public:
	Card(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<TextWithEntities> name);

	void setPresentation(float64 motion, float64 opacity);
	[[nodiscard]] QRectF paintedRect() const;
	[[nodiscard]] QRect paintedQrRect() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] float64 collapseScale() const;
	void refreshAddress();

	const std::shared_ptr<Main::SessionShow> _show;
	style::TextStyle _nameStyle;
	Ui::Text::String _name;
	QString _addressLine1;
	QString _addressLine2;
	float64 _motion = 0.;
	float64 _opacity = 1.;

};

struct BalancePalette {
	QColor mark;
	QColor amount;
	QColor secondary;
};

class BalanceInk final {
public:
	void setContent(CreditsAmount amount, const QString &fiat);
	void setOuterWidth(int outerWidth);
	void refresh();

	void paint(
		QPainter &p,
		float64 progress,
		int outerWidth,
		int titleRight,
		QRect card,
		QRect clip) const;

	[[nodiscard]] QRect boundingRect(
		float64 progress,
		int outerWidth,
		int titleRight) const;
	[[nodiscard]] QRect markRect() const;

private:
	[[nodiscard]] QRectF amountRect(
		float64 progress,
		int outerWidth,
		int titleRight) const;
	[[nodiscard]] QRectF fiatRect(
		float64 progress,
		int outerWidth,
		int titleRight) const;
	void paintPass(
		QPainter &p,
		float64 progress,
		int outerWidth,
		int titleRight,
		const BalancePalette &palette,
		const QImage &mark,
		float64 secondaryOpacity) const;

	QPainterPath _amount;
	QPainterPath _ticker;
	QPainterPath _fiat;
	QImage _markCard;
	QImage _markSettled;
	CreditsAmount _balance;
	QString _fiatText;
	float64 _markTop = 0.;
	float64 _tickerLeft = 0.;
	float64 _amountWidth = 0.;
	float64 _fiatWidth = 0.;
	int _outerWidth = 0;

};

[[nodiscard]] QRect CardQrRect(int cardWidth) {
	return QRect(
		cardWidth - st::walletCardQrRight - st::walletCardQrSize.width(),
		st::walletCardQrTop,
		st::walletCardQrSize.width(),
		st::walletCardQrSize.height());
}

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

// The sheet's address presentation is the friendly form, so a raw address
// the engine cannot convert is a reading this sheet does not have and
// builds no row, exactly as an Absent answer builds none. Substituting the
// raw form would show a different kind of address without saying so.
[[nodiscard]] std::optional<QString> DetailsFriendlyAddress(
		const QString &raw) {
	const auto friendly = FormatFriendly(raw, true);
	if (friendly.isEmpty()) {
		return std::nullopt;
	}
	return friendly;
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

[[nodiscard]] Fn<void()> CopyAddressCallback(
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	return [=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	};
}

[[nodiscard]] object_ptr<Ui::FlatLabel> AddressValueLabel(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(DetailsAddressValue(address)),
		st::walletDetailsAddressLabel);
	const auto copy = CopyAddressCallback(std::move(show), address);
	result->setClickHandlerFilter([=](const auto &...) {
		copy();
		return false;
	});
	return result;
}

[[nodiscard]] object_ptr<Ui::FlatLabel> NameValueLabel(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		const QString &name,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(Ui::Text::Link(name)),
		st::defaultTableValue);
	const auto copy = CopyAddressCallback(std::move(show), address);
	result->setClickHandlerFilter([=](const auto &...) {
		copy();
		return false;
	});
	return result;
}

void InsertAddressTableRow(
		not_null<Ui::TableLayout*> table,
		int position,
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	table->insertRow(
		position,
		object_ptr<Ui::FlatLabel>(
			table,
			tr::lng_wallet_details_address(),
			table->st().defaultLabel),
		AddressValueLabel(table, std::move(show), address),
		st::giveawayGiftCodeLabelMargin,
		st::giveawayGiftCodeValueMargin);
}

enum class RowAvatar {
	Peer,
	In,
	Out,
	Card,
	Contract,
	KeyChange,
};

struct HistoryRowContent {
	QString title;
	QString subtitle;
	QString date;
	int64 amountNano = 0;
	bool incoming = false;
	bool pending = false;
	RowAvatar avatar = RowAvatar::Out;
	PeerData *peer = nullptr;
	bool itemAmount = false;
	QString collectible;
};

[[nodiscard]] QString ShortAddressForm(const QString &full) {
	return full.left(kShortAddressChars)
		+ QChar(0x2026)
		+ full.right(kShortAddressChars);
}

[[nodiscard]] QString ShortAddress(const QString &address) {
	if (address.isEmpty()) {
		return QString();
	}
	const auto full = FormatFriendly(address, true);
	return ShortAddressForm(full);
}

void SetAmountColor(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		const style::color &color) {
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		major->setTextColorOverride(color->c);
		minor->setTextColorOverride(color->c);
	}, major->lifetime());
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
	const auto precise = !amount.whole()
		&& (amount.nano() < kRowAmountPreciseBelowNano);
	auto minorText = tr::marked(precise
		? Info::ChannelEarn::MinorPart(Data::EarnInt(amount.nano()))
		: Info::ChannelEarn::MinorPart(amount));
	minorText.append(helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyColored(
				st::walletRowMarkSize,
				st::windowActiveTextFg->c);
		},
		.margin = st::walletRowIconMargin,
	}));
	minor->setMarkedText(std::move(minorText), helper.context());
	const auto &color = pending
		? st::windowSubTextFg
		: incoming
		? st::boxTextFgGood
		: st::windowBoldFg;
	SetAmountColor(major, minor, color);
}

void SetRowItemAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		bool incoming) {
	major->setText((incoming ? QChar('+') : kMinus)
		+ tr::lng_wallet_row_items(tr::now, lt_count, 1));
	minor->setMarkedText(Ui::Text::IconEmoji(incoming
		? &st::walletRowItemMarkIn
		: &st::walletRowItemMarkOut));
	const auto &color = incoming ? st::boxTextFgGood : st::windowBoldFg;
	SetAmountColor(major, minor, color);
}

void PaintRowAvatar(Painter &p, QRect rect, RowAvatar avatar) {
	auto hq = PainterHighQualityEnabler(p);
	const auto in = (avatar == RowAvatar::In);
	const auto flat = (avatar == RowAvatar::Contract)
		|| (avatar == RowAvatar::KeyChange);
	if (flat) {
		p.setBrush(st::historyPeerArchiveUserpicBg);
	} else {
		const auto &top = in
			? st::historyPeer2UserpicBg
			: st::historyPeer4UserpicBg;
		const auto &bottom = in
			? st::historyPeer2UserpicBg2
			: st::historyPeer4UserpicBg2;
		auto gradient = QLinearGradient(
			rect.topLeft(),
			rect.bottomLeft());
		gradient.setStops({ { 0., top->c }, { 1., bottom->c } });
		p.setBrush(gradient);
	}
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	const auto icon = in
		? &st::walletRowArrowIn
		: (avatar == RowAvatar::Card)
		? &st::walletRowCardIcon
		: (avatar == RowAvatar::Contract)
		? &st::walletRowContractIcon
		: (avatar == RowAvatar::KeyChange)
		? &st::walletRowKeyIcon
		: &st::walletRowArrowOut;
	icon->paintInCenter(p, rect);
}

struct HistoryRowChipState {
	Gram::NftKind kind = Gram::NftKind::Generic;
	Ui::Text::String title;
	Ui::Text::String subtitle;
	int natural = 0;
};

class HistoryRowButton final : public Ui::SettingsButton {
public:
	using Ui::SettingsButton::SettingsButton;

	void setPaintUnderRipple(Fn<void(Painter&)> paint);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Fn<void(Painter&)> _paintUnderRipple;

};

void HistoryRowButton::setPaintUnderRipple(Fn<void(Painter&)> paint) {
	_paintUnderRipple = std::move(paint);
	update();
}

void HistoryRowButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto over = (isOver() || isDown()) && !isDisabled();
	paintBg(p, e->rect(), over);
	if (_paintUnderRipple) {
		_paintUnderRipple(p);
	}
	paintRipple(p, 0, 0);
	const auto outerw = width();
	paintText(p, over, outerw);
	paintToggle(p, outerw);
}

void PaintHistoryRowChipSurface(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state,
		const std::shared_ptr<CollectibleMedia> &media,
		const QString &address) {
	const auto side = st::walletRowIconSize;
	const auto radius = st::walletCollectibleThumbRadius;
	const auto plate = std::min(state.natural, outerWidth);
	const auto rtl = style::RightToLeft();
	const auto plateLeft = rtl ? (outerWidth - plate) : 0;
	const auto square = QRect(rtl ? (outerWidth - side) : 0, 0, side, side);
	const auto dark = (state.kind == Gram::NftKind::TelegramUsername)
		|| (state.kind == Gram::NftKind::TelegramNumber);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			QRect(plateLeft, 0, plate, side),
			radius,
			radius);
		if (dark) {
			p.setBrush(st::callBgOpaque);
			p.drawRoundedRect(square, radius, radius);
		}
	}
	if (state.kind == Gram::NftKind::TelegramUsername) {
		st::walletChipUsernameIcon.paintInCenter(p, square);
	} else if (state.kind == Gram::NftKind::TelegramNumber) {
		st::walletChipNumberIcon.paintInCenter(p, square);
	} else {
		media->paint(p, address, square, outerWidth, radius);
	}
}

void PaintHistoryRowChipText(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state) {
	const auto side = st::walletRowIconSize;
	const auto plate = std::min(state.natural, outerWidth);
	const auto rtl = style::RightToLeft();
	const auto plateLeft = rtl ? (outerWidth - plate) : 0;
	const auto available = plate
		- side
		- st::walletChipTextSkip
		- st::walletChipPadding.right();
	if (available <= 0) {
		return;
	}
	const auto textLeft = rtl
		? (plateLeft + st::walletChipPadding.right())
		: (side + st::walletChipTextSkip);
	const auto titleHeight = st::walletCollectibleTitleStyle.font->height;
	const auto subtitleHeight = st::walletRowDateLabel.style.font->height;
	const auto top = (side
		- titleHeight
		- st::walletRowSkip
		- subtitleHeight) / 2;
	p.setPen(st::windowBoldFg);
	state.title.draw(p, {
		.position = { textLeft, top },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.palette = &st::walletCollectibleTitlePalette,
		.elisionLines = 1,
	});
	p.setPen(st::windowSubTextFg);
	state.subtitle.draw(p, {
		.position = { textLeft, top + titleHeight + st::walletRowSkip },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.elisionLines = 1,
	});
}

void AddHistoryRowChip(
		not_null<Ui::VerticalLayout*> inner,
		not_null<HistoryRowButton*> button,
		std::shared_ptr<CollectibleMedia> media,
		QString address) {
	Ui::AddSkip(inner, st::walletChipTopSkip);
	const auto chip = inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::walletRowIconSize));
	chip->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = chip->lifetime().make_state<HistoryRowChipState>();
	const auto repaint = [=] {
		chip->update();
		button->update(Ui::MapFrom(button, chip, chip->rect()));
	};
	const auto refresh = [=] {
		const auto view = media->view(address);
		state->kind = view.kind;
		using Kind = Gram::NftKind;
		state->title.setMarkedText(
			st::walletCollectibleTitleStyle,
			((view.kind == Kind::TelegramUsername)
				? Ui::Text::Semibold('@' + view.key)
				: (view.kind == Kind::TelegramNumber)
				? Ui::Text::Semibold(Ui::FormatPhone(view.key))
				: CollectibleTitleText(view)));
		state->subtitle.setText(
			st::walletRowDateLabel.style,
			((view.kind == Kind::TelegramGift)
				? tr::lng_wallet_chip_gift(tr::now)
				: (view.kind == Kind::TelegramUsername)
				? tr::lng_wallet_chip_username(tr::now)
				: (view.kind == Kind::TelegramNumber)
				? tr::lng_wallet_chip_number(tr::now)
				: tr::lng_wallet_chip_nft(tr::now)));
		state->natural = st::walletRowIconSize
			+ st::walletChipTextSkip
			+ std::max(state->title.maxWidth(), state->subtitle.maxWidth())
			+ st::walletChipPadding.right();
		repaint();
	};
	chip->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(chip);
		PaintHistoryRowChipText(p, chip->width(), *state);
	}, chip->lifetime());
	button->setPaintUnderRipple([=](Painter &p) {
		const auto origin = Ui::MapFrom(button, chip, QPoint());
		p.translate(origin);
		PaintHistoryRowChipSurface(p, chip->width(), *state, media, address);
		p.translate(-origin);
	});
	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(refresh, chip->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next(repaint, chip->lifetime());
	media->resolveBackground(address);
	refresh();
}

void AddHistoryRow(
		not_null<Ui::VerticalLayout*> list,
		const HistoryRowContent &content,
		Fn<void()> clicked,
		std::shared_ptr<CollectibleMedia> media = nullptr) {
	const auto wrap = list->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			list,
			object_ptr<Ui::VerticalLayout>(list),
			st::walletRowPadding));
	const auto inner = wrap->entity();
	inner->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto button = Ui::CreateChild<HistoryRowButton>(
		wrap,
		rpl::single(QString()));
	button->setClickedCallback(std::move(clicked));
	const auto title = inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		content.title,
		st::walletRowTitleLabel));
	auto subtitle = (Ui::FlatLabel*)nullptr;
	if (!content.subtitle.isEmpty()) {
		Ui::AddSkip(inner, st::walletRowSkip);
		subtitle = inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			content.subtitle,
			st::walletRowSubtitleLabel));
	}
	Ui::AddSkip(inner, st::walletRowSkip);
	inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		content.date,
		st::walletRowDateLabel));
	const auto hasChip = content.itemAmount && (media != nullptr);
	if (hasChip) {
		AddHistoryRowChip(inner, button, media, content.collectible);
	}

	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMajorLabel);
	major->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMinorLabel);
	minor->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (content.itemAmount) {
		SetRowItemAmount(major, minor, content.incoming);
	} else {
		SetRowAmount(
			major,
			minor,
			content.amountNano,
			content.incoming,
			content.pending);
	}
	const auto circle = Ui::CreateChild<Ui::RpWidget>(wrap);
	circle->resize(st::walletRowIconSize, st::walletRowIconSize);
	circle->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (const auto peer = content.peer) {
		const auto userpic = circle->lifetime().make_state<
			Ui::PeerUserpicView>(peer->createUserpicView());
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			circle->update();
		}, circle->lifetime());
		circle->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(circle);
			peer->paintUserpicLeft(
				p,
				*userpic,
				0,
				0,
				circle->width(),
				circle->width());
		}, circle->lifetime());
	} else {
		const auto avatar = content.avatar;
		circle->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(circle);
			PaintRowAvatar(p, circle->rect(), avatar);
		}, circle->lifetime());
	}
	Ui::ToggleChildrenVisibility(wrap, true);
	wrap->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		const auto center = subtitle
			? (st::walletRowPadding.top()
				+ (title->height()
					+ st::walletRowSkip
					+ subtitle->height()) / 2)
			: hasChip
			? ((g.height()
				- st::walletChipTopSkip
				- st::walletRowIconSize) / 2)
			: (g.height() / 2);
		circle->moveToLeft(
			st::walletRowIconLeft,
			center - circle->height() / 2);
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

[[nodiscard]] bool ShowsCollectible(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Collectible)
		&& (item.status == TransferItem::Status::Success)
		&& !item.collectible.isEmpty();
}

[[nodiscard]] HistoryRowContent RowContentFromItem(
		const TransferItem &item,
		not_null<Main::Session*> session) {
	using Kind = TransferItem::Kind;
	const auto date = langDateTime(base::unixtime::parse(item.date));
	if (ShowsCollectible(item)) {
		const auto hasCounterparty = !item.counterparty.isEmpty();
		const auto kindText = item.incoming
			? tr::lng_wallet_row_collectible_in(tr::now)
			: tr::lng_wallet_row_collectible_out(tr::now);
		return {
			.title = (hasCounterparty
				? ShortAddress(item.counterparty)
				: kindText),
			.subtitle = (hasCounterparty ? kindText : QString()),
			.date = date,
			.incoming = item.incoming,
			.avatar = (item.incoming ? RowAvatar::In : RowAvatar::Out),
			.itemAmount = true,
			.collectible = item.collectible,
		};
	}
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	if (item.kind == Kind::CardTopUp) {
		return {
			.title = tr::lng_wallet_row_card_topup(tr::now),
			.subtitle = (pending
				? tr::lng_wallet_row_pending(tr::now)
				: item.provider),
			.date = date,
			.amountNano = item.amountNano,
			.incoming = item.incoming,
			.pending = pending,
			.avatar = RowAvatar::Card,
		};
	}
	if (item.kind == Kind::PeerTransfer && item.counterpartyPeer) {
		const auto peer = session->data().peerLoaded(
			PeerId(item.counterpartyPeer));
		if (peer) {
			return {
				.title = peer->name(),
				.subtitle = (pending
					? tr::lng_wallet_row_pending(tr::now)
					: item.incoming
					? tr::lng_wallet_row_incoming(tr::now)
					: tr::lng_wallet_row_outgoing(tr::now)),
				.date = date,
				.amountNano = item.amountNano,
				.incoming = item.incoming,
				.pending = pending,
				.avatar = RowAvatar::Peer,
				.peer = peer,
			};
		}
	}
	if (item.kind == Kind::KeyChange) {
		return {
			.title = tr::lng_wallet_row_key_change(tr::now),
			.subtitle = (pending
				? tr::lng_wallet_row_pending(tr::now)
				: QString()),
			.date = date,
			.amountNano = item.amountNano,
			.incoming = item.incoming,
			.pending = pending,
			.avatar = RowAvatar::KeyChange,
		};
	}
	const auto contract = (item.kind == Kind::ContractInteraction);
	const auto collectible = (item.kind == Kind::Collectible);
	const auto hasCounterparty = !item.counterparty.isEmpty();
	const auto kindText = contract
		? tr::lng_wallet_row_smart_contract(tr::now)
		: collectible
		? (item.incoming
			? tr::lng_wallet_row_collectible_in(tr::now)
			: tr::lng_wallet_row_collectible_out(tr::now))
		: item.incoming
		? tr::lng_wallet_row_deposit(tr::now)
		: tr::lng_wallet_row_withdrawal(tr::now);
	return {
		.title = (hasCounterparty
			? ShortAddress(item.counterparty)
			: contract
			? tr::lng_wallet_row_contract(tr::now)
			: kindText),
		.subtitle = (pending
			? tr::lng_wallet_row_pending(tr::now)
			: hasCounterparty
			? kindText
			: QString()),
		.date = date,
		.amountNano = item.amountNano,
		.incoming = item.incoming,
		.pending = pending,
		.avatar = (contract
			? RowAvatar::Contract
			: item.incoming
			? RowAvatar::In
			: RowAvatar::Out),
	};
}

[[nodiscard]] HistoryRowContent RowContentFromPending(
		const PendingSendInfo &pending) {
	return {
		.title = ShortAddress(pending.destination),
		.subtitle = tr::lng_wallet_row_pending(tr::now),
		.date = langDateTime(base::unixtime::parse(pending.posted)),
		.amountNano = pending.amountNano,
		.incoming = false,
		.pending = true,
		.avatar = RowAvatar::Out,
	};
}

[[nodiscard]] TransferItem ItemFromPending(
		const PendingSendInfo &pending) {
	auto result = TransferItem();
	result.incoming = false;
	result.counterparty = pending.destination;
	result.amountNano = pending.amountNano;
	result.comment = pending.comment;
	result.date = pending.posted;
	result.status = TransferItem::Status::Pending;
	return result;
}

void AddDetailsAmountHeader(
		not_null<Ui::GenericBox*> box,
		const TransferItem &item,
		int topSkip,
		rpl::producer<FiatRate> rate = nullptr) {
	const auto container = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(
			0,
			topSkip,
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
				st::walletDetailsMarkSize,
				st::windowActiveTextFg->c);
		},
		.margin = st::walletDetailsIconMargin,
	}));
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		container,
		st::walletDetailsAmountMinorLabel);
	minor->setMarkedText(std::move(minorText), helper.context());
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	const auto &color = pending
		? st::windowSubTextFg
		: item.incoming
		? st::boxTextFgGood
		: st::windowFg;
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		major->setTextColorOverride(color->c);
		minor->setTextColorOverride(color->c);
	}, container->lifetime());
	const auto amountNano = item.amountNano;
	const auto fiat = rate
		? Ui::CreateChild<Ui::FlatLabel>(
			container,
			st::walletDetailsFiatLabel)
		: nullptr;
	const auto relayout = [=] {
		const auto majorSize = major->size();
		const auto minorSize = minor->size();
		const auto amountWidth = majorSize.width() + minorSize.width();
		const auto amountHeight = std::max(
			majorSize.height(),
			st::walletDetailsAmountMinorSkip + minorSize.height());
		const auto withFiat = (fiat != nullptr);
		const auto width = std::max(
			amountWidth,
			withFiat ? fiat->width() : 0);
		const auto height = amountHeight + (withFiat
			? st::walletDetailsFiatSkip + fiat->height()
			: 0);
		container->resize(width, height);
		container->setNaturalWidth(width);
		major->moveToLeft((width - amountWidth) / 2, 0, width);
		minor->moveToLeft(
			(width - amountWidth) / 2 + majorSize.width(),
			st::walletDetailsAmountMinorSkip,
			width);
		if (withFiat) {
			fiat->moveToLeft(
				(width - fiat->width()) / 2,
				amountHeight + st::walletDetailsFiatSkip,
				width);
		}
	};
	if (fiat) {
		std::move(rate) | rpl::on_next([=](const FiatRate &value) {
			fiat->setText(FormatFiat(amountNano, value));
			relayout();
		}, fiat->lifetime());
	}
	rpl::combine(
		major->sizeValue(),
		minor->sizeValue()
	) | rpl::on_next(relayout, container->lifetime());
}

void AddDetailsCollectibleHeader(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::shared_ptr<CollectibleMedia> media,
		const TransferItem &item) {
	const auto container = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(
			0,
			st::walletDetailsAmountTopSkip,
			0,
			st::walletDetailsAmountBottomSkip),
		style::al_top);
	const auto address = item.collectible;
	const auto available = st::boxWideWidth
		- st::giveawayGiftCodeTableMargin.left()
		- st::giveawayGiftCodeTableMargin.right();
	const auto arrowWidth = st::walletDetailsCollectionArrowSkip
		+ st::walletDetailsCollectionArrow.width();
	const auto artwork = Ui::CreateChild<Ui::RpWidget>(container);
	artwork->resize(
		st::walletDetailsCollectibleSize,
		st::walletDetailsCollectibleSize);
	artwork->setAttribute(Qt::WA_TransparentForMouseEvents);
	artwork->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(artwork);
		media->paint(
			p,
			address,
			artwork->rect(),
			artwork->width(),
			st::walletDetailsCollectibleRadius);
	}, artwork->lifetime());
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		container,
		st::walletCollectibleTitleLabel);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto collection = Ui::CreateChild<Ui::AbstractButton>(container);
	const auto collectionLabel = Ui::CreateChild<Ui::FlatLabel>(
		collection,
		st::walletDetailsCollectionLabel);
	collectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	collection->hide();
	collection->setClickedCallback([=] {
		const auto contract = media->collection(address);
		if (!contract.isEmpty()) {
			UrlClickHandler::Open(Core::TonExplorerUrl(
				session,
				FormatFriendly(contract, true)));
		}
	});
	collection->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(collection);
		const auto &arrow = st::walletDetailsCollectionArrow;
		arrow.paint(
			p,
			collectionLabel->width() + st::walletDetailsCollectionArrowSkip,
			(collection->height() - arrow.height()) / 2,
			collection->width());
	}, collection->lifetime());
	collectionLabel->sizeValue(
	) | rpl::on_next([=](QSize size) {
		collection->resize(size.width() + arrowWidth, size.height());
		collectionLabel->moveToLeft(0, 0, collection->width());
	}, collection->lifetime());
	const auto relayout = [=] {
		const auto hasCollection = !media->collection(address).isEmpty();
		collection->setVisible(hasCollection);
		const auto nameTop = st::walletDetailsCollectibleSize
			+ st::walletDetailsCollectibleNameSkip;
		const auto collectionTop = nameTop
			+ name->height()
			+ st::walletDetailsCollectionSkip;
		const auto height = hasCollection
			? (collectionTop + collection->height())
			: (nameTop + name->height());
		container->resize(available, height);
		container->setNaturalWidth(available);
		artwork->moveToLeft((available - artwork->width()) / 2, 0, available);
		name->moveToLeft((available - name->width()) / 2, nameTop, available);
		if (hasCollection) {
			collection->moveToLeft(
				(available - collection->width()) / 2,
				collectionTop,
				available);
		}
	};
	const auto apply = [=] {
		const auto view = media->view(address);
		name->setMarkedText(CollectibleTitleText(view));
		name->resizeToNaturalWidth(available);
		const auto contract = media->collection(address);
		if (!contract.isEmpty()) {
			collectionLabel->setText(view.collectionName.isEmpty()
				? ShortAddress(contract)
				: view.collectionName);
			collectionLabel->resizeToNaturalWidth(available - arrowWidth);
		}
		relayout();
	};
	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(apply, container->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next([=] {
		artwork->update();
	}, container->lifetime());
	rpl::combine(
		name->sizeValue(),
		collection->sizeValue()
	) | rpl::on_next(relayout, container->lifetime());
	apply();
}

[[nodiscard]] object_ptr<Ui::PaddingWrap<Ui::FlatLabel>> MakeCommentBubble(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::color &bg) {
	auto result = object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		parent,
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(text),
			st::walletCommentLabel),
		st::giveawayGiftCodeValueMargin);
	const auto raw = result.data();
	const auto background = raw->lifetime().make_state<Ui::RoundRect>(
		st::boxRadius,
		bg);
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		background->paint(p, raw->rect());
	}, raw->lifetime());
	return result;
}

void AddDetailsComment(
		not_null<Ui::GenericBox*> box,
		const TransferItem &item) {
	const auto comment = item.comment.trimmed();
	if (comment.isEmpty()) {
		return;
	}
	box->addRow(
		MakeCommentBubble(box, rpl::single(comment), st::windowBg),
		style::margins(
			st::giveawayGiftCodeTableMargin.left(),
			0,
			st::giveawayGiftCodeTableMargin.right(),
			st::walletDetailsAmountBottomSkip),
		style::al_top);
}

void AddFeeTableRow(
		not_null<Ui::TableLayout*> table,
		not_null<Main::Session*> session,
		int64 feeNano,
		bool alignMarkToDigits = false) {
	auto helper = Ui::Text::CustomEmojiHelper();
	auto descriptor = Ui::Text::PaletteDependentEmoji{
		.factory = [=] {
			return Ui::Earn::IconCurrencyColored(
				table->st().defaultValue.style.font,
				st::windowActiveTextFg->c);
		},
	};
	if (alignMarkToDigits) {
		const auto &font = table->st().defaultValue.style.font;
		const auto image = descriptor.factory();
		const auto alignedTop = AlignedMarkTop(font, image);
		const auto emojiY = (font->height - st::emojiSize) / 2;
		const auto lineShift = Ui::Fixed(font->ascent) - font->fascent;
		const auto naturalTop = (lineShift + emojiY).toInt()
			+ Ui::Emoji::GetCustomSkipNormal();
		const auto marginTop = int(base::SafeRound(alignedTop - naturalTop));
		descriptor.margin = QMargins(0, marginTop, 0, 0);
	}
	const auto diamond = helper.paletteDependent(std::move(descriptor));
	auto value = FiatRateValue(
		session
	) | rpl::map([=](FiatRate rate) {
		auto fee = diamond;
		fee.append(QChar(' '));
		fee.append(Ui::FormatTonAmount(feeNano).full);
		fee.append(QChar(' '));
		fee.append(Ui::Text::Colorized(
			FormatFiat(feeNano, rate, kFeeFiatDecimals, true)));
		return fee;
	});
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_fee(),
		std::move(value),
		helper.context());
}

void AddPeerCounterpartyRows(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		not_null<Main::Session*> session,
		const TransferItem &item) {
	const auto peer = session->data().peerLoaded(
		PeerId(item.counterpartyPeer));
	if (!peer) {
		return;
	}
	Ui::AddTableRow(
		table,
		(item.incoming
			? tr::lng_wallet_details_sender()
			: tr::lng_wallet_details_recipient()),
		rpl::single(tr::marked(peer->name())));
	const auto position = table->rowsCount();
	const auto show = box->uiShow();
	const auto addresses = &session->wallet().userAddresses();
	const auto userId = peerToUser(peer->id);
	addresses->resolve({ userId }, crl::guard(table, [=] {
		const auto answer = addresses->known(userId);
		if (answer.state != UserAddressState::Known) {
			return;
		}
		if (const auto address = DetailsFriendlyAddress(answer.address)) {
			InsertAddressTableRow(table, position, show, *address);
		}
	}));
}

void AddDetailsTable(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const TransferItem &item) {
	const auto wrap = box->addRow(
		object_ptr<Ui::PaddingWrap<Ui::TableLayout>>(
			box,
			object_ptr<Ui::TableLayout>(box, st::walletDetailsTable),
			style::margins()),
		st::giveawayGiftCodeTableMargin);
	const auto bg = wrap->lifetime().make_state<Ui::RoundRect>(
		st::walletDetailsTable.radius,
		st::windowBg);
	wrap->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(wrap);
		bg->paint(p, wrap->rect());
	}, wrap->lifetime());
	const auto table = wrap->entity();
	if (item.kind == TransferItem::Kind::KeyChange) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_operation(),
			tr::lng_wallet_details_key_change(tr::marked));
	} else if (!item.counterparty.isEmpty()) {
		if (const auto address = DetailsFriendlyAddress(item.counterparty)) {
			auto label = (item.incoming
				? tr::lng_wallet_details_sender()
				: tr::lng_wallet_details_recipient());
			if (item.counterpartyName.isEmpty()) {
				Ui::AddTableRow(
					table,
					std::move(label),
					AddressValueLabel(table, box->uiShow(), *address));
			} else {
				Ui::AddTableRow(
					table,
					std::move(label),
					NameValueLabel(
						table,
						box->uiShow(),
						item.counterpartyName,
						*address));
				Ui::AddTableRow(
					table,
					tr::lng_wallet_details_address(),
					AddressValueLabel(table, box->uiShow(), *address));
			}
		}
	} else if (item.kind == TransferItem::Kind::PeerTransfer
		&& item.counterpartyPeer) {
		// A peer transfer carries no counterparty address at all: the
		// server sends none for a user counterparty and the mapping
		// synthesizes nothing, so this sheet names the same user the row
		// named and asks the per-user address store for the address half,
		// which may answer that the user has no wallet, or answer late.
		AddPeerCounterpartyRows(box, table, session, item);
	}
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	if (item.feeNano > 0 && !pending) {
		AddFeeTableRow(table, session, item.feeNano, true);
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

[[nodiscard]] QImage ReceiveQrCenter(int side, int markSide) {
	auto result = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto svg = QSvgRenderer(
		Ui::Earn::CurrencySvgColored(st::activeButtonBg->c));
	const auto skip = (side - markSide) / 2;
	svg.render(&p, QRectF(skip, skip, markSide, markSide));
	return result;
}

[[nodiscard]] QImage ReceiveQrImage(
		const QString &address,
		int size,
		int ratio,
		int quietZoneModules = 0) {
	const auto data = Qr::Encode(address, Qr::Redundancy::Quartile);
	const auto pixel = std::max(size / std::max(data.size, 1), 1);
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);
	const auto replaceSide = Qr::ReplaceSize(data, pixel * ratio);
	const auto markSide = std::min(
		st::walletReceiveMarkSize * ratio,
		replaceSide - 2 * pixel * ratio);
	image = Qr::ReplaceCenter(
		std::move(image),
		ReceiveQrCenter(replaceSide, markSide));
	if (quietZoneModules > 0) {
		const auto skip = quietZoneModules * pixel * ratio;
		auto padded = QImage(
			image.width() + 2 * skip,
			image.height() + 2 * skip,
			QImage::Format_ARGB32_Premultiplied);
		padded.fill(Qt::white);
		auto p = QPainter(&padded);
		p.drawImage(skip, skip, image);
		p.end();
		image = std::move(padded);
	}
	image.setDevicePixelRatio(ratio);
	return image;
}

struct WalletBoxTitleBar {
	not_null<Ui::FlatLabel*> title;
	not_null<Ui::FadeWrapScaled<Ui::IconButton>*> back;
	not_null<Ui::IconButton*> close;
};

[[nodiscard]] WalletBoxTitleBar AddWalletBoxTitleBar(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<bool> backShown) {
	const auto row = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::walletReceiveTitleHeight),
		style::margins(),
		style::al_justify);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		std::move(title),
		st::boxTitle);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto back = Ui::CreateChild<Ui::FadeWrapScaled<Ui::IconButton>>(
		row,
		object_ptr<Ui::IconButton>(row, st::walletReceiveTitleBack));
	const auto close = Ui::CreateChild<Ui::IconButton>(
		row,
		st::boxTitleClose);
	struct State {
		Ui::Animations::Simple titleLeft;
	};
	const auto state = row->lifetime().make_state<State>();
	const auto updateTitleLeft = [=] {
		const auto progress = state->titleLeft.value(
			back->toggled() ? 1. : 0.);
		label->moveToLeft(
			anim::interpolate(
				st::boxTitlePosition.x(),
				st::walletReceiveTitleLeft,
				progress),
			st::boxTitlePosition.y(),
			row->width());
	};
	back->toggledValue(
	) | rpl::on_next([=](bool toggled) {
		state->titleLeft.start(
			updateTitleLeft,
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, back->lifetime());
	Ui::ToggleChildrenVisibility(row, true);
	back->toggleOn(std::move(backShown));
	state->titleLeft.stop();
	row->sizeValue(
	) | rpl::on_next([=](QSize size) {
		back->moveToLeft(st::walletReceiveTitleButtonSkip, 0, size.width());
		close->moveToRight(
			st::walletReceiveTitleButtonSkip,
			0,
			size.width());
		updateTitleLeft();
	}, row->lifetime());
	return { label, back, close };
}

void AddBuySectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st::defaultSubsectionTitle),
		st::walletBuySubsectionPadding);
}

enum class OnrampRoutePresentation {
	BankCard,
	Cryptocurrency,
	P2p,
	Generic,
};

enum class OnrampRoutesViewStatus {
	Loading,
	Ready,
	Empty,
	GeographicDenied,
	BuyingDisabled,
	Error,
};

enum class OnrampDiscoveryPhase {
	Inactive,
	Providers,
	BaseCurrencies,
	Availability,
	Complete,
};

struct OnrampRenderRoute {
	OnrampRoutePresentation presentation = OnrampRoutePresentation::Generic;
	QString providerName;
	OnrampRouteSelection selection;
};

struct OnrampProviderDiscovery {
	OnrampProvider provider;
	bool eligible = false;
};

struct OnrampMethodMapping {
	QStringView method;
	OnrampRoutePresentation presentation = OnrampRoutePresentation::Generic;
};

class OnrampRoutesController final {
public:
	OnrampRoutesController(
		not_null<Main::Session*> session,
		QString address,
		std::shared_ptr<Ui::Show> show);

	[[nodiscard]] OnrampRoutesViewStatus status() const;
	[[nodiscard]] const std::vector<OnrampRenderRoute> &routes() const;
	[[nodiscard]] bool allowedCurrenciesReady() const;
	[[nodiscard]] std::vector<QString> allowedCurrencies() const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<bool> routePendingValue(
		OnrampRouteSelection selection) const;

	void activate();
	void activateRoute(const OnrampRouteSelection &selection);
	void restart();
	void invalidate();

private:
	void clearExpected();
	void publish(OnrampRoutesViewStatus status);
	void fail(bool currenciesReady);
	void advanceBaseCurrencies();
	void advanceAvailability();
	void providersLoaded(const Onramp::ProvidersState &load);
	void baseCurrenciesLoaded(const Onramp::BaseCurrenciesState &load);
	void availabilityLoaded(const Onramp::AvailabilityState &load);
	void rateChanged(const FiatRate &rate);
	[[nodiscard]] bool isCurrentRoute(
		const OnrampRouteSelection &selection) const;
	void hostedSessionLoaded(const Onramp::HostedSessionState &load);
	void clearHostedExpected();
	void failHostedSession();

	Rates *_rates = nullptr;
	Onramp *_onramp = nullptr;
	const QString _address;
	const std::shared_ptr<Ui::Show> _show;
	uint64 _epoch = 0;
	uint64 _expectedEpoch = 0;
	OnrampDiscoveryPhase _phase = OnrampDiscoveryPhase::Inactive;
	OnrampRoutesViewStatus _status = OnrampRoutesViewStatus::Loading;
	QString _currency;
	QString _expectedProvider;
	QString _expectedCurrency;
	std::vector<QString> _knownCurrencies;
	std::vector<QString> _allowedCurrencies;
	std::vector<OnrampProviderDiscovery> _providers;
	std::vector<OnrampRenderRoute> _routes;
	std::optional<OnrampRouteSelection> _expectedHostedSelection;
	rpl::variable<std::optional<OnrampRouteSelection>> _pendingHostedSelection;
	int _providerIndex = 0;
	int _resolvedProviders = 0;
	int _geographicDeniedProviders = 0;
	int _buyingDisabledProviders = 0;
	bool _active = false;
	bool _providersExpectingPending = false;
	bool _providersPendingSeen = false;
	bool _baseCurrenciesExpectingPending = false;
	bool _baseCurrenciesPendingSeen = false;
	bool _availabilityExpectingPending = false;
	bool _availabilityPendingSeen = false;
	bool _hostedExpectingPending = false;
	bool _hostedPendingSeen = false;
	bool _allowedCurrenciesReady = false;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

constexpr auto kOnrampMethodMappings = std::array{
	OnrampMethodMapping{
		.method = u"p2p_express",
		.presentation = OnrampRoutePresentation::P2p,
	},
	OnrampMethodMapping{
		.method = u"credit_debit_card",
		.presentation = OnrampRoutePresentation::BankCard,
	},
};

[[nodiscard]] auto OnrampPresentationForMethod(const QString &method)
-> std::optional<OnrampRoutePresentation> {
	for (const auto &mapping : kOnrampMethodMappings) {
		if (mapping.method == QStringView(method)) {
			return mapping.presentation;
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool IsValidOnrampUrl(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	return parsed.isValid()
		&& parsed.scheme() == u"https"_q
		&& !parsed.host().isEmpty();
}

void AddOnrampCurrency(
		std::vector<QString> &currencies,
		QString currency) {
	currency = currency.toUpper();
	if (currency.isEmpty() || ranges::contains(currencies, currency)) {
		return;
	}
	currencies.push_back(std::move(currency));
}

[[nodiscard]] std::vector<QString> NormalizeOnrampCurrencies(
		const std::vector<QString> &currencies) {
	auto result = std::vector<QString>();
	result.reserve(currencies.size());
	for (const auto &currency : currencies) {
		AddOnrampCurrency(result, currency);
	}
	return result;
}

[[nodiscard]] bool OnrampRouteExists(
		const std::vector<OnrampRenderRoute> &routes,
		const QString &provider,
		const std::optional<QString> &paymentMethod) {
	return ranges::find_if(routes, [&](const OnrampRenderRoute &route) {
		return (route.selection.provider == provider)
			&& (route.selection.paymentMethod == paymentMethod);
	}) != end(routes);
}

[[nodiscard]] rpl::producer<QString> OnrampProviderText(
		QString name,
		const QString &provider) {
	name = name.trimmed();
	if (name.isEmpty() || name == provider) {
		return tr::lng_wallet_buy_provider();
	}
	return rpl::single(std::move(name));
}

[[nodiscard]] OnrampRoutesViewStatus OnrampTerminalStatus(
		const std::vector<OnrampRenderRoute> &routes,
		int resolvedProviders,
		int geographicDeniedProviders,
		int buyingDisabledProviders) {
	if (!routes.empty()) {
		return OnrampRoutesViewStatus::Ready;
	} else if (resolvedProviders > 0
		&& geographicDeniedProviders == resolvedProviders) {
		return OnrampRoutesViewStatus::GeographicDenied;
	} else if (resolvedProviders > 0
		&& buyingDisabledProviders == resolvedProviders) {
		return OnrampRoutesViewStatus::BuyingDisabled;
	}
	return OnrampRoutesViewStatus::Empty;
}

void AddBuyRow(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle,
		const style::icon *icon,
		const style::color *background,
		rpl::producer<bool> pending,
		Fn<void()> activate) {
	const auto wrap = container->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container),
			st::walletBuyRowPadding));
	const auto inner = wrap->entity();
	inner->setAttribute(Qt::WA_TransparentForMouseEvents);
	inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		std::move(title),
		st::walletBuyRowTitle));
	Ui::AddSkip(inner, st::walletBuyRowSkip);
	inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		std::move(subtitle),
		st::walletBuyRowSubtitle));

	const auto button = Ui::CreateChild<Ui::SettingsButton>(
		wrap,
		rpl::single(QString()),
		st::walletBuyRow);
	if (icon && background) {
		Settings::AddButtonIcon(button, st::walletBuyRow, {
			.icon = icon,
			.type = Settings::IconType::Rounded,
			.background = background,
		});
	}
	const auto arrow = Ui::CreateChild<Ui::IconButton>(
		button,
		st::backButton);
	arrow->setIconOverride(
		&st::settingsPremiumArrow,
		&st::settingsPremiumArrowOver);
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		st::boxLoadingSize,
		&st::boxLoadingAnimation);
	loading->setAttribute(Qt::WA_TransparentForMouseEvents);
	std::move(pending) | rpl::on_next([=](bool pending) {
		button->setDisabled(pending);
		arrow->setVisible(!pending);
		loading->setVisible(pending);
	}, button->lifetime());
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto &shift = st::settingsPremiumArrowShift;
		arrow->moveToRight(
			-shift.x(),
			shift.y() + (size.height() - arrow->height()) / 2);
		loading->moveToRight(
			-shift.x() + (arrow->width() - loading->width()) / 2,
			shift.y() + (size.height() - loading->height()) / 2);
	}, arrow->lifetime());
	button->setClickedCallback(std::move(activate));
	Ui::ToggleChildrenVisibility(wrap, true);
	wrap->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		button->resize(g.size());
		button->lower();
	}, wrap->lifetime());
}

void AddOnrampInformation(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		bool loading) {
	const auto wrap = container->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container),
			st::walletBuyRowPadding));
	const auto inner = wrap->entity();
	if (loading) {
		using namespace Info::Statistics;
		const auto envelope = inner->add(
			object_ptr<Ui::FixedHeightWidget>(
				inner,
				st::boxLoadingSize));
		const auto animation = InfiniteRadialAnimationWidget(
			envelope,
			st::boxLoadingSize,
			&st::boxLoadingAnimation);
		AddChildToWidgetCenter(envelope, animation);
		Ui::AddSkip(inner, st::walletBuyRowSkip);
	}
	inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		std::move(text),
		st::walletBuyRowSubtitle));
	Ui::ToggleChildrenVisibility(wrap, true);
}

OnrampRoutesController::OnrampRoutesController(
		not_null<Main::Session*> session,
		QString address,
		std::shared_ptr<Ui::Show> show)
: _rates(&session->wallet().rates())
, _onramp(&session->wallet().onramp())
, _address(std::move(address))
, _show(std::move(show)) {
	_onramp->providersValue(
	) | rpl::on_next([=](const Onramp::ProvidersState &load) {
		providersLoaded(load);
	}, _lifetime);
	_onramp->baseCurrenciesValue(
	) | rpl::on_next([=](const Onramp::BaseCurrenciesState &load) {
		baseCurrenciesLoaded(load);
	}, _lifetime);
	_onramp->availabilityValue(
	) | rpl::on_next([=](const Onramp::AvailabilityState &load) {
		availabilityLoaded(load);
	}, _lifetime);
	_onramp->routeSelections(
	) | rpl::on_next([=](const OnrampRouteSelection &) {
		clearHostedExpected();
	}, _lifetime);
	_onramp->hostedSessionValue(
	) | rpl::on_next([=](const Onramp::HostedSessionState &load) {
		hostedSessionLoaded(load);
	}, _lifetime);
	_rates->value(
	) | rpl::on_next([=](const FiatRate &rate) {
		rateChanged(rate);
	}, _lifetime);
}

OnrampRoutesViewStatus OnrampRoutesController::status() const {
	return _status;
}

auto OnrampRoutesController::routes() const
-> const std::vector<OnrampRenderRoute> & {
	return _routes;
}

bool OnrampRoutesController::allowedCurrenciesReady() const {
	return _allowedCurrenciesReady;
}

std::vector<QString> OnrampRoutesController::allowedCurrencies() const {
	return _allowedCurrencies;
}

rpl::producer<> OnrampRoutesController::changes() const {
	return _changes.events();
}

rpl::producer<bool> OnrampRoutesController::routePendingValue(
		OnrampRouteSelection selection) const {
	return _pendingHostedSelection.value(
	) | rpl::map([selection = std::move(selection)](
			const std::optional<OnrampRouteSelection> &pending) {
		return pending && (*pending == selection);
	}) | rpl::distinct_until_changed();
}

void OnrampRoutesController::activate() {
	_active = true;
	restart();
}

void OnrampRoutesController::restart() {
	clearHostedExpected();
	if (!_active) {
		return;
	}
	++_epoch;
	clearExpected();
	_phase = OnrampDiscoveryPhase::Providers;
	_currency = _rates->current().currency.toUpper();
	_knownCurrencies = NormalizeOnrampCurrencies(_rates->currencies());
	AddOnrampCurrency(_knownCurrencies, _currency);
	_allowedCurrencies.clear();
	_providers.clear();
	_routes.clear();
	_providerIndex = 0;
	_resolvedProviders = 0;
	_geographicDeniedProviders = 0;
	_buyingDisabledProviders = 0;
	_allowedCurrenciesReady = false;
	_expectedEpoch = _epoch;
	_providersExpectingPending = true;
	_providersPendingSeen = false;
	publish(OnrampRoutesViewStatus::Loading);
	_onramp->requestProvidersForGram();
	if (_phase == OnrampDiscoveryPhase::Providers
		&& _expectedEpoch == _epoch) {
		_providersPendingSeen = _providersPendingSeen
			|| _onramp->providersCurrent().pending;
		_providersExpectingPending = false;
	}
}

void OnrampRoutesController::invalidate() {
	clearHostedExpected();
	if (!_active && _phase == OnrampDiscoveryPhase::Inactive) {
		return;
	}
	_active = false;
	++_epoch;
	clearExpected();
	_phase = OnrampDiscoveryPhase::Inactive;
	_currency.clear();
	_knownCurrencies.clear();
	_allowedCurrencies.clear();
	_providers.clear();
	_routes.clear();
	_providerIndex = 0;
	_resolvedProviders = 0;
	_geographicDeniedProviders = 0;
	_buyingDisabledProviders = 0;
	_allowedCurrenciesReady = false;
}

void OnrampRoutesController::clearExpected() {
	_expectedEpoch = 0;
	_expectedProvider.clear();
	_expectedCurrency.clear();
	_providersExpectingPending = false;
	_providersPendingSeen = false;
	_baseCurrenciesExpectingPending = false;
	_baseCurrenciesPendingSeen = false;
	_availabilityExpectingPending = false;
	_availabilityPendingSeen = false;
}

void OnrampRoutesController::publish(OnrampRoutesViewStatus status) {
	_status = status;
	_changes.fire({});
}

void OnrampRoutesController::fail(bool currenciesReady) {
	clearHostedExpected();
	clearExpected();
	_phase = OnrampDiscoveryPhase::Complete;
	_routes.clear();
	if (!currenciesReady) {
		_allowedCurrencies.clear();
		_allowedCurrenciesReady = false;
	}
	publish(OnrampRoutesViewStatus::Error);
}

void OnrampRoutesController::advanceBaseCurrencies() {
	if (!_active || _phase != OnrampDiscoveryPhase::BaseCurrencies) {
		return;
	}
	clearExpected();
	while (_providerIndex < int(_providers.size())) {
		auto &entry = _providers[_providerIndex];
		if (entry.provider.supportsBaseCurrencies) {
			const auto providerId = entry.provider.id;
			_expectedEpoch = _epoch;
			_expectedProvider = providerId;
			_baseCurrenciesExpectingPending = true;
			_baseCurrenciesPendingSeen = false;
			_onramp->requestBaseCurrencies(providerId);
			if (_phase == OnrampDiscoveryPhase::BaseCurrencies
				&& _expectedEpoch == _epoch
				&& _expectedProvider == providerId) {
				_baseCurrenciesPendingSeen = _baseCurrenciesPendingSeen
					|| _onramp->baseCurrenciesCurrent().pending;
				_baseCurrenciesExpectingPending = false;
			}
			return;
		}
		entry.eligible = true;
		_allowedCurrencies = _knownCurrencies;
		++_providerIndex;
	}
	_allowedCurrenciesReady = true;
	_phase = OnrampDiscoveryPhase::Availability;
	_providerIndex = 0;
	_changes.fire({});
	advanceAvailability();
}

void OnrampRoutesController::advanceAvailability() {
	if (!_active || _phase != OnrampDiscoveryPhase::Availability) {
		return;
	}
	clearExpected();
	while (_providerIndex < int(_providers.size())
		&& !_providers[_providerIndex].eligible) {
		++_providerIndex;
	}
	if (_providerIndex == int(_providers.size())) {
		_phase = OnrampDiscoveryPhase::Complete;
		publish(OnrampTerminalStatus(
			_routes,
			_resolvedProviders,
			_geographicDeniedProviders,
			_buyingDisabledProviders));
		return;
	}
	const auto providerId = _providers[_providerIndex].provider.id;
	_expectedEpoch = _epoch;
	_expectedProvider = providerId;
	_expectedCurrency = _currency.toLower();
	_availabilityExpectingPending = true;
	_availabilityPendingSeen = false;
	_onramp->requestAvailability(providerId, _expectedCurrency);
	if (_phase == OnrampDiscoveryPhase::Availability
		&& _expectedEpoch == _epoch
		&& _expectedProvider == providerId
		&& _expectedCurrency == _currency.toLower()) {
		_availabilityPendingSeen = _availabilityPendingSeen
			|| _onramp->availabilityCurrent().pending;
		_availabilityExpectingPending = false;
	}
}

void OnrampRoutesController::providersLoaded(
		const Onramp::ProvidersState &load) {
	if (!_active
		|| _phase != OnrampDiscoveryPhase::Providers
		|| _expectedEpoch != _epoch) {
		return;
	}
	if (load.pending) {
		_providersPendingSeen = _providersExpectingPending;
		return;
	} else if (!_providersPendingSeen && !_providersExpectingPending) {
		return;
	} else if (load.error) {
		fail(false);
		return;
	} else if (!load.value) {
		return;
	}
	clearExpected();
	_providers.clear();
	_providers.reserve(load.value->size());
	for (const auto &provider : *load.value) {
		if (provider.id.isEmpty()
			|| ranges::find_if(
				_providers,
				[&](const OnrampProviderDiscovery &entry) {
					return entry.provider.id == provider.id;
				}) != end(_providers)) {
			continue;
		}
		_providers.push_back({ provider });
	}
	_phase = OnrampDiscoveryPhase::BaseCurrencies;
	_providerIndex = 0;
	advanceBaseCurrencies();
}

void OnrampRoutesController::baseCurrenciesLoaded(
		const Onramp::BaseCurrenciesState &load) {
	if (!_active
		|| _phase != OnrampDiscoveryPhase::BaseCurrencies
		|| _expectedEpoch != _epoch
		|| _providerIndex >= int(_providers.size())
		|| _expectedProvider != _providers[_providerIndex].provider.id) {
		return;
	}
	if (load.pending) {
		_baseCurrenciesPendingSeen = _baseCurrenciesExpectingPending;
		return;
	} else if (!_baseCurrenciesPendingSeen
		&& !_baseCurrenciesExpectingPending) {
		return;
	} else if (load.error) {
		fail(false);
		return;
	} else if (!load.value) {
		return;
	}
	const auto supported = NormalizeOnrampCurrencies(*load.value);
	auto &entry = _providers[_providerIndex];
	entry.eligible = ranges::contains(supported, _currency);
	for (const auto &currency : _knownCurrencies) {
		if (ranges::contains(supported, currency)) {
			AddOnrampCurrency(_allowedCurrencies, currency);
		}
	}
	++_providerIndex;
	advanceBaseCurrencies();
}

void OnrampRoutesController::availabilityLoaded(
		const Onramp::AvailabilityState &load) {
	if (!_active
		|| _phase != OnrampDiscoveryPhase::Availability
		|| _expectedEpoch != _epoch
		|| _providerIndex >= int(_providers.size())) {
		return;
	}
	const auto &provider = _providers[_providerIndex].provider;
	if (_expectedProvider != provider.id
		|| _expectedCurrency != _currency.toLower()) {
		return;
	}
	if (load.pending) {
		_availabilityPendingSeen = _availabilityExpectingPending;
		return;
	} else if (!_availabilityPendingSeen
		&& !_availabilityExpectingPending) {
		return;
	} else if (load.error) {
		fail(true);
		return;
	} else if (!load.value) {
		return;
	}

	++_resolvedProviders;
	const auto &availability = *load.value;
	if (!availability.allowed) {
		++_geographicDeniedProviders;
	} else if (!availability.buyAllowed) {
		++_buyingDisabledProviders;
	} else {
		auto hasAvailableMethod = false;
		auto hasMappedRoute = false;
		for (const auto &method : availability.methods) {
			if (!method.available) {
				continue;
			}
			hasAvailableMethod = true;
			const auto presentation = OnrampPresentationForMethod(
				method.paymentMethod);
			if (!presentation) {
				continue;
			}
			hasMappedRoute = true;
			const auto paymentMethod = std::optional<QString>(
				method.paymentMethod);
			if (OnrampRouteExists(_routes, provider.id, paymentMethod)) {
				continue;
			}
			_routes.push_back({
				.presentation = *presentation,
				.providerName = provider.name,
				.selection = {
					.provider = provider.id,
					.paymentMethod = paymentMethod,
					.baseCurrency = _expectedCurrency,
				},
			});
		}
		if (hasAvailableMethod && !hasMappedRoute) {
			const auto paymentMethod = std::optional<QString>();
			if (!OnrampRouteExists(_routes, provider.id, paymentMethod)) {
				_routes.push_back({
					.presentation = OnrampRoutePresentation::Generic,
					.providerName = provider.name,
					.selection = {
						.provider = provider.id,
						.paymentMethod = std::nullopt,
						.baseCurrency = _expectedCurrency,
					},
				});
			}
		}
	}
	++_providerIndex;
	advanceAvailability();
}

void OnrampRoutesController::rateChanged(const FiatRate &rate) {
	const auto currency = rate.currency.toUpper();
	if (_active && currency != _currency) {
		restart();
	}
}

bool OnrampRoutesController::isCurrentRoute(
		const OnrampRouteSelection &selection) const {
	return _active
		&& _phase == OnrampDiscoveryPhase::Complete
		&& _status == OnrampRoutesViewStatus::Ready
		&& ranges::find_if(
			_routes,
			[&](const OnrampRenderRoute &route) {
				return route.selection == selection;
			}) != end(_routes);
}

void OnrampRoutesController::activateRoute(
		const OnrampRouteSelection &selection) {
	if (!isCurrentRoute(selection)) {
		return;
	}
	const auto pending = _pendingHostedSelection.current();
	if (pending && *pending == selection) {
		return;
	}
	_onramp->selectRoute(selection);
	_expectedHostedSelection = selection;
	_pendingHostedSelection = std::optional<OnrampRouteSelection>(selection);
	_hostedExpectingPending = true;
	_hostedPendingSeen = false;
	auto args = OnrampSessionArgs{
		.provider = selection.provider,
		.address = _address,
		.paymentMethod = selection.paymentMethod,
		.baseCurrency = selection.baseCurrency.toLower(),
	};
	if (selection.provider == u"moonpay"_q) {
		args.theme = Window::Theme::IsNightMode()
			? u"dark"_q
			: u"light"_q;
	}
	_onramp->createSession(args);
	if (_expectedHostedSelection
		&& *_expectedHostedSelection == selection) {
		_hostedPendingSeen = _hostedPendingSeen
			|| _onramp->hostedSessionCurrent().pending;
		_hostedExpectingPending = false;
	}
}

void OnrampRoutesController::hostedSessionLoaded(
		const Onramp::HostedSessionState &load) {
	if (!_expectedHostedSelection
		|| !isCurrentRoute(*_expectedHostedSelection)) {
		return;
	}
	const auto pending = _pendingHostedSelection.current();
	if (!pending || *pending != *_expectedHostedSelection) {
		return;
	}
	if (load.pending) {
		_hostedPendingSeen = _hostedExpectingPending;
		return;
	} else if (!_hostedPendingSeen && !_hostedExpectingPending) {
		return;
	} else if (load.error) {
		failHostedSession();
		return;
	} else if (!load.value) {
		return;
	}
	const auto &session = *load.value;
	if (session.provider != _expectedHostedSelection->provider
		|| session.expiresDate <= base::unixtime::now()
		|| !IsValidOnrampUrl(session.url)) {
		failHostedSession();
		return;
	}
	const auto url = session.url;
	clearHostedExpected();
	File::OpenUrl(url);
}

void OnrampRoutesController::clearHostedExpected() {
	_expectedHostedSelection = std::nullopt;
	_hostedExpectingPending = false;
	_hostedPendingSeen = false;
	_pendingHostedSelection = std::nullopt;
}

void OnrampRoutesController::failHostedSession() {
	clearHostedExpected();
	_show->showToast(tr::lng_wallet_buy_session_error(tr::now));
}

void RenderOnrampRoutes(
		not_null<Ui::VerticalLayout*> routeList,
		OnrampRoutesController &controller,
		Fn<void()> retry) {
	routeList->clear();
	switch (controller.status()) {
	case OnrampRoutesViewStatus::Loading:
		AddOnrampInformation(
			routeList,
			tr::lng_wallet_buy_loading(),
			true);
		break;
	case OnrampRoutesViewStatus::Ready:
		for (const auto &route : controller.routes()) {
			auto providerText = OnrampProviderText(
				route.providerName,
				route.selection.provider);
			auto activate = [
					controller = &controller,
					selection = route.selection] {
				controller->activateRoute(selection);
			};
			switch (route.presentation) {
			case OnrampRoutePresentation::BankCard:
				AddBuyRow(
					routeList,
					tr::lng_wallet_buy_card(),
					std::move(providerText),
					&st::walletBuyBankCardIcon,
					&st::settingsIconBg2,
					controller.routePendingValue(route.selection),
					std::move(activate));
				break;
			case OnrampRoutePresentation::Cryptocurrency:
				AddBuyRow(
					routeList,
					tr::lng_wallet_buy_crypto(),
					std::move(providerText),
					&st::walletBuyCryptoIcon,
					&st::settingsIconBg3,
					controller.routePendingValue(route.selection),
					std::move(activate));
				break;
			case OnrampRoutePresentation::P2p:
				AddBuyRow(
					routeList,
					tr::lng_wallet_buy_p2p(),
					std::move(providerText),
					&st::walletBuyP2pIcon,
					&st::settingsIconBg4,
					controller.routePendingValue(route.selection),
					std::move(activate));
				break;
			case OnrampRoutePresentation::Generic:
				AddBuyRow(
					routeList,
					tr::lng_wallet_buy_provider(),
					std::move(providerText),
					nullptr,
					nullptr,
					controller.routePendingValue(route.selection),
					std::move(activate));
				break;
			}
		}
		break;
	case OnrampRoutesViewStatus::Empty:
		AddOnrampInformation(
			routeList,
			tr::lng_wallet_buy_empty(),
			false);
		break;
	case OnrampRoutesViewStatus::GeographicDenied:
		AddOnrampInformation(
			routeList,
			tr::lng_wallet_buy_geographic_denied(),
			false);
		break;
	case OnrampRoutesViewStatus::BuyingDisabled:
		AddOnrampInformation(
			routeList,
			tr::lng_wallet_buy_disabled(),
			false);
		break;
	case OnrampRoutesViewStatus::Error: {
		AddOnrampInformation(
			routeList,
			tr::lng_wallet_buy_error(),
			false);
		const auto retryButton = routeList->add(
			object_ptr<Ui::RoundButton>(
				routeList,
				tr::lng_wallet_buy_retry(),
				st::walletReceiveBuyButton),
			st::walletReceiveBuyMargin,
			style::al_justify);
		retryButton->setTextTransform(
			Ui::RoundButtonTextTransform::NoTransform);
		retryButton->setClickedCallback(std::move(retry));
		retryButton->widthValue(
		) | rpl::on_next([=](int width) {
			retryButton->setFullWidth(width);
		}, retryButton->lifetime());
		break;
	}
	}
	Ui::AddSkip(routeList, st::walletBuySectionBottomSkip);
	if (const auto width = routeList->width()) {
		routeList->resizeToWidth(width);
	}
}

void WalletChooseCurrencyBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<std::vector<QString>> allowedCodes);

void WalletReceiveBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &address) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletReceiveBox);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop | RectPart::FullBottom);

	struct State {
		rpl::variable<bool> buying = false;
		Ui::Animations::Simple buyingAnimation;
		QImage image;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto show = box->uiShow();
	const auto routes = box->lifetime().make_state<OnrampRoutesController>(
		session,
		address,
		show);
	const auto sessionShow = Main::MakeSessionShow(show, session);
	state->buying.changes(
	) | rpl::on_next([=](bool buying) {
		if (buying) {
			routes->activate();
		} else {
			routes->invalidate();
		}
	}, box->lifetime());
	box->boxClosing(
	) | rpl::on_next([=] {
		routes->invalidate();
	}, box->lifetime());

	box->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(box);
		auto hq = PainterHighQualityEnabler(p);
		const auto shown = state->buyingAnimation.value(
			state->buying.current() ? 1. : 0.);
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(st::activeButtonBg, st::boxBg, shown));
		p.drawRoundedRect(box->rect(), st::boxRadius, st::boxRadius);
	}, box->lifetime());
	state->buying.changes(
	) | rpl::on_next([=](bool buying) {
		state->buyingAnimation.start([=] {
			box->update();
		}, buying ? 0. : 1., buying ? 1. : 0., st::slideWrapDuration);
	}, box->lifetime());

	const auto bar = AddWalletBoxTitleBar(
		box,
		state->buying.value(
		) | rpl::map([](bool buying) {
			return buying
				? tr::lng_wallet_buy_title()
				: tr::lng_wallet_add_funds();
		}) | rpl::flatten_latest(),
		state->buying.value());
	rpl::combine(
		state->buying.value(),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::on_next([=](bool buying, rpl::empty_value) {
		bar.title->setTextColorOverride(buying
			? std::optional<QColor>()
			: st::activeButtonFg->c);
		bar.close->setIconOverride(
			buying ? nullptr : &st::walletReceiveCloseIconActive,
			buying ? nullptr : &st::walletReceiveCloseIconActiveOver);
		const auto ripple = buying ? nullptr : &st::activeButtonBgRipple;
		bar.close->setRippleColorOverride(ripple);
	}, box->lifetime());
	bar.close->setClickedCallback([=] {
		box->closeBox();
	});
	bar.back->entity()->setClickedCallback([=] {
		state->buying = false;
	});

	const auto addFunds = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins(),
		style::al_justify);
	const auto inner = addFunds->entity();
	addFunds->toggleOn(state->buying.value(
	) | rpl::map([](bool buying) {
		return !buying;
	}));
	addFunds->finishAnimating();

	state->image = ReceiveQrImage(
		address,
		st::walletReceiveQrSize,
		style::DevicePixelRatio());
	const auto qrSide = state->image.width() / style::DevicePixelRatio();
	const auto &padding = st::walletReceivePlatePadding;
	const auto font = st::walletReceiveAddressFont->monospace();
	const auto hintFont = st::walletReceiveHintFont;
	const auto groupWidth = font->width(address.left(kAddressGroup));
	const auto spaceWidth = font->width(QChar(' '));
	const auto lineWidth = kReceiveGroupsPerLine * groupWidth
		+ (kReceiveGroupsPerLine - 1) * spaceWidth;
	const auto lineHeight = font->height + st::walletReceiveAddressLineSkip;
	const auto blockHeight = kReceiveLines * font->height
		+ (kReceiveLines - 1) * st::walletReceiveAddressLineSkip;
	const auto qrLeft = padding.left();
	const auto qrTop = padding.top();
	const auto addressTop = qrTop + qrSide + st::walletReceiveAddressTopSkip;
	const auto hintTop = addressTop
		+ blockHeight
		+ st::walletReceiveHintTopSkip;
	const auto plateWidth = std::max(qrSide, lineWidth)
		+ padding.left()
		+ padding.right();
	const auto plateHeight = hintTop + hintFont->height + padding.bottom();

	auto plateOwned = object_ptr<Ui::FixedHeightWidget>(inner, plateHeight);
	plateOwned->setNaturalWidth(plateWidth);
	const auto plate = inner->add(
		std::move(plateOwned),
		st::walletReceivePlateMargin,
		style::al_top);
	plate->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			plate->rect(),
			st::walletReceivePlateRadius,
			st::walletReceivePlateRadius);
		p.drawImage(qrLeft, qrTop, state->image);
		p.setFont(font);
		const auto left = (plate->width() - lineWidth) / 2;
		for (auto i = 0; i != kAddressLength / kAddressGroup; ++i) {
			const auto line = i / kReceiveGroupsPerLine;
			const auto column = i % kReceiveGroupsPerLine;
			p.setPen((i % 2)
				? QColor(0x99, 0x99, 0x99)
				: QColor(0x22, 0x22, 0x22));
			p.drawText(
				left + column * (groupWidth + spaceWidth),
				addressTop + line * lineHeight + font->ascent,
				address.mid(i * kAddressGroup, kAddressGroup));
		}
		const auto hint = hintFont->elided(
			tr::lng_wallet_receive_copy_hint(tr::now),
			plate->width() - padding.left() - padding.right());
		p.setFont(hintFont);
		p.setPen(QColor(0x99, 0x99, 0x99));
		p.drawText(
			(plate->width() - hintFont->width(hint)) / 2,
			hintTop + hintFont->ascent,
			hint);
	}, plate->lifetime());
	style::PaletteChanged(
	) | rpl::on_next([=] {
		state->image = ReceiveQrImage(
			address,
			st::walletReceiveQrSize,
			style::DevicePixelRatio());
		plate->update();
	}, plate->lifetime());

	const auto qrTarget = Ui::CreateChild<Ui::AbstractButton>(plate);
	qrTarget->setClickedCallback([=] {
		QGuiApplication::clipboard()->setImage(ReceiveQrImage(
			address,
			st::walletReceiveQrCopySize,
			1,
			kQrQuietZoneModules));
		show->showToast({
			.text = { tr::lng_group_invite_qr_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
	const auto textTarget = Ui::CreateChild<Ui::AbstractButton>(plate);
	textTarget->setClickedCallback([=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
	Ui::ToggleChildrenVisibility(plate, true);
	const auto textTop = addressTop - st::walletReceiveAddressTopSkip / 2;
	plate->sizeValue(
	) | rpl::on_next([=](QSize size) {
		qrTarget->setGeometry(qrLeft, qrTop, qrSide, qrSide);
		textTarget->setGeometry(
			0,
			textTop,
			size.width(),
			size.height() - textTop);
	}, plate->lifetime());

	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_wallet_receive_about(),
			st::walletReceiveAboutLabel),
		st::walletReceiveAboutMargin,
		style::al_top);

	const auto buy = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			tr::lng_wallet_buy_button(),
			st::walletReceiveBuyButton),
		st::walletReceiveBuyMargin,
		style::al_justify);
	buy->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	buy->setClickedCallback([=] {
		state->buying = true;
	});
	buy->widthValue(
	) | rpl::on_next([=](int width) {
		buy->setFullWidth(width);
	}, buy->lifetime());

	const auto buyGrams = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins(),
		style::al_justify);
	const auto buyInner = buyGrams->entity();

	AddBuySectionTitle(buyInner, tr::lng_wallet_buy_pay_with());
	const auto currencyButton = Settings::AddButtonWithLabel(
		buyInner,
		tr::lng_wallet_menu_currency(),
		FiatRateValue(session) | rpl::map([](const FiatRate &rate) {
			return rate.currency;
		}) | rpl::distinct_until_changed(),
		st::settingsButtonNoIcon);
	currencyButton->setClickedCallback([=] {
		if (!routes->allowedCurrenciesReady()) {
			return;
		}
		sessionShow->showBox(Box(
			WalletChooseCurrencyBox,
			sessionShow,
			std::optional<std::vector<QString>>(
				routes->allowedCurrencies())));
	});
	Ui::AddSkip(buyInner, st::walletBuySectionBottomSkip);

	Ui::AddDivider(buyInner);
	AddBuySectionTitle(buyInner, tr::lng_wallet_buy_buy_with());
	const auto routeList = buyInner->add(
		object_ptr<Ui::VerticalLayout>(buyInner));
	const auto rebuildRoutes = [=] {
		currencyButton->setDisabled(!routes->allowedCurrenciesReady());
		RenderOnrampRoutes(routeList, *routes, [=] {
			routes->restart();
		});
	};
	rebuildRoutes();
	routes->changes(
	) | rpl::on_next(rebuildRoutes, routeList->lifetime());

	buyGrams->toggleOn(state->buying.value());
	buyGrams->finishAnimating();
}

void ShowWalletReceiveBox(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show) {
	auto &wallet = session->wallet();
	const auto address = wallet.addressFriendly(false);
	if (address.size() != kAddressLength) {
		return;
	}
	show->showBox(Box(WalletReceiveBox, session, address));
}

void WalletHowItWorksBox(not_null<Ui::GenericBox*> box) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	auto icon = Settings::CreateLottieIcon(
		box->verticalLayout(),
		{
			.name = u"diamond"_q,
			.sizeOverride = {
				st::walletHowLottieSize,
				st::walletHowLottieSize,
			},
		},
		st::walletHowLottieMargin);
	box->verticalLayout()->add(std::move(icon.widget));
	box->showFinishes() | rpl::on_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::loop);
	}, box->lifetime());

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
		st::walletHowSubtitleMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			.icon = st::walletAboutInstantIcon,
			.title = tr::lng_wallet_about_instant_title(tr::now),
			.about = tr::lng_wallet_about_instant_text(tr::now, tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletAboutFeesIcon,
			.title = tr::lng_wallet_about_fees_title(tr::now),
			.about = tr::lng_wallet_about_fees_text(tr::now, tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletAboutChainIcon,
			.title = tr::lng_wallet_about_chain_title(tr::now),
			.about = tr::lng_wallet_about_chain_text(tr::now, tr::marked),
			.similarLines = true,
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
		not_null<Ui::RpWidget*> card,
		Fn<QRect()> markRect,
		rpl::producer<> moves) {
	struct State {
		Ui::GlareTooltip *tooltip = nullptr;
		base::Timer hide;
		bool started = false;
		bool finished = false;
	};
	const auto state = parent->lifetime().make_state<State>();
	state->tooltip = Ui::CreateChild<Ui::GlareTooltip>(
		parent.get(),
		st::walletIntroTooltip,
		st::walletIntroTooltipFont,
		tr::lng_wallet_intro_text(tr::now),
		Ui::GlareTooltipColors{
			.edge = st::windowActiveTextFg->c,
			.center = anim::color(
				st::windowActiveTextFg,
				st::activeButtonFg,
				0.35),
			.rim = st::activeButtonFg->c,
			.text = st::activeButtonFg->c,
		});
	state->tooltip->finishAnimating();
	const auto finish = [=] {
		if (state->finished) {
			return;
		}
		state->finished = true;
		state->hide.cancel();
		state->tooltip->stopGlare();
		state->tooltip->fade(false);
	};
	state->hide.setCallback(finish);

	rpl::merge(
		card->geometryValue() | rpl::to_empty,
		parent->widthValue() | rpl::to_empty,
		std::move(moves)
	) | rpl::on_next([=] {
		if (state->finished || !parent->width()) {
			return;
		}
		const auto mark = markRect();
		if (mark.isEmpty()) {
			if (state->started) {
				finish();
			}
			return;
		}
		state->tooltip->pointAt(
			mark,
			Ui::MapFrom(parent, card, card->rect()));
		if (!state->started) {
			state->started = true;
			state->tooltip->fade(true);
			state->hide.callOnce(
				state->tooltip->glaresDuration(kWalletIntroGlares));
		}
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
		TransferItem item,
		std::shared_ptr<CollectibleMedia> media) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletDetailsBox);
	box->setNoContentMargin(true);
	box->setTitle(tr::lng_wallet_details_title());

	if (ShowsCollectible(item)) {
		if (!media) {
			media = std::make_shared<CollectibleMedia>(session);
		}
		media->resolve(item.collectible);
		AddDetailsCollectibleHeader(box, session, std::move(media), item);
	} else {
		AddDetailsAmountHeader(
			box,
			item,
			st::walletDetailsAmountTopSkip,
			FiatRateValue(session));
	}
	AddDetailsComment(box, item);
	AddDetailsTable(box, session, item);

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
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		std::shared_ptr<CollectibleMedia> media = nullptr) {
	show->showBox(Box(
		WalletTransactionBox,
		&show->session(),
		item,
		std::move(media)));

	const auto local = &show->session().local();
	if (local->readPref<bool>(kIntroToastShownPref)) {
		return;
	}
	local->writePref<bool>(kIntroToastShownPref, true);
	show->showToast({
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

void ApplyCommentLimit(not_null<Ui::InputField*> field) {
	Ui::AddLengthLimitLabel(field, kCommentMaxBytes, {
		.customCharactersCount = [=] {
			return CommentBytes(field->getLastText());
		},
	});
	field->setMaxLength(-1);
}

struct SendFlow {
	QString destination;
	bool bounce = true;
	QString displayForm;
	int64 amountNano = 0;
	int64 feeNano = 0;
	QString comment;
};

[[nodiscard]] std::optional<SendFlow> ParseRecipientFlow(
		const QString &text) {
	auto address = text;
	auto amountNano = int64(0);
	auto comment = QString();
	if (const auto link = ParseTransferLink(text)) {
		address = link->address;
		amountNano = link->amountNano;
		comment = link->comment.trimmed();
	}
	const auto parsed = ParseAddress(address);
	if (!parsed || parsed->testnet) {
		return std::nullopt;
	}
	const auto friendly = FormatFriendly(parsed->raw, parsed->bounceable);
	if (friendly.isEmpty()) {
		return std::nullopt;
	}
	return SendFlow{
		.destination = parsed->raw,
		.bounce = parsed->bounceable,
		.displayForm = (parsed->friendly ? address : friendly),
		.amountNano = amountNano,
		.comment = (CommentFits(comment) ? comment : QString()),
	};
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
	ApplyCommentLimit(field);
	return field;
}

[[nodiscard]] not_null<Ui::InputField*> AddSendField(
		not_null<Ui::VerticalLayout*> container,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st,
			Ui::InputField::Mode::NoNewlines,
			std::move(placeholder),
			value),
		st::walletSendFieldMargin);
	const auto paste = Ui::CreateChild<Ui::RoundButton>(
		field,
		tr::lng_wallet_send_paste(),
		st::defaultTableSmallButton);
	paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	paste->setClickedCallback([=] {
		field->setFocusFast();
		field->setText(QGuiApplication::clipboard()->text().trimmed());
	});
	field->widthValue(
	) | rpl::on_next([=, &st](int) {
		paste->moveToRight(0, st.textMargins.top());
	}, paste->lifetime());
	const auto updatePaste = [=] {
		paste->setVisible(field->getLastText().isEmpty());
	};
	field->changes() | rpl::on_next(updatePaste, field->lifetime());
	updatePaste();
	return field;
}

[[nodiscard]] QString SendErrorText(SendError error) {
	switch (error) {
	case SendError::None:
		return QString();
	case SendError::InsufficientBalance:
		return tr::lng_wallet_send_error_insufficient(tr::now);
	case SendError::InsufficientFees:
		return tr::lng_wallet_send_error_fees(tr::now);
	case SendError::PreviousUnresolved:
		return tr::lng_wallet_send_error_unresolved(tr::now);
	case SendError::AlreadySending:
		return tr::lng_wallet_send_error_in_progress(tr::now);
	case SendError::SigningUnavailable:
		return tr::lng_wallet_send_error_signing(tr::now);
	case SendError::InvalidRequest:
	case SendError::Failed:
		return tr::lng_wallet_send_error_failed(tr::now);
	}
	Unexpected("Error value in SendErrorText.");
}

void WalletSendConfirmBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow,
		Fn<void(QString)> commentEdited) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	auto item = TransferItem();
	item.incoming = false;
	item.amountNano = flow.amountNano;
	item.status = TransferItem::Status::Success;
	AddDetailsAmountHeader(
		box,
		item,
		st::boxTitleHeight + st::walletDetailsAmountTopSkip);

	const auto table = box->addRow(
		object_ptr<Ui::TableLayout>(
			box,
			st::walletDetailsTable),
		st::giveawayGiftCodeTableMargin);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_send_address_label(),
		AddressValueLabel(table, box->uiShow(), flow.displayForm));
	AddFeeTableRow(
		table,
		&show->session(),
		flow.feeNano);
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
	const auto wallet = &show->session().wallet();
	const auto weak = base::make_weak(box.get());
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
		wallet->send(args, [=](SendError error) {
			if (!show->valid()) {
				return;
			}
			if (error != SendError::None) {
				if (weak.get()) {
					state->confirmButtonBusy = false;
				}
				show->showToast(SendErrorText(error));
				return;
			}
			show->hideLayer();
			show->showToast(tr::lng_wallet_sent_toast(
				tr::now,
				lt_address,
				ShortAddressForm(flow.displayForm)));
			if (const auto &pending = wallet->pendingSend()) {
				ShowWalletTransactionBox(show, ItemFromPending(*pending));
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

[[nodiscard]] not_null<Ui::InputField*> AddAmountField(
		not_null<Ui::VerticalLayout*> container,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		int64 value,
		Fn<int()> fractionDigits,
		Fn<QString()> separator,
		rpl::producer<bool> entryFiat,
		rpl::producer<QString> currency,
		rpl::producer<QString> fiat,
		Fn<void()> swap) {
	const auto wrap = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st.heightMin),
		st::walletSendFieldMargin);
	const auto field = Ui::CreateTonAmountInput(
		wrap,
		std::move(placeholder),
		value,
		std::move(fractionDigits),
		&st,
		std::move(separator));
	auto helper = Ui::Text::CustomEmojiHelper();
	auto diamond = helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyColored(
				st::walletSendMarkSize,
				st::windowActiveTextFg->c);
		},
	});
	const auto icon = Ui::CreateChild<Ui::FlatLabel>(
		field.get(),
		rpl::single(std::move(diamond)),
		st::defaultFlatLabel,
		st::defaultPopupMenu,
		helper.context());
	const auto fiatIcon = Ui::CreateChild<Ui::FlatLabel>(
		field.get(),
		std::move(currency) | rpl::map([](const QString &code) {
			return Ui::CurrencyName(code);
		}),
		st::walletSendFiatLabel);
	const auto fiatLabel = Ui::CreateChild<Ui::FlatLabel>(
		field.get(),
		std::move(fiat),
		st::walletSendFiatLabel);
	fiatLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto swapButton = Ui::CreateChild<Ui::AbstractButton>(field.get());
	swapButton->setPointerCursor(true);
	swapButton->setClickedCallback(std::move(swap));
	fiatLabel->geometryValue(
	) | rpl::on_next([=](const QRect &geometry) {
		swapButton->setGeometry(geometry);
		swapButton->raise();
	}, swapButton->lifetime());
	rpl::combine(
		std::move(entryFiat),
		fiatIcon->naturalWidthValue(),
		fiatLabel->naturalWidthValue()
	) | rpl::on_next([=](bool fiat, int prefixWidth, int fiatWidth) {
		icon->setVisible(!fiat);
		fiatIcon->setVisible(fiat);
		const auto overflow = st::walletSendMarkPosition.x()
			+ prefixWidth
			+ st::walletSendFiatLabelSkip
			- field->st().textMargins.left();
		field->setAdditionalMargins({
			(fiat && overflow > 0) ? overflow : 0,
			0,
			fiatWidth + st::walletSendFiatLabelSkip,
			0,
		});
	}, field->lifetime());
	rpl::combine(
		field->widthValue(),
		fiatLabel->naturalWidthValue()
	) | rpl::on_next([=](int width, int naturalWidth) {
		fiatLabel->resizeToWidth(naturalWidth);
		fiatLabel->moveToRight(0, st::walletSendFiatTop, width);
	}, fiatLabel->lifetime());
	wrap->widthValue() | rpl::on_next([=](int width) {
		icon->move(st::walletSendMarkPosition);
		fiatIcon->move(st::walletSendMarkPosition.x(), st::walletSendFiatTop);
		field->move(0, 0);
		field->resize(width, field->height());
		wrap->resize(width, field->height());
	}, wrap->lifetime());
	return field;
}

void WalletSendBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<SendFlow> initial) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setTitle(tr::lng_wallet_send_title());
	AddBoxCloseButton(box);

	const auto wallet = &show->session().wallet();

	struct State {
		std::optional<SendFlow> flow;
		rpl::variable<bool> expanded = false;
		rpl::variable<bool> invalid = false;
		rpl::variable<int64> amount = 0;
		rpl::variable<int64> fee = 0;
		rpl::variable<bool> previewInsufficient = false;
		rpl::variable<SendError> previewError = SendError::None;
		rpl::variable<bool> insufficient = false;
		rpl::variable<bool> canSend = false;
		rpl::variable<FiatRate> rate;
		rpl::variable<bool> entryFiat = false;
		QString previousCurrency;
		bool settingUnitText = false;
		Fn<void()> swapUnit;
	};
	const auto state = box->lifetime().make_state<State>();
	state->rate = FiatRateValue(&show->session());

	const auto entrySeparator = [=] {
		if (!state->entryFiat.current()) {
			return Ui::TonAmountSeparator();
		}
		const auto rule = Ui::LookupCurrencyRule(
			state->rate.current().currency);
		return QString(QChar(rule.decimal));
	};

	const auto recipient = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto recipientField = AddSendField(
		recipient,
		st::walletSendField,
		tr::lng_wallet_send_recipient(),
		initial ? initial->displayForm : QString());
	const auto errorWrap = recipient->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			recipient,
			object_ptr<Ui::FlatLabel>(
				recipient,
				tr::lng_wallet_send_invalid_address(),
				st::walletSendErrorLabel)),
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			0));
	errorWrap->toggleOn(state->invalid.value());
	errorWrap->finishAnimating();

	const auto wrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();
	Ui::AddDivider(inner);

	auto fiatText = rpl::combine(
		state->amount.value(),
		state->entryFiat.value(),
		state->rate.value()
	) | rpl::map([](int64 amount, bool fiat, const FiatRate &rate) {
		return fiat
			? tr::lng_wallet_send_pill_gram(
				tr::now,
				lt_amount,
				Ui::FormatTonAmount(amount).full)
			: FormatFiat(amount, rate, kFiatCurrencyDecimals, true);
	});
	const auto amountField = AddAmountField(
		inner,
		st::walletSendAmountField,
		tr::lng_wallet_send_amount_label(),
		std::min(initial ? initial->amountNano : 0, kMaxAmountNano),
		[=] {
			return state->entryFiat.current()
				? Ui::LookupCurrencyRule(
					state->rate.current().currency).exponent
				: 9;
		},
		entrySeparator,
		state->entryFiat.value(),
		state->rate.value() | rpl::map([](const FiatRate &rate) {
			return rate.currency;
		}) | rpl::distinct_until_changed(),
		std::move(fiatText),
		[=] { state->swapUnit(); });

	const auto updateAmount = [=] {
		if (state->settingUnitText) {
			return;
		}
		const auto parsed = Ui::ParseTonAmountString(
			amountField->getLastText(),
			entrySeparator()).value_or(0);
		const auto rate = state->rate.current();
		state->amount = !state->entryFiat.current()
			? parsed
			: rate.available()
			? std::min(
				int64(std::clamp(
					base::SafeRound(double(parsed) / rate.perGram),
					0.,
					double(kMaxAmountNano))),
				kMaxAmountNano)
			: 0;
	};
	amountField->changes(
	) | rpl::on_next(updateAmount, amountField->lifetime());
	updateAmount();

	const auto renderUnitText = [=] {
		const auto amount = state->amount.current();
		if (!state->entryFiat.current()) {
			return amount
				? Ui::FormatTonAmount(
					amount,
					Ui::TonFormatFlag::Simple).full
				: QString();
		}
		const auto rate = state->rate.current();
		const auto quantum = FiatMinorUnitNanos(rate.currency);
		const auto maxUnits = kMaxFiatUnits * (Ui::kNanosInOne / quantum);
		const auto units = int64(std::min(
			base::SafeRound(amount * rate.perGram / double(quantum)),
			double(maxUnits)));
		if (!units) {
			return QString();
		}
		const auto formatted = Ui::FormatTonAmount(
			units * quantum,
			Ui::TonFormatFlag::Simple);
		auto result = formatted.wholeString;
		if (!formatted.nanoString.isEmpty()) {
			result += entrySeparator() + formatted.nanoString;
		}
		return result;
	};
	const auto setUnitText = [=] {
		state->settingUnitText = true;
		Ui::PostponeCall(amountField, [=] {
			state->settingUnitText = false;
		});
		amountField->setText(renderUnitText());
		amountField->setFocusFast();
	};
	const auto switchEntryUnit = [=](bool fiat) {
		if (state->entryFiat.current() == fiat
			|| (fiat && !state->rate.current().available())) {
			return;
		}
		state->entryFiat = fiat;
		setUnitText();
	};
	state->swapUnit = [=] { switchEntryUnit(!state->entryFiat.current()); };
	state->previousCurrency = state->rate.current().currency;
	state->rate.value() | rpl::on_next([=](const FiatRate &now) {
		const auto currencyChanged
			= (now.currency != state->previousCurrency);
		state->previousCurrency = now.currency;
		if (!now.available()) {
			switchEntryUnit(false);
		} else if (state->entryFiat.current()) {
			if (currencyChanged) {
				setUnitText();
			} else {
				updateAmount();
			}
		}
	}, box->lifetime());

	const auto refreshFee = [=] {
		if (!state->flow) {
			return;
		} else if (state->amount.current() <= 0) {
			state->previewInsufficient = false;
			state->previewError = SendError::None;
			state->fee = 0;
			return;
		}
		auto args = SendArgs{
			.destination = state->flow->destination,
			.amountNano = state->amount.current(),
			.bounce = state->flow->bounce,
		};
		wallet->estimateFee(args, crl::guard(box, [=](FeeResult result) {
			switch (result.error) {
			case SendError::None:
				state->fee = result.feeNano;
				state->previewInsufficient = false;
				state->previewError = SendError::None;
				return;
			case SendError::InsufficientBalance:
			case SendError::InsufficientFees:
				state->previewInsufficient = true;
				state->previewError = SendError::None;
				return;
			case SendError::InvalidRequest:
			case SendError::PreviousUnresolved:
			case SendError::AlreadySending:
			case SendError::SigningUnavailable:
			case SendError::Failed:
				state->fee = 0;
				state->previewInsufficient = false;
				state->previewError = result.error;
				return;
			}
			Unexpected("Error value in the send box fee estimate.");
		}));
	};
	state->amount.value() | rpl::on_next([=] {
		refreshFee();
	}, box->lifetime());
	state->insufficient = rpl::combine(
		state->amount.value(),
		state->fee.value(),
		state->previewInsufficient.value(),
		wallet->balanceNanoValue(),
		wallet->stateKnownValue()
	) | rpl::map([](
			int64 amount,
			int64 fee,
			bool preview,
			int64 balance,
			bool known) {
		return (amount > 0)
			&& (preview || (known && amount > balance - fee));
	});
	state->canSend = rpl::combine(
		state->amount.value(),
		state->insufficient.value(),
		state->expanded.value(),
		wallet->stateKnownValue(),
		state->previewError.value()
	) | rpl::map([](
			int64 amount,
			bool insufficient,
			bool valid,
			bool known,
			SendError error) {
		return valid
			&& known
			&& (amount > 0)
			&& !insufficient
			&& (error == SendError::None);
	});

	auto balanceLayout = object_ptr<Ui::VerticalLayout>(inner);
	const auto balance = balanceLayout.data();
	inner->add(
		object_ptr<Ui::DividerLabel>(
			inner,
			std::move(balanceLayout),
			st::walletSendBalancePadding,
			st::defaultDividerBar,
			RectPart::Top | RectPart::Bottom),
		style::margins(),
		style::al_justify);
	balance->add(
		object_ptr<Ui::FlatLabel>(
			balance,
			tr::lng_wallet_send_balance(
				lt_amount,
				wallet->balanceNanoValue() | rpl::map([](int64 nano) {
					return Ui::FormatTonAmount(nano).full;
				})),
			st::walletSendBalanceLabel));
	const auto insufficientWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				tr::lng_wallet_send_insufficient(),
				st::walletSendErrorLabel)));
	insufficientWrap->toggleOn(state->insufficient.value());
	insufficientWrap->finishAnimating();
	const auto refusalWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				state->previewError.value() | rpl::filter([](
						SendError error) {
					return (error != SendError::None);
				}) | rpl::map([](SendError error) {
					return SendErrorText(error);
				}),
				st::walletSendErrorLabel)));
	refusalWrap->toggleOn(state->previewError.value() | rpl::map([](
			SendError error) {
		return (error != SendError::None);
	}));
	refusalWrap->finishAnimating();
	const auto depositWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			balance,
			object_ptr<Ui::VerticalLayout>(balance)));
	const auto depositInner = depositWrap->entity();
	const auto deposit = depositInner->add(
		object_ptr<Ui::RoundButton>(
			depositInner,
			tr::lng_wallet_send_deposit(),
			st::defaultTableSmallButton));
	deposit->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	deposit->setClickedCallback([=] {
		ShowWalletReceiveBox(&show->session(), box->uiShow());
	});
	depositWrap->toggleOn(state->insufficient.value());
	depositWrap->finishAnimating();

	const auto commentField = AddSendField(
		inner,
		st::walletSendCommentField,
		tr::lng_wallet_send_comment_placeholder(),
		initial ? initial->comment : QString());
	ApplyCommentLimit(commentField);

	const auto setComment = crl::guard(box, [=](QString comment) {
		commentField->setText(comment);
	});
	const auto submit = [=] {
		if (!state->flow || !state->canSend.current()) {
			amountField->showError();
			return;
		}
		auto next = *state->flow;
		next.amountNano = state->amount.current();
		next.feeNano = state->fee.current();
		next.comment = commentField->getLastText().trimmed();
		box->uiShow()->showBox(Box(
			WalletSendConfirmBox,
			show,
			next,
			setComment));
	};
	const auto button = box->addButton(
		tr::lng_wallet_send_continue(),
		submit).data();
	state->expanded.value() | rpl::on_next([=](bool expanded) {
		button->setVisible(expanded);
	}, button->lifetime());
	state->canSend.value() | rpl::on_next([=](bool canSend) {
		SetButtonDisabledLook(button, !canSend);
	}, button->lifetime());
	amountField->submits() | rpl::on_next(submit, amountField->lifetime());
	commentField->submits() | rpl::on_next(submit, commentField->lifetime());

	state->flow = initial;
	state->expanded = initial.has_value();
	refreshFee();
	recipientField->changes() | rpl::on_next([=] {
		const auto text = recipientField->getLastText().trimmed();
		const auto previous = state->flow;
		const auto was = state->expanded.current();
		state->flow = ParseRecipientFlow(text);
		const auto valid = state->flow.has_value();
		state->invalid = !text.isEmpty() && !valid;
		state->expanded = valid;
		if (valid) {
			refreshFee();
			const auto changed = !previous
				|| (previous->amountNano != state->flow->amountNano)
				|| (previous->comment != state->flow->comment);
			if (changed) {
				const auto amountNano = std::min(
					state->flow->amountNano,
					kMaxAmountNano);
				if (amountNano > 0) {
					state->entryFiat = false;
					amountField->setText(
						Ui::FormatTonAmount(
							amountNano,
							Ui::TonFormatFlag::Simple).full);
				}
				if (!state->flow->comment.isEmpty()) {
					commentField->setText(state->flow->comment);
				}
			}
			if (!was) {
				amountField->setFocusFast();
			}
		}
	}, recipientField->lifetime());
	recipientField->submits() | rpl::on_next([=] {
		if (state->flow) {
			amountField->setFocusFast();
		} else {
			recipientField->showError();
		}
	}, recipientField->lifetime());
	box->setFocusCallback([=] {
		(initial ? amountField : recipientField)->setFocusFast();
	});
	wrap->toggleOn(state->expanded.value());
	wrap->finishAnimating();
}

void AddPhraseBoxHeader(
		not_null<Ui::GenericBox*> box,
		const QString &lottieName,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		int lottieSize,
		const style::margins &lottieMargin,
		const style::margins &textMargin) {
	auto icon = Settings::CreateLottieIcon(
		box->verticalLayout(),
		{
			.name = lottieName,
			.sizeOverride = { lottieSize, lottieSize },
		},
		lottieMargin);
	box->verticalLayout()->add(std::move(icon.widget));
	box->showFinishes() | rpl::on_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, box->lifetime());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::walletPhraseTitleLabel),
		st::boxRowPadding,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::walletPhraseTextLabel),
		textMargin,
		style::al_top);
}

void AddPhraseGrid(
		not_null<Ui::GenericBox*> box,
		const std::vector<QString> &words) {
	const auto count = int(words.size());
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
			words[i],
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
}

void WalletPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto count = int(words.size());
	AddPhraseBoxHeader(
		box,
		u"wallet/paper"_q,
		tr::lng_wallet_phrase_title(),
		tr::lng_wallet_phrase_text(
			lt_count,
			rpl::single(count * 1.) | tr::to_count(),
			tr::marked),
		st::walletPhraseGridLottieSize,
		st::walletPhraseGridLottieMargin,
		st::walletPhraseGridTextMargin);

	AddPhraseGrid(box, words);

	AddBoxCloseButton(box);
	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
}

[[nodiscard]] TextWithEntities EnforcementCheckAbout(
		const QString &error,
		tr::phrase<lngtag_duration> wait,
		tr::phrase<> about) {
	auto result = tr::marked();
	const auto prefixes = {
		u"PASSWORD_TOO_FRESH_"_q,
		u"SESSION_TOO_FRESH_"_q,
	};
	for (const auto &prefix : prefixes) {
		if (!error.startsWith(prefix)) {
			continue;
		}
		const auto seconds = error.mid(prefix.size()).toInt();
		if (seconds > 0) {
			result.append(wait(
				tr::now,
				lt_duration,
				tr::marked(Ui::FormatResetCloudPasswordIn(seconds)),
				tr::marked)
			).append(QChar('\n')).append(QChar('\n'));
		}
		break;
	}
	result.append(about(tr::now, tr::marked));
	return result;
}

[[nodiscard]] TextWithEntities PhraseCheckAbout(const QString &error) {
	return EnforcementCheckAbout(
		error,
		tr::lng_wallet_phrase_check_wait,
		tr::lng_wallet_phrase_check_about);
}

[[nodiscard]] TextWithEntities ReplaceCheckAbout(const QString &error) {
	return EnforcementCheckAbout(
		error,
		tr::lng_wallet_replace_check_wait,
		tr::lng_wallet_replace_check_about);
}

[[nodiscard]] TextWithEntities BackupCheckAbout(const QString &error) {
	return EnforcementCheckAbout(
		error,
		tr::lng_wallet_backup_check_wait,
		tr::lng_wallet_backup_check_about);
}

void RequestPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void(std::vector<QString>)> onWords = nullptr) {
	const auto done = crl::guard(warning, [=](std::vector<QString> words) {
		if (passcode) {
			passcode->closeBox();
		}
		if (onWords) {
			onWords(std::move(words));
		} else {
			warning->closeBox();
			show->showBox(Box(WalletPhraseBox, show, std::move(words)));
		}
	});
	const auto fail = crl::guard(warning, [=](const QString &error) {
		unblock();
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				PhraseCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		show->showToast(tr::lng_wallet_phrase_error(tr::now));
	});
	auto &wallet = show->session().wallet();
	if (parkedKey) {
		wallet.revealParked(*parkedKey, done, fail);
	} else {
		wallet.revealPhrase(std::move(password), done, fail);
	}
}

void StartPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void(std::vector<QString>)> onWords = nullptr) {
	const auto session = &show->session();
	if (parkedKey) {
		RequestPhraseReveal(
			show,
			warning,
			std::nullopt,
			nullptr,
			unblock,
			parkedKey,
			onWords);
		return;
	}
	if (session->wallet().revealsLocally()) {
		RequestPhraseReveal(
			show,
			warning,
			std::nullopt,
			nullptr,
			unblock,
			std::nullopt,
			onWords);
		return;
	}
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestPhraseReveal(
				show,
				warning,
				std::nullopt,
				nullptr,
				unblock,
				std::nullopt,
				onWords);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_wallet_phrase_password_title();
		fields.customDescription = tr::lng_wallet_phrase_password_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestPhraseReveal(
				show,
				warning,
				result,
				passcode,
				unblock,
				std::nullopt,
				onWords);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, warning->lifetime());
}

void WalletPhraseWarningBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey = std::nullopt) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		u"wallet/paper"_q,
		tr::lng_wallet_phrase_intro_title(),
		tr::lng_wallet_phrase_intro_text(tr::marked),
		st::walletCoverLottieSize,
		st::walletCoverLottieMargin,
		st::walletPhraseTextMargin);

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
	const auto revealing = box->lifetime().make_state<bool>(false);
	box->addButton(tr::lng_wallet_keys_show_phrase(), [=] {
		if (*revealing) {
			return;
		}
		*revealing = true;
		StartPhraseReveal(show, box, [=] { *revealing = false; }, parkedKey);
	});
}

void WalletPasscodeBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void()> passed = nullptr) {
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
		const auto &domain = show->session().domain();
		if (domain.local().checkPasscode(field->text().toUtf8())) {
			cSetPasscodeBadTries(0);
			box->closeBox();
			if (passed) {
				passed();
			} else {
				show->showBox(Box(WalletPhraseWarningBox, show, parkedKey));
			}
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

void WalletRevealFlow(
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey = std::nullopt) {
	const auto &domain = show->session().domain();
	if (domain.local().hasLocalPasscode()) {
		show->showBox(Box(WalletPasscodeBox, show, parkedKey, nullptr));
	} else {
		show->showBox(Box(WalletPhraseWarningBox, show, parkedKey));
	}
}

void RunLocalPasscodeGate(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> then) {
	const auto &domain = show->session().domain();
	if (domain.local().hasLocalPasscode()) {
		show->showBox(
			Box(WalletPasscodeBox, show, std::nullopt, std::move(then)));
	} else {
		then();
	}
}

enum class WalletImportMode {
	Replace,
	Restore,
};

void WalletImportBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	WalletImportMode mode,
	Fn<void()> restored = nullptr);

void RequestCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> action,
		Fn<void()> unblock) {
	const auto done = [=] {
		if (passcode) {
			passcode->closeBox();
		}
		action();
	};
	const auto fail = [=](const QString &error) {
		if (unblock) {
			unblock();
		}
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				PhraseCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		show->showToast(tr::lng_wallet_phrase_error(tr::now));
	};
	show->session().wallet().restoreFromBackup(
		std::move(password),
		done,
		fail);
}

void StartCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action,
		Fn<void()> unblock = nullptr) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	const auto lifetime = std::make_shared<rpl::lifetime>();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		const auto owned = base::take(*lifetime);
		if (!state.hasPassword) {
			RequestCustodyRestore(
				show,
				std::nullopt,
				nullptr,
				action,
				unblock);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_wallet_phrase_password_title();
		fields.customDescription = tr::lng_wallet_restore_password_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestCustodyRestore(show, result, passcode, action, unblock);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		if (unblock) {
			unblock();
		}
	}, *lifetime);
}

enum class KeyActionKind {
	Plain,
	Reveal,
	ResumeAfterRestore,
};

void RunKeyRequiringAction(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action,
		KeyActionKind kind = KeyActionKind::Plain) {
	const auto state = show->session().wallet().deviceCustodyState();
	if (state.mode == DeviceMode::Full) {
		action();
	} else if (state.conflict) {
		show->showToast(tr::lng_wallet_conflict_toast(tr::now));
	} else if (state.mode == DeviceMode::ReadOnlyRestorable) {
		if (kind == KeyActionKind::Reveal) {
			action();
		} else {
			StartCustodyRestore(show, std::move(action));
		}
	} else if (state.mode == DeviceMode::ReadOnlyNotRestorable) {
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Restore,
			(kind == KeyActionKind::ResumeAfterRestore) ? action : nullptr));
	}
}

void RequestWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError) {
	const auto imported = words.has_value();
	const auto done = crl::guard(origin, [=] {
		if (passcode) {
			passcode->closeBox();
		}
		show->hideLayer();
		show->showToast({
			.title = (imported
				? tr::lng_wallet_imported_title(tr::now)
				: tr::lng_wallet_created_title(tr::now)),
			.text = { imported
				? tr::lng_wallet_imported_text(tr::now)
				: tr::lng_wallet_created_text(tr::now) },
			.icon = &st::toastCheckIcon,
		});
	});
	const auto fail = crl::guard(origin, [=](const QString &error) {
		unblock();
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				ReplaceCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		if (!imported) {
			show->showToast(tr::lng_wallet_create_error(tr::now));
		} else if (showError) {
			showError((error == u"REPLACE_INVALID_PHRASE"_q)
				? tr::lng_wallet_import_error(tr::now)
				: tr::lng_wallet_import_failed(tr::now));
		} else {
			show->showToast(tr::lng_wallet_import_failed(tr::now));
		}
	});
	auto &wallet = show->session().wallet();
	if (imported) {
		wallet.replaceWithImported(
			std::move(*words),
			std::move(password),
			done,
			fail);
	} else {
		wallet.replaceWithNew(std::move(password), done, fail);
	}
}

void StartWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError = nullptr) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestWalletReplace(
				show,
				origin,
				words,
				std::nullopt,
				nullptr,
				unblock,
				showError);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_wallet_phrase_password_title();
		fields.customDescription = tr::lng_wallet_replace_password_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestWalletReplace(
				show,
				origin,
				words,
				result,
				passcode,
				unblock,
				showError);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, origin->lifetime());
}

enum class BackupChange {
	Enable,
	Disable,
};

void RequestBackupChange(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		BackupChange change,
		std::vector<QByteArray> parts,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void()> done) {
	const auto succeeded = crl::guard(origin, [=] {
		unblock();
		if (passcode) {
			passcode->closeBox();
		}
		done();
	});
	const auto fail = crl::guard(origin, [=](const QString &error) {
		unblock();
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				BackupCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		show->showToast((error == u"WALLET_BACKUP_NOT_AVAILABLE"_q)
			? tr::lng_wallet_backup_unavailable_error(tr::now)
			: tr::lng_wallet_backup_error(tr::now));
	});
	auto &wallet = show->session().wallet();
	if (change == BackupChange::Disable) {
		wallet.disableBackup(std::move(password), succeeded, fail);
	} else {
		wallet.enableBackup(
			std::move(parts),
			std::move(password),
			succeeded,
			fail);
	}
}

void StartBackupRequest(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		BackupChange change,
		std::vector<QByteArray> parts,
		Fn<void()> unblock,
		Fn<void()> done) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestBackupChange(
				show,
				origin,
				change,
				parts,
				std::nullopt,
				nullptr,
				unblock,
				done);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_wallet_phrase_password_title();
		fields.customDescription = tr::lng_wallet_backup_password_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestBackupChange(
				show,
				origin,
				change,
				parts,
				result,
				passcode,
				unblock,
				done);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, origin->lifetime());
}

void ShowBackupEnabledToast(std::shared_ptr<Main::SessionShow> show) {
	show->showToast({
		.title = tr::lng_wallet_backup_enabled_title(tr::now),
		.text = { tr::lng_wallet_backup_enabled_text(tr::now) },
		.icon = &st::toastCheckIcon,
	});
}

void StartBackupEnable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy) {
	const auto mode = show->session().wallet().deviceCustodyState().mode;
	const auto hadCustody = (mode == DeviceMode::Full);
	const auto upload = crl::guard(origin, [=] {
		if (*busy) {
			return;
		}
		*busy = true;
		show->session().wallet().prepareBackupParts(
			crl::guard(origin, [=](std::vector<QByteArray> parts) {
				StartBackupRequest(
					show,
					origin,
					BackupChange::Enable,
					std::move(parts),
					[=] { *busy = false; },
					[=] { ShowBackupEnabledToast(show); });
			}),
			crl::guard(origin, [=](const QString &) {
				*busy = false;
				show->showToast(tr::lng_wallet_backup_error(tr::now));
			}));
	});
	RunKeyRequiringAction(show, [=] {
		if (hadCustody) {
			RunLocalPasscodeGate(show, upload);
		} else {
			upload();
		}
	}, KeyActionKind::ResumeAfterRestore);
}

void WalletBackupPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void(std::vector<QString>)> next,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		u"wallet/paper"_q,
		std::move(title),
		std::move(text),
		st::walletPhraseGridLottieSize,
		st::walletPhraseGridLottieMargin,
		st::walletPhraseGridTextMargin);

	AddPhraseGrid(box, words);

	AddBoxCloseButton(box);
	const auto shownAt = crl::now();
	box->addButton(tr::lng_continue(), [=] {
		if (crl::now() - shownAt < kBackupWriteDownDelay) {
			show->showBox(Ui::MakeInformBox({
				.text = tr::lng_wallet_backup_sure_text(tr::now),
				.confirmText = tr::lng_wallet_backup_sure_ok(),
				.title = tr::lng_wallet_backup_sure_title(),
			}));
			return;
		}
		next(words);
		box->closeBox();
	});
}

[[nodiscard]] std::vector<int> BackupQuizIndices(int count) {
	auto result = std::vector<int>();
	while (int(result.size()) < kBackupQuizWordCount) {
		const auto index = base::RandomIndex(count);
		if (!ranges::contains(result, index)) {
			result.push_back(index);
		}
	}
	ranges::sort(result);
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> BackupQuizText(
		const std::vector<int> &indices) {
	const auto number = [](int index) {
		return rpl::single(tr::bold(QString::number(index + 1)));
	};
	return tr::lng_wallet_backup_test_text(
		lt_index1,
		number(indices[0]),
		lt_index2,
		number(indices[1]),
		lt_index3,
		number(indices[2]),
		tr::marked);
}

[[nodiscard]] not_null<Ui::InputField*> AddBackupQuizField(
		not_null<Ui::VerticalLayout*> container,
		int index) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::walletBackupQuizField,
			Ui::InputField::Mode::SingleLine),
		st::walletImportFieldMargin);
	const auto number = Ui::CreateChild<Ui::FlatLabel>(
		field,
		QString::number(index + 1) + QChar('.'),
		st::walletPhraseNumberLabel);
	number->setAttribute(Qt::WA_TransparentForMouseEvents);
	field->widthValue(
	) | rpl::on_next([=](int width) {
		number->moveToLeft(
			st::walletImportNumberLeft,
			st::walletImportNumberTop,
			width);
	}, field->lifetime());
	return field;
}

[[nodiscard]] bool BackupQuizAnswerMatches(
		not_null<Ui::InputField*> field,
		const QString &word) {
	const auto entered = field->getLastText().trimmed();
	return (entered.compare(word.trimmed(), Qt::CaseInsensitive) == 0);
}

void WalletBackupQuizBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void()> passed,
		Fn<void(Fn<void()> lock)> publishLock) {
	Expects(int(words.size()) >= kBackupQuizWordCount);

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		std::vector<Ui::InputField*> fields;
		std::vector<int> indices;
		std::vector<bool> wrong;
	};
	const auto state = box->lifetime().make_state<State>();
	state->indices = BackupQuizIndices(int(words.size()));
	state->wrong.resize(kBackupQuizWordCount, false);

	AddPhraseBoxHeader(
		box,
		u"wallet/test"_q,
		tr::lng_wallet_backup_test_title(),
		BackupQuizText(state->indices),
		st::walletPhraseGridLottieSize,
		st::walletPhraseGridLottieMargin,
		st::walletPhraseGridTextMargin);

	const auto container = box->verticalLayout();
	Ui::AddSkip(container, st::walletBackupQuizFieldsTopSkip);
	for (const auto index : state->indices) {
		state->fields.push_back(AddBackupQuizField(container, index));
	}
	Ui::AddSkip(container, st::walletBackupQuizFieldsBottomSkip);

	AddBoxCloseButton(box);
	const auto button = box->addButton(tr::lng_continue());
	const auto allFilled = [=] {
		return ranges::all_of(state->fields, [](Ui::InputField *field) {
			return !field->getLastText().trimmed().isEmpty();
		});
	};
	const auto anyWrong = [=] {
		return ranges::contains(state->wrong, true);
	};
	const auto refreshButton = [=] {
		if (const auto raw = button.data()) {
			SetButtonDisabledLook(raw, !allFilled() || anyWrong());
		}
	};
	const auto submit = [=] {
		if (!allFilled() || anyWrong()) {
			return;
		}
		for (auto i = 0; i != kBackupQuizWordCount; ++i) {
			const auto field = state->fields[i];
			if (!BackupQuizAnswerMatches(field, words[state->indices[i]])) {
				field->showError();
				state->wrong[i] = true;
			}
		}
		if (anyWrong()) {
			refreshButton();
		} else {
			passed();
		}
	};
	button->setClickedCallback(submit);
	refreshButton();

	for (auto i = 0; i != kBackupQuizWordCount; ++i) {
		const auto field = state->fields[i];
		field->changes() | rpl::on_next([=] {
			state->wrong[i] = false;
			refreshButton();
		}, field->lifetime());
		field->submits() | rpl::on_next([=] {
			if (i + 1 < kBackupQuizWordCount) {
				state->fields[i + 1]->setFocus();
			} else {
				submit();
			}
		}, field->lifetime());
	}
	box->setFocusCallback([=] {
		state->fields.front()->setFocusFast();
	});
	if (publishLock) {
		publishLock([=] {
			for (const auto field : state->fields) {
				field->setDisabled(true);
			}
			// A disabled field takes no focus, and with the focus left on
			// the layer stack Escape closes the box past
			// setCloseByEscape(false), so the box holds it itself.
			box->setFocusCallback(nullptr);
			box->setInnerFocus();
		});
	}
}

void ShowBackupDisabledToast(std::shared_ptr<Main::SessionShow> show) {
	show->showToast({
		.title = tr::lng_wallet_backup_disabled_title(tr::now),
		.text = { tr::lng_wallet_backup_disabled_text(tr::now) },
		.icon = &st::toastCheckIcon,
	});
}

void ShowBackupDisableConfirm(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> quiz,
		std::shared_ptr<bool> requesting) {
	const auto request = [=] {
		const auto strong = quiz.get();
		if (!strong || *requesting) {
			return;
		}
		*requesting = true;
		StartBackupRequest(
			show,
			strong,
			BackupChange::Disable,
			{},
			[=] { *requesting = false; },
			[=] {
				if (quiz) {
					quiz->closeBox();
				}
				ShowBackupDisabledToast(show);
			});
	};
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::lng_wallet_backup_final_text(tr::now),
		.confirmed = [=](Fn<void()> close) {
			close();
			request();
		},
		.confirmText = tr::lng_wallet_backup_disable_confirm(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_wallet_backup_disable_title(),
	}));
}

void CollectBackupPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin) {
	const auto showQuiz = [=](std::vector<QString> words) {
		const auto quiz = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
		const auto requesting = std::make_shared<bool>(false);
		*quiz = show->show(Box(WalletBackupQuizBox, show, words, [=] {
			ShowBackupDisableConfirm(show, *quiz, requesting);
		}, nullptr));
	};
	const auto showPhrase = [=](std::vector<QString> words) {
		show->showBox(Box(
			WalletBackupPhraseBox,
			show,
			words,
			showQuiz,
			tr::lng_wallet_backup_phrase_title(),
			tr::lng_wallet_backup_phrase_text(tr::marked)));
	};
	RunLocalPasscodeGate(show, crl::guard(origin, [=] {
		StartPhraseReveal(show, origin, [] {}, std::nullopt, showPhrase);
	}));
}

[[nodiscard]] QString RotationQuoteErrorText(SendError error) {
	return (error == SendError::Failed || error == SendError::InvalidRequest)
		? tr::lng_wallet_backup_update_quote_error(tr::now)
		: SendErrorText(error);
}

[[nodiscard]] QString RotationFeeText(
		tr::phrase<lngtag_amount, lngtag_fiat> phrase,
		int64 feeNano,
		const FiatRate &rate) {
	return phrase(
		tr::now,
		lt_amount,
		Ui::FormatTonAmount(feeNano).full,
		lt_fiat,
		FormatFiat(feeNano, rate, kFeeFiatDecimals, true));
}

[[nodiscard]] QString RotationFailureReason(const QString &error) {
	if (error == u"ROTATION_FEES"_q) {
		return tr::lng_wallet_backup_rotate_reason_fees(tr::now);
	} else if (error == u"ROTATION_EXPIRED"_q) {
		return tr::lng_wallet_backup_rotate_reason_expired(tr::now);
	} else if (error == u"ROTATION_ALREADY_SENDING"_q) {
		return tr::lng_wallet_backup_rotate_reason_busy(tr::now);
	} else if (error == u"ROTATION_REFUSED"_q
		|| error == u"ROTATION_REPLACED"_q
		|| error == u"ROTATION_FAILED"_q) {
		return tr::lng_wallet_backup_rotate_reason_unconfirmed(tr::now);
	}
	return tr::lng_wallet_backup_rotate_reason_failed(tr::now);
}

void ShowRotationFailedToast(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error) {
	show->showToast({
		.title = tr::lng_wallet_backup_rotate_failed_title(tr::now),
		.text = { tr::lng_wallet_backup_rotate_failed_text(
			tr::now,
			lt_reason,
			RotationFailureReason(error)) },
		.icon = &st::toastCheckIcon,
	});
}

void SetBoxBusy(not_null<Ui::GenericBox*> box) {
	box->clearButtons();
	const auto button = box->addButton(rpl::single(QString()));
	SetButtonDisabledLook(button.data(), true);
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		st::giveawayGiftCodeBoxButton.height / 2);
	Info::Statistics::AddChildToWidgetCenter(button.data(), loading);
	loading->show();
	box->setCloseByOutsideClick(false);
	box->setCloseByEscape(false);
}

struct RotationState {
	bool submitted = false;
	base::weak_qptr<Ui::GenericBox> quiz;
	Fn<void()> lockQuiz;
};

void SubmitRotation(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<RotationState> state) {
	const auto quiz = state->quiz.get();
	if (!quiz || state->submitted) {
		return;
	}
	state->submitted = true;
	state->lockQuiz();
	SetBoxBusy(quiz);
	const auto closeQuiz = [=] {
		if (const auto quiz = state->quiz.get()) {
			quiz->closeBox();
		}
	};
	show->session().wallet().submitRotation([=] {
		closeQuiz();
		const auto strong = origin.get();
		if (!strong) {
			return;
		}
		*busy = true;
		StartBackupRequest(
			show,
			strong,
			BackupChange::Disable,
			{},
			[=] { *busy = false; },
			[=] { ShowBackupDisabledToast(show); });
	}, [=](const QString &error) {
		closeQuiz();
		ShowRotationFailedToast(show, error);
	});
}

void ShowRotationConfirm(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<RotationState> state,
		int64 feeNano) {
	if (state->submitted) {
		return;
	}
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::lng_wallet_backup_rotate_final_text(
			tr::now,
			lt_amount,
			Ui::FormatTonAmount(feeNano).full),
		.confirmed = [=](Fn<void()> close) {
			close();
			SubmitRotation(show, origin, busy, state);
		},
		.cancelled = [=](Fn<void()> close) {
			close();
			if (const auto quiz = state->quiz.get()) {
				quiz->closeBox();
			}
		},
		.confirmText = tr::lng_wallet_backup_disable_confirm(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_wallet_backup_disable_title(),
	}));
}

void ShowRotationPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		int64 feeNano,
		std::vector<QString> words) {
	const auto state = std::make_shared<RotationState>();
	const auto wallet = &show->session().wallet();
	const auto weak = base::make_weak(origin);
	const auto showQuiz = [=](std::vector<QString> words) {
		const auto quiz = show->show(Box(WalletBackupQuizBox, show, words, [=] {
			ShowRotationConfirm(show, weak, busy, state, feeNano);
		}, [=](Fn<void()> lock) {
			state->lockQuiz = std::move(lock);
		}));
		state->quiz = quiz;
		quiz->boxClosing() | rpl::on_next([=] {
			if (!state->submitted) {
				wallet->abandonRotation();
			}
		}, quiz->lifetime());
	};
	const auto sheet = show->show(Box(
		WalletBackupPhraseBox,
		show,
		words,
		showQuiz,
		tr::lng_wallet_backup_new_phrase_title(),
		tr::lng_wallet_backup_new_phrase_text(tr::marked)));
	sheet->boxClosing() | rpl::on_next([=] {
		if (!state->quiz && !state->submitted) {
			wallet->abandonRotation();
		}
	}, sheet->lifetime());
}

void StartRotation(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		int64 feeNano) {
	RunLocalPasscodeGate(show, crl::guard(origin, [=] {
		if (*busy) {
			return;
		}
		*busy = true;
		const auto weak = base::make_weak(origin);
		show->session().wallet().prepareRotation(feeNano, [=](
				std::vector<QString> words) {
			const auto strong = weak.get();
			if (!strong) {
				show->session().wallet().abandonRotation();
				return;
			}
			*busy = false;
			ShowRotationPhrase(show, strong, busy, feeNano, std::move(words));
		}, crl::guard(origin, [=](const QString &error) {
			*busy = false;
			ShowRotationFailedToast(show, error);
		}));
	}));
}

void ShowBackupUpdateAlert(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		int64 feeNano,
		Fn<void()> notNow) {
	const auto weak = base::make_weak(origin);
	const auto rate = show->session().wallet().rates().current();
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::lng_wallet_backup_update_text(tr::now)
			+ u"\n\n"_q
			+ RotationFeeText(tr::lng_wallet_backup_update_fee, feeNano, rate),
		.confirmed = [=](Fn<void()> close) {
			close();
			if (const auto strong = weak.get()) {
				StartRotation(show, strong, busy, feeNano);
			}
		},
		.cancelled = [=](Fn<void()> close) {
			close();
			notNow();
		},
		.confirmText = tr::lng_wallet_backup_update_confirm(),
		.cancelText = tr::lng_wallet_backup_update_later(),
		.title = tr::lng_wallet_backup_update_title(),
		.strictCancel = true,
	}));
}

void ShowBackupTopUpAlert(
		std::shared_ptr<Main::SessionShow> show,
		int64 feeNano,
		Fn<void()> notNow) {
	const auto rate = show->session().wallet().rates().current();
	show->showBox(Ui::MakeConfirmBox({
		.text = RotationFeeText(
			tr::lng_wallet_backup_topup_text,
			feeNano,
			rate),
		.confirmed = [=](Fn<void()> close) {
			close();
			ShowWalletReceiveBox(&show->session(), show);
		},
		.cancelled = [=](Fn<void()> close) {
			close();
			notNow();
		},
		.confirmText = tr::lng_wallet_backup_topup_confirm(),
		.cancelText = tr::lng_wallet_backup_update_later(),
		.title = tr::lng_wallet_backup_topup_title(),
		.strictCancel = true,
	}));
}

void OfferBackupUpdate(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy) {
	auto &wallet = show->session().wallet();
	const auto weak = base::make_weak(origin);
	const auto collect = [=] {
		if (const auto strong = weak.get()) {
			CollectBackupPhrase(show, strong);
		}
	};
	if (!wallet.rotationOffered()) {
		collect();
		return;
	}
	*busy = true;
	wallet.quoteRotationFee(crl::guard(origin, [=](FeeResult fee) {
		*busy = false;
		if (fee.error == SendError::None) {
			ShowBackupUpdateAlert(show, origin, busy, fee.feeNano, collect);
		} else if (fee.error == SendError::InsufficientFees) {
			ShowBackupTopUpAlert(show, fee.feeNano, collect);
		} else {
			show->showToast(RotationQuoteErrorText(fee.error));
			collect();
		}
	}));
}

void ShowBackupUpdateFork(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy) {
	if (show->session().wallet().revealsLocally()) {
		OfferBackupUpdate(show, origin, busy);
		return;
	}
	*busy = true;
	const auto weak = base::make_weak(origin);
	StartCustodyRestore(show, [=] {
		if (const auto strong = weak.get()) {
			*busy = false;
			OfferBackupUpdate(show, strong, busy);
		}
	}, crl::guard(origin, [=] { *busy = false; }));
}

void StartBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy) {
	const auto weak = base::make_weak(origin);
	show->showBox(Ui::MakeConfirmBox({
		.text = tr::lng_wallet_backup_disable_text(tr::now),
		.confirmed = [=](Fn<void()> close) {
			close();
			if (const auto strong = weak.get()) {
				ShowBackupUpdateFork(show, strong, busy);
			}
		},
		.confirmText = tr::lng_wallet_backup_disable_confirm(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_wallet_backup_disable_title(),
	}));
}

[[nodiscard]] QStringList SplitPhraseWords(const QString &text) {
	return text.simplified().split(QChar(' '), Qt::SkipEmptyParts);
}

struct ImportCover {
	not_null<Ui::RpWidget*> widget;
	Fn<int()> height;
	Fn<void()> updateScroll;
};

[[nodiscard]] ImportCover SetupImportCover(
		not_null<Ui::GenericBox*> box,
		WalletImportMode mode) {
	const auto spacer = box->verticalLayout()->add(
		object_ptr<Ui::RpWidget>(box));
	const auto cover = Ui::CreateChild<Ui::RpWidget>(box.get());
	cover->show();

	struct State {
		std::unique_ptr<Lottie::Icon> icon;
		Ui::FlatLabel *about = nullptr;
		QPainterPath titlePath;
		int fullTitleTop = 0;
		int maxHeight = 0;
		float64 aboutOpacity = -1.;
	};
	const auto state = cover->lifetime().make_state<State>();
	state->icon = Lottie::MakeIcon({
		.name = u"wallet/paper"_q,
		.sizeOverride = QSize(
			st::walletCoverLottieSize,
			st::walletCoverLottieSize),
		.limitFps = true,
	});
	state->about = Ui::CreateChild<Ui::FlatLabel>(
		cover,
		((mode == WalletImportMode::Restore)
			? tr::lng_wallet_restore_text()
			: tr::lng_wallet_import_text()),
		st::walletPhraseTextLabel);
	state->about->setAttribute(Qt::WA_TransparentForMouseEvents);

	auto title = (mode == WalletImportMode::Restore)
		? tr::lng_wallet_restore_title()
		: tr::lng_wallet_import_title();
	std::move(title) | rpl::on_next([=](const QString &text) {
		state->titlePath = QPainterPath();
		state->titlePath.addText(
			0,
			st::boxTitle.style.font->ascent,
			st::boxTitle.style.font,
			text);
		cover->update();
	}, cover->lifetime());

	const auto countProgress = [=] {
		return (state->maxHeight > st::boxTitleHeight)
			? std::clamp(
				(cover->height() - st::boxTitleHeight)
					/ float64(state->maxHeight - st::boxTitleHeight),
				0.,
				1.)
			: 1.;
	};
	const auto countBodyOpacity = [](float64 progress) {
		return 1. - std::clamp((1. - progress) / kCoverBodyPart, 0., 1.);
	};
	const auto countArtRect = [=](float64 opacity) {
		const auto side = st::walletCoverLottieSize * opacity;
		return QRectF(
			(cover->width() - side) / 2.,
			st::walletCoverLottieMargin.top() * opacity,
			side,
			side);
	};
	const auto updateScroll = [=] {
		if (state->maxHeight <= st::boxTitleHeight) {
			return;
		}
		const auto height = std::clamp(
			state->maxHeight - box->scrollTop(),
			st::boxTitleHeight,
			state->maxHeight);
		cover->setGeometry(0, 0, box->width(), height);
		const auto opacity = countBodyOpacity(countProgress());
		if (state->aboutOpacity != opacity) {
			state->aboutOpacity = opacity;
			state->about->setOpacity(opacity);
			state->about->moveToLeft(
				st::boxRowPadding.left(),
				int(countArtRect(opacity).bottom())
					+ st::walletCoverLottieMargin.bottom()
					+ st::boxTitleFont->height
					+ st::walletPhraseTextMargin.top());
		}
	};

	const auto relayout = [=] {
		const auto width = box->width();
		if (width <= 0) {
			return;
		}
		state->about->resizeToWidth(width
			- st::boxRowPadding.left()
			- st::boxRowPadding.right());
		state->fullTitleTop = st::walletCoverLottieMargin.top()
			+ st::walletCoverLottieSize
			+ st::walletCoverLottieMargin.bottom();
		state->maxHeight = state->fullTitleTop
			+ st::boxTitleFont->height
			+ st::walletPhraseTextMargin.top()
			+ state->about->height()
			+ st::walletPhraseTextMargin.bottom();
		spacer->resize(width, state->maxHeight);
		updateScroll();
	};
	rpl::combine(
		box->widthValue(),
		state->about->heightValue()
	) | rpl::on_next(relayout, cover->lifetime());

	cover->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(cover);
		p.fillRect(cover->rect(), st::boxBg);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = countProgress();
		const auto opacity = countBodyOpacity(progress);
		if (opacity > 0.) {
			const auto artRect = countArtRect(opacity);
			const auto frame = state->icon->frame(
				QSize(int(artRect.width()), int(artRect.height())),
				[=] { cover->update(); });
			p.setOpacity(opacity);
			p.drawImage(artRect, frame.image);
		}
		p.setOpacity(1.);
		const auto titleRect = state->titlePath.boundingRect();
		p.translate(
			anim::interpolate(
				(cover->width() - titleRect.width()) / 2,
				st::boxTitlePosition.x(),
				1. - progress),
			anim::interpolate(
				state->fullTitleTop,
				st::boxTitlePosition.y(),
				1. - progress));
		p.translate(titleRect.center());
		const auto scale = 1. + kCoverTitleScale * progress;
		p.scale(scale, scale);
		p.translate(-titleRect.center());
		p.fillPath(state->titlePath, st::boxTitleFg);
	}, cover->lifetime());

	base::install_event_filter(cover, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			box->sendScrollViewportEvent(event);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	box->showFinishes() | rpl::on_next([=] {
		const auto icon = state->icon.get();
		const auto update = [=] { cover->update(); };
		if (anim::Disabled()) {
			icon->jumpTo(icon->framesCount() - 1, update);
		} else {
			icon->animate(update, 0, icon->framesCount() - 1);
		}
	}, cover->lifetime());

	return {
		.widget = cover,
		.height = [=] { return cover->height(); },
		.updateScroll = updateScroll,
	};
}

void WalletImportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		WalletImportMode mode,
		Fn<void()> restored) {
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

	const auto cover = SetupImportCover(box, mode);

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
			st::walletImportPaste);
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
			paste->moveToRight(0, st::walletImportPasteTop);
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

	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			state->error.value(),
			st::walletImportErrorLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::walletImportErrorSkip,
			st::boxRowPadding.right(),
			0),
		style::al_top);
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
			if (word.isEmpty() || !IsWordlistWord(word)) {
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
		const auto count = state->count.current();
		auto words = std::vector<QString>();
		words.reserve(count);
		for (auto i = 0; i != count; ++i) {
			words.push_back(wordAt(i));
		}
		state->importing = true;
		if (mode == WalletImportMode::Restore) {
			show->session().wallet().restoreFromPhrase(
				std::move(words),
				crl::guard(box, [=] {
					if (restored) {
						box->closeBox();
						restored();
					} else {
						show->hideLayer();
						show->showToast({
							.title = tr::lng_wallet_imported_title(tr::now),
							.text = { tr::lng_wallet_imported_text(tr::now) },
							.icon = &st::toastCheckIcon,
						});
					}
				}),
				crl::guard(box, [=](const QString &error) {
					state->importing = false;
					state->error = (error == u"PHRASE_INVALID_PHRASE"_q)
						? tr::lng_wallet_import_error(tr::now)
						: (error == u"PHRASE_KEY_MISMATCH"_q)
						? tr::lng_wallet_restore_error(tr::now)
						: tr::lng_wallet_import_failed(tr::now);
				}));
		} else {
			StartWalletReplace(show, box, std::move(words), [=] {
				state->importing = false;
			}, [=](const QString &text) {
				state->error = text;
			});
		}
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
			st::boxRoundShadow.extend);
		Ui::Shadow::paint(
			p,
			inner,
			suggestions->width(),
			st::boxRoundShadow);
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
		const auto &extend = st::boxRoundShadow.extend;
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
		if (fieldTop + field->height() <= cover.height()
			|| fieldTop >= box->height()) {
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
			: WordlistSuggestions(typed, kImportSuggestionsLimit);
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
			p.setPen(st::windowFg);
			p.drawText(left, baseline, prefix);
			p.setPen(st::windowSubTextFg);
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
		field->tabbed() | rpl::on_next([=](
				not_null<Ui::InputField::TabbedRequest*> request) {
			if (request->backward) {
				if (i > 0) {
					request->handled = true;
					state->fields[i - 1]->setFocus();
				}
				return;
			}
			request->handled = true;
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
				const auto top = field->mapTo(box, QPoint()).y();
				if (top < cover.height()) {
					box->scrollToY(
						box->scrollTop() - (cover.height() - top));
				}
			} else if (state->suggestionField == i) {
				hideSuggestions();
			}
		}, field->lifetime());
		refreshAccessories(i);
	}

	box->widthValue() | rpl::skip(1) | rpl::on_next([=] {
		repositionSuggestions();
	}, box->lifetime());
	box->setInitScrollCallback([=] {
		cover.widget->raise();
		cover.updateScroll();
		box->scrolls() | rpl::on_next([=] {
			cover.updateScroll();
			repositionSuggestions();
		}, box->lifetime());
	});
}

void WalletReplaceBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
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
	const auto creating = box->lifetime().make_state<bool>(false);
	create->setClickedCallback([=] {
		if (*creating) {
			return;
		}
		*creating = true;
		StartWalletReplace(show, box, std::nullopt, [=] {
			*creating = false;
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
		show->showBox(
			Box(WalletImportBox, show, WalletImportMode::Replace, nullptr));
	});
	Ui::AddSkip(box->verticalLayout());
}

void WalletConflictBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	struct State {
		bool busy = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto wallet = &show->session().wallet();

	box->setStyle(st::walletConflictBox);
	Ui::AddSkip(box->verticalLayout(), st::walletConflictBoxTopSkip);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_conflict_text(),
			st::boxLabel),
		st::boxRowPadding);
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto content = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box->verticalLayout()));

	const auto rebuild = [=] {
		const auto parked = wallet->parkedRecords();
		if (parked.empty()) {
			box->closeBox();
			return;
		}
		content->clear();
		auto first = true;
		for (const auto &record : parked) {
			if (!first) {
				Ui::AddSkip(content);
				Ui::AddDivider(content);
			}
			first = false;
			Ui::AddSkip(content);
			const auto key = record.publicKey;
			const auto exportOld = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					tr::lng_wallet_conflict_export(),
					st::defaultLightButton),
				st::boxRowPadding,
				style::al_justify);
			exportOld->setFullRadius(true);
			exportOld->setClickedCallback([=] {
				WalletRevealFlow(show, key);
			});
			Ui::AddSkip(content);
			const auto switchNow = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					tr::lng_wallet_conflict_switch(),
					st::attentionBoxButton),
				st::boxRowPadding,
				style::al_justify);
			switchNow->setFullRadius(true);
			switchNow->setClickedCallback([=] {
				if (state->busy) {
					return;
				}
				state->busy = true;
				wallet->dropParked(key, crl::guard(box, [=] {
					state->busy = false;
				}), crl::guard(box, [=](const QString &) {
					state->busy = false;
					show->showToast(tr::lng_wallet_phrase_error(tr::now));
				}));
			});
		}
		if (const auto width = content->width()) {
			content->resizeToWidth(width);
		}
	};
	rebuild();

	wallet->custodyUpdates(
	) | rpl::on_next(rebuild, box->lifetime());

	wallet->presenceValue(
	) | rpl::filter([](Presence presence) {
		return (presence != Presence::Ready);
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());

	Ui::AddSkip(box->verticalLayout());
	const auto cancel = box->verticalLayout()->add(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_cancel(),
			st::defaultLightButton),
		st::boxRowPadding,
		style::al_justify);
	cancel->setFullRadius(true);
	cancel->setClickedCallback([=] {
		box->closeBox();
	});
}

void AddBackupSection(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> box) {
	auto &wallet = show->session().wallet();
	const auto busy = box->lifetime().make_state<bool>(false);
	Ui::AddSubsectionTitle(container, tr::lng_wallet_backup_section());
	const auto disable = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_backup_disable(),
				st::settingsAttentionButton)));
	disable->toggleOn(wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.backupEnabled;
	}));
	disable->finishAnimating();
	disable->entity()->addClickHandler([=] {
		if (*busy) {
			return;
		}
		RunKeyRequiringAction(show, [=] {
			StartBackupDisable(show, box, busy);
		}, KeyActionKind::Reveal);
	});
	const auto enable = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_backup_enable(),
				st::settingsButtonNoIcon)));
	enable->toggleOn(wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.canEnableBackup && !capabilities.backupEnabled;
	}));
	enable->finishAnimating();
	enable->entity()->addClickHandler([=] {
		StartBackupEnable(show, box, busy);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.backupEnabled
			? tr::lng_wallet_backup_about_on()
			: capabilities.canEnableBackup
			? tr::lng_wallet_backup_about_off()
			: tr::lng_wallet_backup_about_unavailable();
	}) | rpl::flatten_latest());
	Ui::AddSkip(container);
}

void WalletKeysBackupBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	auto &wallet = show->session().wallet();
	if (wallet.presence() != Presence::Ready) {
		box->closeBox();
		return;
	}
	box->setTitle(tr::lng_wallet_keys_title());
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_wallet_keys_phrase_section());
	const auto phrase = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_keys_show_phrase(),
				st::settingsButtonNoIcon)));
	phrase->toggleOn(rpl::combine(
		wallet.capabilitiesValue(),
		wallet.deviceCustodyStateValue()
	) | rpl::map([](
			const WalletCapabilities &capabilities,
			const DeviceCustodyState &custody) {
		return capabilities.canExportPhrase
			|| (custody.mode == DeviceMode::Full);
	}));
	phrase->finishAnimating();
	phrase->entity()->addClickHandler([=] {
		RunKeyRequiringAction(show, [=] {
			WalletRevealFlow(show);
		}, KeyActionKind::Reveal);
	});
	const auto restore = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_keys_restore(),
				st::settingsButtonNoIcon)));
	restore->toggleOn(wallet.deviceCustodyStateValue(
	) | rpl::map([](const DeviceCustodyState &custody) {
		return (custody.mode == DeviceMode::ReadOnlyNotRestorable);
	}));
	restore->finishAnimating();
	restore->entity()->addClickHandler([=] {
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Restore,
			nullptr));
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_wallet_keys_phrase_about());
	Ui::AddSkip(container);
	AddBackupSection(container, show, box);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_wallet_keys_delete(),
		st::settingsAttentionButton
	)->addClickHandler([=] {
		show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_wallet_delete_text(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				show->showBox(Box(WalletReplaceBox, show));
			},
			.confirmText = tr::lng_wallet_delete_confirm(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_delete_title(),
		}));
	});
	Ui::AddSkip(container);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] BalancePalette CardBalancePalette() {
	return {
		.mark = st::activeButtonFg->c,
		.amount = st::activeButtonFg->c,
		.secondary = st::activeButtonFg->c,
	};
}

[[nodiscard]] BalancePalette SettledBalancePalette() {
	return {
		.mark = st::windowActiveTextFg->c,
		.amount = st::windowBoldFg->c,
		.secondary = st::windowSubTextFg->c,
	};
}

[[nodiscard]] float64 BalanceAmountScale(float64 progress) {
	const auto settled = st::walletBalanceHeaderMajorFont->height
		/ float64(st::walletCardBalanceMajorLabel.style.font->height);
	return 1. + (settled - 1.) * progress;
}

[[nodiscard]] float64 BalanceFiatScale(float64 progress) {
	const auto settled = st::walletBalanceHeaderFiatFont->height
		/ float64(st::walletCardFiatLabel.style.font->height);
	return 1. + (settled - 1.) * progress;
}

[[nodiscard]] int BalanceSettledRight(int outerWidth) {
	return outerWidth
		- st::separatePanelClose.width
		- st::separatePanelMenu.width
		- st::walletBalanceHeaderSkip;
}

[[nodiscard]] float64 BalanceSettledLeft(
		int outerWidth,
		int titleRight,
		float64 width) {
	return std::max(
		BalanceSettledRight(outerWidth) - width,
		titleRight + float64(st::walletBalanceHeaderSkip));
}

[[nodiscard]] float64 BalanceSettledTop() {
	const auto block = st::walletBalanceHeaderMajorFont->height
		+ st::walletBalanceHeaderLineSkip
		+ st::walletBalanceHeaderFiatFont->height;
	return (st::separatePanelTitleHeight - block) / 2.
		- st::separatePanelTitleHeight;
}

[[nodiscard]] float64 BalanceStartLeft() {
	return st::walletCardMargin.left() + st::walletCardContentLeft;
}

void BalanceInk::setContent(CreditsAmount amount, const QString &fiat) {
	_balance = amount;
	_fiatText = fiat;
	refresh();
}

void BalanceInk::setOuterWidth(int outerWidth) {
	if (_outerWidth == outerWidth) {
		return;
	}
	_outerWidth = outerWidth;
	refresh();
}

void BalanceInk::refresh() {
	const auto &majorFont = st::walletCardBalanceMajorLabel.style.font;
	const auto &minorFont = st::walletCardBalanceMinorLabel.style.font;
	const auto &fiatFont = st::walletCardFiatLabel.style.font;
	const auto minor = _balance.nano()
		? Info::ChannelEarn::MinorPart(_balance)
		: QString();
	const auto ticker = tr::lng_wallet_card_ticker(tr::now);
	const auto majorLeft = st::walletCardMarkSize
		+ st::walletCardIconMargin.right();
	const auto cardWidth = _outerWidth
		- st::walletCardMargin.left()
		- st::walletCardMargin.right();
	const auto qrLeft = CardQrRect(cardWidth).x();
	const auto tickerWidth = majorFont->width(ticker);
	const auto availableWithTicker = qrLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft
		- majorLeft
		- minorFont->width(minor)
		- st::walletCardTickerSkip
		- tickerWidth;
	const auto full = Info::ChannelEarn::MajorPart(_balance);
	const auto tickerShown = (availableWithTicker > 0)
		&& (majorFont->width(full) <= availableWithTicker);
	const auto available = tickerShown
		? availableWithTicker
		: (availableWithTicker + st::walletCardTickerSkip + tickerWidth);
	const auto major = (available > 0)
		? majorFont->elided(full, available)
		: full;
	const auto minorLeft = majorLeft + majorFont->width(major);

	_amount = QPainterPath();
	_amount.addText(majorLeft, majorFont->ascent, majorFont, major);
	_amount.addText(
		minorLeft,
		st::walletCardBalanceMinorSkip + minorFont->ascent,
		minorFont,
		minor);

	_tickerLeft = minorLeft
		+ minorFont->width(minor)
		+ (tickerShown ? st::walletCardTickerSkip : 0);
	_ticker = QPainterPath();
	if (tickerShown) {
		_ticker.addText(0, majorFont->ascent, majorFont, ticker);
	}
	_amountWidth = _tickerLeft + (tickerShown ? tickerWidth : 0);

	_fiat = QPainterPath();
	_fiat.addText(0, fiatFont->ascent, fiatFont, _fiatText);
	_fiatWidth = fiatFont->width(_fiatText);

	_markCard = Ui::Earn::IconCurrencyColored(
		st::walletCardMarkSize,
		CardBalancePalette().mark);
	_markSettled = Ui::Earn::IconCurrencyColored(
		st::walletCardMarkSize,
		SettledBalancePalette().mark);
	_markTop = AlignedMarkTop(
		st::walletCardBalanceMajorLabel.style.font,
		_markCard);
}

QRectF BalanceInk::amountRect(
		float64 progress,
		int outerWidth,
		int titleRight) const {
	const auto scale = BalanceAmountScale(progress);
	const auto width = _amountWidth * scale;
	const auto height = st::walletCardBalanceMajorLabel.style.font->height
		* scale;
	const auto x0 = BalanceStartLeft();
	const auto y0 = float64(st::walletCardTopSkip
		+ st::walletCardBalanceTop);
	const auto x1 = BalanceSettledLeft(
		outerWidth,
		titleRight,
		_amountWidth * BalanceAmountScale(1.));
	const auto y1 = BalanceSettledTop();
	return QRectF(
		x0 + (x1 - x0) * progress,
		y0 + (y1 - y0) * progress,
		width,
		height);
}

QRectF BalanceInk::fiatRect(
		float64 progress,
		int outerWidth,
		int titleRight) const {
	const auto scale = BalanceFiatScale(progress);
	const auto width = _fiatWidth * scale;
	const auto height = st::walletCardFiatLabel.style.font->height * scale;
	const auto x0 = BalanceStartLeft();
	const auto y0 = float64(st::walletCardTopSkip + st::walletCardFiatTop);
	const auto x1 = BalanceSettledLeft(
		outerWidth,
		titleRight,
		_fiatWidth * BalanceFiatScale(1.));
	const auto y1 = BalanceSettledTop()
		+ st::walletBalanceHeaderMajorFont->height
		+ st::walletBalanceHeaderLineSkip;
	return QRectF(
		x0 + (x1 - x0) * progress,
		y0 + (y1 - y0) * progress,
		width,
		height);
}

void BalanceInk::paintPass(
		QPainter &p,
		float64 progress,
		int outerWidth,
		int titleRight,
		const BalancePalette &palette,
		const QImage &mark,
		float64 secondaryOpacity) const {
	const auto amount = amountRect(progress, outerWidth, titleRight);
	const auto amountScale = BalanceAmountScale(progress);
	p.save();
	p.translate(amount.x(), amount.y());
	p.scale(amountScale, amountScale);
	p.drawImage(
		QRectF(
			0.,
			_markTop,
			st::walletCardMarkSize,
			st::walletCardMarkSize),
		mark);
	p.fillPath(_amount, palette.amount);
	p.setOpacity(p.opacity() * secondaryOpacity);
	p.translate(_tickerLeft, 0.);
	p.fillPath(_ticker, palette.secondary);
	p.restore();

	const auto fiat = fiatRect(progress, outerWidth, titleRight);
	const auto fiatScale = BalanceFiatScale(progress);
	p.save();
	p.setOpacity(p.opacity() * secondaryOpacity);
	p.translate(fiat.x(), fiat.y());
	p.scale(fiatScale, fiatScale);
	p.fillPath(_fiat, palette.secondary);
	p.restore();
}

void BalanceInk::paint(
		QPainter &p,
		float64 progress,
		int outerWidth,
		int titleRight,
		QRect card,
		QRect clip) const {
	const auto ink = boundingRect(progress, outerWidth, titleRight);
	const auto inside = card.intersected(clip);
	if (inside.intersects(ink)) {
		p.save();
		p.setClipRect(inside, Qt::IntersectClip);
		paintPass(
			p,
			progress,
			outerWidth,
			titleRight,
			CardBalancePalette(),
			_markCard,
			st::walletCardSecondaryOpacity);
		p.restore();
	}
	const auto outside = QRegion(clip) - QRegion(inside);
	if (outside.intersects(ink)) {
		p.save();
		p.setClipRegion(outside, Qt::IntersectClip);
		paintPass(
			p,
			progress,
			outerWidth,
			titleRight,
			SettledBalancePalette(),
			_markSettled,
			1.);
		p.restore();
	}
}

QRect BalanceInk::boundingRect(
		float64 progress,
		int outerWidth,
		int titleRight) const {
	const auto amount = amountRect(progress, outerWidth, titleRight);
	const auto fiat = fiatRect(progress, outerWidth, titleRight);
	return amount.united(fiat).toAlignedRect();
}

QRect BalanceInk::markRect() const {
	return QRect(
		qRound(BalanceStartLeft()),
		st::walletCardTopSkip
			+ st::walletCardBalanceTop
			+ int(base::SafeRound(_markTop)),
		st::walletCardMarkSize,
		st::walletCardMarkSize);
}

Card::Card(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show,
	rpl::producer<TextWithEntities> name)
: RpWidget(parent)
, _show(std::move(show))
, _nameStyle(st::defaultTextStyle) {
	_nameStyle.font = st::walletCardNameFont->monospace();
	_show->session().wallet().presenceValue(
	) | rpl::on_next([=](Presence) {
		refreshAddress();
	}, lifetime());

	std::move(name) | rpl::on_next([=](TextWithEntities name) {
		_name.setMarkedText(
			_nameStyle,
			tr::upper(std::move(name)),
			kMarkupTextOptions,
			Core::TextContext({
				.session = &_show->session(),
				.repaint = crl::guard(this, [=] { update(); }),
		}));
		update();
	}, lifetime());
}

void Card::setPresentation(float64 motion, float64 opacity) {
	if (_motion == motion && _opacity == opacity) {
		return;
	}
	_motion = motion;
	_opacity = opacity;
	setVisible(_opacity > 0.);
	update();
}

float64 Card::collapseScale() const {
	return 1. - (1. - st::walletCardCollapseScale) * _motion;
}

QRectF Card::paintedRect() const {
	const auto scale = collapseScale();
	return QRectF(
		width() * (1. - scale) / 2.,
		0.,
		width() * scale,
		height() * scale);
}

QRect Card::paintedQrRect() const {
	const auto painted = paintedRect();
	const auto scale = collapseScale();
	const auto qr = CardQrRect(width());
	return QRectF(
		painted.x() + qr.x() * scale,
		painted.y() + qr.y() * scale,
		qr.width() * scale,
		qr.height() * scale).toRect();
}

void Card::refreshAddress() {
	auto &wallet = _show->session().wallet();
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

void Card::paintEvent(QPaintEvent *e) {
	if (_opacity <= 0.) {
		return;
	}
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_opacity);
	const auto scale = collapseScale();
	p.translate(width() / 2., 0.);
	p.scale(scale, scale);
	p.translate(-width() / 2., 0.);

	p.setPen(Qt::NoPen);
	p.setBrush(st::activeButtonBg);
	p.drawRoundedRect(rect(), st::walletCardRadius, st::walletCardRadius);

	const auto qr = CardQrRect(width());
	const auto half = st::lineWidth / 2.;
	p.setPen(QPen(st::windowActiveTextFg, st::lineWidth));
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(
		QRectF(qr).marginsRemoved({ half, half, half, half }),
		st::walletCardQrRadius,
		st::walletCardQrRadius);
	st::walletCardQrIcon.paintInCenter(p, qr);

	const auto &nameFont = _nameStyle.font;
	const auto addressFont = st::walletCardAddressFont->monospace();
	const auto addressBaseline = width()
		- st::walletCardAddressRight
		- addressFont->height
		- addressFont->ascent;
	const auto stripLeft = addressBaseline - addressFont->descent;
	const auto nameMax = stripLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft;
	p.setPen(st::activeButtonFg);
	_name.drawLeftElided(
		p,
		st::walletCardContentLeft,
		height() - st::walletCardNameBottom - nameFont->ascent,
		nameMax,
		width(),
		1);

	if (!_addressLine1.isEmpty()) {
		p.setPen(st::windowActiveTextFg);
		p.setFont(addressFont);
		p.save();
		p.translate(addressBaseline, st::walletCardAddressSkip);
		p.rotate(90);
		p.drawText(0, 0, _addressLine1);
		p.drawText(0, -addressFont->height, _addressLine2);
		p.restore();
	}
}

Content::Content(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show)
: RpWidget(parent)
, _show(std::move(show))
, _scroll(this, st::defaultScrollArea)
, _loadMoreCheck([this] { checkLoadMore(); }) {
	auto &wallet = _show->session().wallet();
	wallet.startPolling();

	setupContent();
	_scroll->show();
}

Content::~Content() {
	_show->session().wallet().stopPolling();
}

[[nodiscard]] bool HistoryShown(not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return !wallet->listsGated()
		&& (!wallet->history().empty() || wallet->pendingSend().has_value());
}

[[nodiscard]] rpl::producer<bool> HistoryShownValue(
		not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->historyUpdates(),
		wallet->sendStateValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty
	)) | rpl::map([=] {
		return HistoryShown(session);
	}) | rpl::distinct_until_changed();
}

[[nodiscard]] rpl::producer<bool> CollectiblesShownValue(
		not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->collectiblesUpdates(),
		wallet->listsGatedValue() | rpl::to_empty
	)) | rpl::map([=] {
		return !wallet->listsGated() && !wallet->collectibles().empty();
	}) | rpl::distinct_until_changed();
}

void PaintBottomRoundedPlate(
		QPainter &p,
		QRect rect,
		const style::color &bg) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(
		rect.marginsAdded({ 0, 2 * st::callRadius, 0, 0 }),
		st::callRadius,
		st::callRadius);
}

// The three faces the area under the card can show once the lists are known
// to be empty. They are derived from one producer and are mutually exclusive
// by construction, so no two of the wraps below can ever be open at once.
enum class EmptyFace {
	None,
	About,
	Unavailable,
	Unreachable,
};

void Content::setupContent() {
	_container = _scroll->setOwnedWidget(
		object_ptr<Ui::RpWidget>(_scroll.data()));
	_column = Ui::CreateChild<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_container,
		object_ptr<Ui::VerticalLayout>(_container),
		style::margins());
	_column->show();
	const auto column = _column->entity();
	const auto wallet = &_show->session().wallet();
	auto collectiblesShown = CollectiblesShownValue(&_show->session());

	setupPinned();
	setupBalance();
	setupTabs(rpl::duplicate(collectiblesShown));
	setupStrip();
	setupListsLoading();

	const auto media = std::make_shared<CollectibleMedia>(&_show->session());
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
				st::walletAboutIconLeft,
				g.top() + (top->height() - left->height()) / 2);
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
	Ui::AddSkip(about, st::walletAboutBottomSkip);

	auto emptyFace = rpl::combine(
		HistoryShownValue(&_show->session()),
		wallet->collectiblesTabValue(),
		wallet->listsEmptyStateValue(),
		wallet->presenceValue()
	) | rpl::map([](
			bool history,
			bool collectibles,
			ListsEmptyState lists,
			Presence presence) {
		return (!lists.confirmedEmpty || history || collectibles)
			? EmptyFace::None
			: (presence == Presence::Unavailable)
			? EmptyFace::Unavailable
			: (lists.unreachable || (presence == Presence::AddressUnreadable))
			? EmptyFace::Unreachable
			: EmptyFace::About;
	});
	wrap->toggleOn(rpl::duplicate(emptyFace) | rpl::map(
		rpl::mappers::_1 == EmptyFace::About));
	wrap->finishAnimating();

	const auto statementWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto statement = statementWrap->entity();
	Ui::AddSkip(statement, st::walletAboutTopSkip);
	statement->add(
		object_ptr<Ui::FlatLabel>(
			statement,
			rpl::combine(
				tr::lng_wallet_unavailable(),
				tr::lng_wallet_state_error(),
				rpl::duplicate(emptyFace)
			) | rpl::map([](
					const QString &unavailable,
					const QString &error,
					EmptyFace face) {
				return (face == EmptyFace::Unavailable) ? unavailable : error;
			}),
			st::walletAboutTextLabel),
		st::boxRowPadding,
		style::al_top);
	Ui::AddSkip(statement, st::walletAboutBottomSkip);
	statementWrap->toggleOn(std::move(emptyFace) | rpl::map(
		(rpl::mappers::_1 == EmptyFace::Unavailable)
		|| (rpl::mappers::_1 == EmptyFace::Unreachable)));
	statementWrap->finishAnimating();

	const auto rowsTopSkip = column->add(Ui::CreateSlideSkipWidget(
		column,
		st::walletRowsTopSkip));
	rowsTopSkip->toggleOn(rpl::combine(
		std::move(collectiblesShown),
		wallet->collectiblesTabValue(),
		HistoryShownValue(&_show->session())
	) | rpl::map([](bool available, bool collectibles, bool history) {
		return available && (collectibles || history);
	}));
	rowsTopSkip->finishAnimating();

	const auto listWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto list = listWrap->entity();
	const auto rebuildList = [=] {
		list->clear();
		const auto &history = wallet->history();
		const auto &pending = wallet->pendingSend();
		if (HistoryShown(&_show->session())) {
			if (wallet->collectibles().empty()) {
				Ui::AddSkip(list, st::walletRowsTopSkip);
				Ui::AddSubsectionTitle(list, tr::lng_wallet_rows_title());
				Ui::AddSkip(list);
			}
			if (pending) {
				const auto item = ItemFromPending(*pending);
				AddHistoryRow(list, RowContentFromPending(*pending), [=] {
					ShowWalletTransactionBox(_show, item);
				});
			}
			for (const auto &item : history) {
				const auto content = RowContentFromItem(
					item,
					&_show->session());
				AddHistoryRow(list, content, [=] {
					ShowWalletTransactionBox(_show, item, media);
				}, media);
			}
			Ui::AddSkip(list, st::walletRowsTopSkip);
		}
		if (const auto width = list->width()) {
			list->resizeToWidth(width);
		}
	};
	rpl::merge(
		wallet->historyUpdates(),
		wallet->collectiblesUpdates(),
		wallet->sendStateValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty
	) | rpl::on_next(rebuildList, list->lifetime());
	listWrap->toggleOn(TransactionsShownValue(&_show->session()));
	listWrap->finishAnimating();

	const auto collectiblesWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	AddCollectiblesList(collectiblesWrap->entity(), _show, media);
	collectiblesWrap->toggleOn(wallet->collectiblesTabValue());
	collectiblesWrap->finishAnimating();

	_scroll->scrolls(
	) | rpl::on_next([=] {
		_loadMoreCheck.call();
	}, lifetime());

	_scroll->scrollTopValue(
	) | rpl::on_next([=](int) {
		updatePinned();
		updateVisibleArea();
	}, lifetime());

	Ui::ResizeFitChild(_container, _column);

	_pinnedInner->heightValue(
	) | rpl::on_next([=] {
		updateRegions();
	}, lifetime());

	_column->entity()->heightValue(
	) | rpl::on_next([=] {
		updateRegions();
		_loadMoreCheck.call();
	}, lifetime());

	_pinnedBackground->raise();
	_card->raise();
	_cardFade->raise();
	_pinned->raise();
	_cardQr->raise();
	_tabsShadow->raise();
	_headerShadow->raise();
	_stripShadow->raise();
	_strip->raise();

	const auto local = &_show->session().local();
	if (!local->readPref<bool>(kIntroTooltipShownPref)) {
		local->writePref<bool>(kIntroTooltipShownPref, true);
		SetupIntroTooltip(this, _card, [=] {
			return (collapseProgress() > 0.) ? QRect() : _ink->markRect();
		}, _pinned->heightValue() | rpl::to_empty);
	}

	setupCustodyBar();
}

void Content::setupPinned() {
	_pinnedBackground = Ui::CreateChild<Ui::RpWidget>(this);
	_pinnedBackground->setAttribute(Qt::WA_TransparentForMouseEvents);
	_pinnedBackground->setGeometry(QRect());
	_pinnedBackground->show();

	_pinned = Ui::CreateChild<Ui::RpWidget>(this);
	_pinned->show();
	_pinnedInner = Ui::CreateChild<Ui::VerticalLayout>(_pinned);
	_pinnedInner->show();

	Ui::AddSkip(_pinnedInner, st::walletCardTopSkip);
	_cardPlaceholder = _pinnedInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			_pinnedInner,
			st::walletCardHeight),
		st::walletCardMargin);
	auto name = Info::Profile::NameValue(
		_show->session().user()
	) | rpl::map([](QString name) {
		return tr::marked(std::move(name));
	});
	_card = Ui::CreateChild<Card>(this, _show, std::move(name));
	_card->setGeometry(Ui::MapFrom(
		this,
		_cardPlaceholder,
		_cardPlaceholder->rect()));
	_card->show();
	_cardQr = Ui::CreateChild<Ui::AbstractButton>(this);
	_cardQr->setClickedCallback([=] {
		ShowWalletReceiveBox(&_show->session(), _show);
	});
	_cardQr->setGeometry(Ui::MapFrom(
		this,
		_card,
		_card->paintedQrRect()));
	_cardQr->show();
	_cardFade = Ui::CreateChild<Ui::RpWidget>(this);
	_cardFade->setAttribute(Qt::WA_TransparentForMouseEvents);
	_cardFade->setGeometry(QRect());
	_cardFade->show();
	_cardFade->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_cardFade);
		p.setOpacity(_cardFadeOpacity);
		Dialogs::PaintTopFade(
			p,
			_cardFade->width(),
			_cardFade->height(),
			st::windowBgOver->c);
	}, _cardFade->lifetime());

	const auto buttons = _pinnedInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			_pinnedInner,
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
		ShowWalletReceiveBox(&_show->session(), _show);
	});
	const auto send = addPill(tr::lng_wallet_send_button(), [=] {
		const auto show = _show;
		RunKeyRequiringAction(show, [=] {
			show->showBox(Box(WalletSendBox, show, std::optional<SendFlow>()));
		});
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

	_show->session().wallet().presenceValue(
	) | rpl::on_next([=](Presence presence) {
		const auto ready = (presence == Presence::Ready);
		for (const auto button : { addFunds, send }) {
			button->setDisabled(!ready);
			button->setAttribute(Qt::WA_TransparentForMouseEvents, !ready);
			button->setTextFgOverride(ready
				? std::optional<QColor>()
				: anim::color(st::activeButtonBg, st::activeButtonFg, 0.5));
		}
	}, buttons->lifetime());

	_headerBottomSkip = _pinnedInner->add(
		Ui::CreateSlideSkipWidget(_pinnedInner, st::walletRowsTopSkip / 2));

	_headerShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	_pinnedBackground->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_pinnedBackground);
		const auto height = _pinnedBackground->height();
		const auto tabsTop = height - pinnedMin();
		p.fillRect(
			0,
			0,
			_pinnedBackground->width(),
			tabsTop,
			st::windowBgOver);
		if (tabsTop < height) {
			p.fillRect(
				0,
				tabsTop,
				_pinnedBackground->width(),
				height - tabsTop,
				st::windowBg);
		}
	}, _pinnedBackground->lifetime());

	const auto forwardWheel = [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		_scroll->viewportEvent(e);
		return base::EventFilterResult::Cancel;
	};
	base::install_event_filter(_pinned, forwardWheel);
	base::install_event_filter(_cardQr, forwardWheel);
}

void Content::setupBalance() {
	_ink = std::make_unique<BalanceInk>();

	_pinnedBalance = Ui::CreateChild<Ui::RpWidget>(_pinned);
	_pinnedBalance->setAttribute(Qt::WA_TransparentForMouseEvents);
	_pinnedBalance->show();
	_pinnedBalance->raise();
	_pinnedBalance->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		const auto progress = collapseProgress();
		const auto ink = _ink->boundingRect(progress, width(), _titleRight);
		if (!clip.intersects(ink)) {
			return;
		}
		auto p = QPainter(_pinnedBalance);
		auto hq = PainterHighQualityEnabler(p);
		_ink->paint(
			p,
			progress,
			width(),
			_titleRight,
			cardVisible(),
			clip);
	}, _pinnedBalance->lifetime());

	_titleBalance.reset(Ui::CreateChild<Ui::RpWidget>(window()));
	_titleBalance->setAttribute(Qt::WA_TransparentForMouseEvents);
	_titleBalance->show();
	_titleBalance->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_titleBalance.get());
		auto hq = PainterHighQualityEnabler(p);
		p.translate(0, st::separatePanelTitleHeight);
		_ink->paint(
			p,
			collapseProgress(),
			width(),
			_titleRight,
			QRect(),
			_titleBalance->rect().translated(
				0,
				-st::separatePanelTitleHeight));
	}, _titleBalance->lifetime());

	const auto repaintBalance = [=] {
		_pinnedBalance->update();
		_titleBalance->update();
		_paintedInk = _titleBalance->rect();
	};

	widthValue(
	) | rpl::on_next([=](int width) {
		_ink->setOuterWidth(width);
		repaintBalance();
	}, lifetime());

	rpl::combine(
		_show->session().wallet().balanceNanoValue(),
		FiatRateValue(&_show->session())
	) | rpl::on_next([=](int64 nano, FiatRate rate) {
		_ink->setContent(
			CreditsAmount(
				nano / Ui::kNanosInOne,
				nano % Ui::kNanosInOne,
				CreditsType::Ton),
			FormatFiat(nano, rate));
		repaintBalance();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_ink->refresh();
		_card->update();
		_pinnedBackground->update();
		_cardFade->update();
		repaintBalance();
	}, lifetime());

	rpl::combine(
		tr::lng_wallet_title(),
		tr::lng_wallet_card_ticker()
	) | rpl::on_next([=](const QString &title, const QString &) {
		_titleRight = st::separatePanelTitleLeft
			+ st::separatePanelTitle.style.font->width(title);
		_ink->refresh();
		repaintBalance();
	}, lifetime());
}

QRect Content::cardVisible() {
	if (_card->isHidden()) {
		return {};
	}
	return Ui::MapFrom(
		this,
		_card,
		_card->paintedRect().toAlignedRect()
	).intersected(rect());
}

void Content::setupTabs(rpl::producer<bool> collectiblesShown) {
	_tabsWrap = _pinnedInner->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsSlider>>(
			_pinnedInner,
			object_ptr<Ui::SettingsSlider>(
				_pinnedInner,
				st::walletTabsSlider),
			style::margins(0, st::walletHeaderBottomSkip, 0, 0)));
	const auto tabs = _tabsWrap->entity();
	tabs->setSections({
		tr::lng_wallet_rows_title(tr::now),
		tr::lng_wallet_rows_collectibles(tr::now),
	});
	tabs->fitWidthToSections();
	tabs->setNaturalWidth(tabs->width());
	_tabsShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	const auto wallet = &_show->session().wallet();
	tabs->setActiveSectionFast(wallet->collectiblesTab() ? 1 : 0);
	tabs->sectionActivated(
	) | rpl::on_next([=](int index) {
		wallet->setCollectiblesTab(index == 1);
		_scroll->scrollToY(0);
	}, tabs->lifetime());

	wallet->collectiblesTabValue(
	) | rpl::on_next([=](bool collectibles) {
		const auto index = collectibles ? 1 : 0;
		if (tabs->activeSection() != index) {
			tabs->setActiveSectionFast(index);
		}
	}, tabs->lifetime());

	std::move(collectiblesShown) | rpl::on_next([=](bool shown) {
		_tabsShown = shown;
		_tabsWrap->toggle(shown, anim::type::instant);
		_headerBottomSkip->toggle(!shown, anim::type::instant);
		_tabsShadow->setVisible(shown);
		updateRegions();
	}, lifetime());
}

void Content::setupStrip() {
	_stripShadow = Ui::CreateChild<Ui::PlainShadow>(this);
	_strip = Ui::CreateChild<Ui::RpWidget>(this);
	_strip->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_strip);
		PaintBottomRoundedPlate(p, _strip->rect(), st::windowBgOver);
	}, _strip->lifetime());

	const auto hint = Ui::CreateChild<Ui::FlatLabel>(
		_strip,
		tr::lng_wallet_rows_hint(),
		st::defaultSubTextLabel);
	hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	hint->show();
	rpl::combine(
		_strip->sizeValue(),
		hint->sizeValue()
	) | rpl::on_next([=](QSize size, QSize) {
		hint->moveToLeft(
			st::boxRowPadding.left(),
			(size.height() - hint->height()) / 2,
			size.width());
	}, hint->lifetime());

	TransactionsShownValue(
		&_show->session()
	) | rpl::on_next([=](bool shown) {
		_stripShown = shown;
		_strip->setVisible(shown);
		_stripShadow->setVisible(shown);
		updateRegions();
	}, lifetime());
}

void Content::setupListsLoading() {
	_listsLoading = Ui::CreateChild<Ui::RpWidget>(this);
	_listsLoading->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto &loading = st::walletListsLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		_listsLoading,
		side,
		&loading);
	indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
	Info::Statistics::AddChildToWidgetCenter(_listsLoading, indicator);

	const auto caption = Ui::CreateChild<Ui::FlatLabel>(
		_listsLoading,
		tr::lng_wallet_provisioning(),
		st::walletAboutTextLabel);
	caption->setAttribute(Qt::WA_TransparentForMouseEvents);
	_listsLoading->sizeValue(
	) | rpl::on_next([=](QSize size) {
		caption->resizeToNaturalWidth(size.width());
		caption->moveToLeft(
			(size.width() - caption->width()) / 2,
			((size.height() + side) / 2) + st::walletAboutTitleSkip,
			size.width());
	}, caption->lifetime());
	_show->session().wallet().presenceValue(
	) | rpl::map(
		rpl::mappers::_1 == Presence::Provisioning
	) | rpl::on_next([=](bool provisioning) {
		caption->setVisible(provisioning);
	}, caption->lifetime());

	_show->session().wallet().listsGatedValue(
	) | rpl::on_next([=](bool gated) {
		indicator->setVisible(gated);
		_listsLoading->setVisible(gated);
		updateRegions();
	}, lifetime());
}

void Content::setupCustodyBar() {
	_custodyBar = Ui::CreateChild<Ui::SlideWrap<Ui::AbstractButton>>(
		this,
		object_ptr<Ui::AbstractButton>(this));
	_custodyBar->hide(anim::type::instant);
	_custodyBarShadow = Ui::CreateChild<Ui::PlainShadow>(this);
	_custodyBarShadow->hide();

	const auto button = _custodyBar->entity();
	button->resize(0, st::walletInfoBarHeight);
	button->setAttribute(Qt::WA_OpaquePaintEvent);
	button->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(button).fillRect(clip, st::windowBgOver);
	}, button->lifetime());

	_custodyBarLabel = Ui::CreateChild<Ui::FlatLabel>(
		button,
		st::walletInfoBarLabel);
	_custodyBarLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto updateLabelGeometry = [=] {
		const auto available = button->width()
			- 2 * st::walletInfoBarLabelSkip;
		if (available <= 0) {
			return;
		}
		_custodyBarLabel->resizeToWidth(
			std::min(_custodyBarLabel->textMaxWidth(), available));
		_custodyBarLabel->moveToLeft(
			(button->width() - _custodyBarLabel->width()) / 2,
			(button->height() - _custodyBarLabel->height()) / 2,
			button->width());
	};
	button->widthValue(
	) | rpl::on_next(updateLabelGeometry, button->lifetime());

	enum class Bar {
		None,
		Conflict,
		ReadOnly,
	};
	const auto current = button->lifetime().make_state<Bar>(Bar::None);
	button->setClickedCallback([=] {
		if (*current == Bar::Conflict) {
			_show->showBox(Box(WalletConflictBox, _show));
		} else if (*current == Bar::ReadOnly) {
			_show->showBox(Box(
				WalletImportBox,
				_show,
				WalletImportMode::Restore,
				nullptr));
		}
	});

	auto &wallet = _show->session().wallet();
	rpl::combine(
		wallet.deviceCustodyStateValue(),
		wallet.presenceValue()
	) | rpl::map([](DeviceCustodyState state, Presence presence) {
		return (presence != Presence::Ready)
			? Bar::None
			: state.conflict
			? Bar::Conflict
			: (state.mode == DeviceMode::ReadOnlyNotRestorable)
			? Bar::ReadOnly
			: Bar::None;
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](Bar bar) {
		*current = bar;
		if (bar != Bar::None) {
			_custodyBarLabel->setText((bar == Bar::Conflict)
				? tr::lng_wallet_conflict_bar(tr::now)
				: tr::lng_wallet_readonly_bar(tr::now));
			updateLabelGeometry();
		}
		_custodyBar->toggle(bar != Bar::None, anim::type::normal);
	}, lifetime());

	_custodyBarShadow->showOn(rpl::combine(
		_custodyBar->shownValue(),
		_custodyBar->heightValue(),
		rpl::mappers::_1 && rpl::mappers::_2 > 0
	) | rpl::filter([=](bool shown) {
		return (shown == _custodyBarShadow->isHidden());
	}));
	_custodyBar->geometryValue(
	) | rpl::on_next([=](QRect geometry) {
		_custodyBarShadow->setGeometry(
			geometry.x(),
			geometry.y() + geometry.height(),
			geometry.width(),
			st::lineWidth);
	}, _custodyBar->lifetime());
	_custodyBar->heightValue(
	) | rpl::on_next([=] {
		updateRegions();
	}, _custodyBar->lifetime());

	_custodyBar->raise();
	_custodyBarShadow->raise();
}

int Content::pinnedMax() const {
	return _pinnedInner->height();
}

int Content::pinnedMin() const {
	return _tabsShown ? st::walletTabsSlider.height : 0;
}

int Content::barHeight() const {
	return _custodyBar ? _custodyBar->height() : 0;
}

float64 Content::collapseProgress() const {
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	return (max > min)
		? ((max - _pinned->height()) / float64(max - min))
		: 1.;
}

void Content::updateRegions() {
	if (!width() || !height()) {
		return;
	}
	_container->resize(width(), _container->height());
	if (_pinnedInner->widthNoMargins() != width()) {
		_pinnedInner->resizeToWidth(width());
	}
	if (_custodyBar) {
		_custodyBar->resizeToWidth(width());
		_custodyBar->moveToLeft(0, 0);
	}
	const auto bar = barHeight();
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	const auto stripHeight = _stripShown
		? (st::walletRowsHintHeight + st::lineWidth)
		: 0;
	const auto open = height() - bar - max - stripHeight;
	_reserve = (_column->entity()->height() > open) ? (max - min) : 0;
	_column->setPadding({ 0, _reserve, 0, 0 });
	const auto scrollTop = bar + max - _reserve;
	_scroll->setGeometry(
		0,
		scrollTop,
		width(),
		std::max(0, height() - scrollTop - stripHeight));
	if (_listsLoading && !_listsLoading->isHidden()) {
		_listsLoading->setGeometry(
			0,
			bar + max,
			width(),
			height() - bar - max);
	}

	const auto body = Ui::MapFrom(window(), this, rect());
	_titleBalance->setGeometry(
		body.x(),
		body.y() - st::separatePanelTitleHeight,
		BalanceSettledRight(width()),
		st::separatePanelTitleHeight);

	updatePinned();
	if (_stripShown) {
		const auto stripTop = std::max(
			scrollTop,
			height() - st::walletRowsHintHeight);
		_stripShadow->setGeometry(
			0,
			stripTop - st::lineWidth,
			width(),
			st::lineWidth);
		_strip->setGeometry(0, stripTop, width(), st::walletRowsHintHeight);
	}
	_headerShadow->setGeometry(0, bar, width(), st::lineWidth);
	updateVisibleArea();
}

void Content::updateVisibleArea() {
	const auto top = _scroll->scrollTop();
	_column->setVisibleTopBottom(top, top + _scroll->height());
}

void Content::updatePinned() {
	if (!width() || !height()) {
		return;
	}
	const auto bar = barHeight();
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	const auto top = std::clamp(_scroll->scrollTop(), 0, _reserve);
	const auto height = max - top;
	_pinnedInner->moveToLeft(0, height - max, width());
	_pinned->setGeometry(0, bar, width(), height);
	const auto progress = collapseProgress();
	_pinnedBackground->setGeometry(0, bar, width(), height);
	const auto motion = std::clamp(progress / kCardMotionPart, 0., 1.);
	const auto opacity = 1.
		- std::clamp(progress / kCardFadePart, 0., 1.);
	auto cardGeometry = Ui::MapFrom(
		this,
		_cardPlaceholder,
		_cardPlaceholder->rect());
	cardGeometry.translate(
		0,
		top - qRound(st::walletCardHeight * motion));
	_card->setGeometry(cardGeometry);
	_card->setPresentation(motion, opacity);
	_cardQr->setGeometry(Ui::MapFrom(
		this,
		_card,
		_card->paintedQrRect()));
	_cardQr->setVisible(opacity > 0.);
	const auto fadeOpacity = std::clamp(
		(st::walletCardTopSkip - cardGeometry.top())
			/ float64(st::walletCardTopSkip),
		0.,
		1.);
	const auto fadeChanged = (_cardFadeOpacity != fadeOpacity);
	_cardFadeOpacity = fadeOpacity;
	_cardFade->setGeometry(
		0,
		bar,
		width(),
		st::walletSendButton.height);
	if (fadeChanged) {
		_cardFade->update();
	}
	_scroll->setVerticalBarTopSkip(bar + height - min);
	_tabsShadow->setGeometry(0, bar + height, width(), st::lineWidth);
	_headerShadow->setVisible(height == min);
	const auto paintedCard = cardVisible();
	const auto cardDirty = _paintedCard.united(paintedCard);
	_paintedCard = paintedCard;
	_pinnedBalance->setGeometry(_pinned->rect());
	if (!cardDirty.isEmpty()) {
		update(cardDirty);
		_pinnedBackground->update(
			cardDirty.intersected(_pinnedBackground->rect()));
		_pinnedBalance->update(
			cardDirty.intersected(_pinnedBalance->rect()));
	}
	if (_paintedHeight == height && _paintedMin == min) {
		return;
	}
	_paintedHeight = height;
	_paintedMin = min;
	_pinnedBackground->update();

	const auto ink = _ink->boundingRect(
		progress,
		width(),
		_titleRight
	).translated(0, st::separatePanelTitleHeight);
	const auto band = ink.intersected(_titleBalance->rect());
	const auto repaint = band.united(_paintedInk);
	_paintedInk = band;
	if (!repaint.isEmpty()) {
		_titleBalance->update(repaint);
	}
}

void Content::checkLoadMore() {
	auto &wallet = _show->session().wallet();
	if (wallet.listsGated()) {
		return;
	}
	const auto collectibles = wallet.collectiblesTab();
	const auto hasNext = collectibles
		? wallet.collectiblesHasNext()
		: wallet.historyHasNext();
	if (!hasNext) {
		return;
	}
	if (_scroll->scrollTop() + _scroll->height() >= _scroll->scrollTopMax()) {
		if (collectibles) {
			wallet.loadMoreCollectibles();
		} else {
			wallet.loadMoreHistory();
		}
	}
}

void Content::focusInEvent(QFocusEvent *e) {
	_scroll->setFocus();
}

void Content::resizeEvent(QResizeEvent *e) {
	updateRegions();
	_loadMoreCheck.call();
}

void Content::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_stripShown) {
		p.fillRect(0, 0, width(), _strip->y(), st::windowBg);
		return;
	}
	PaintBottomRoundedPlate(p, rect(), st::windowBg);
}

class CurrencyListWidget final : public Ui::RpWidget {
public:
	CurrencyListWidget(
		not_null<QWidget*> parent,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(QString)> chosen,
		std::optional<std::vector<QString>> allowedCodes);

	void updateFilter(const QString &query);
	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);
	void chooseSelected();
	void scrollToCurrent();

	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> mustScrollTo() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Row {
		QString code;
		QString name;
	};

	[[nodiscard]] const std::vector<Row> &current() const;
	[[nodiscard]] bool rowMatches(const Row &row) const;
	void refreshRows();
	void refreshFiltered();
	void refreshHeight();
	void updateSelected(QPoint localPos);
	void setSelected(int index);
	void setPressed(int pressed);
	void updateRow(int index);

	const std::shared_ptr<Main::SessionShow> _show;
	const Fn<void(QString)> _chosen;
	const std::optional<std::vector<QString>> _allowedCodes;
	QString _activeCode;
	QString _filter;
	std::vector<Row> _rows;
	std::vector<Row> _filtered;
	std::vector<std::unique_ptr<Ui::RippleAnimation>> _ripples;
	int _selected = -1;
	int _pressed = -1;
	bool _mouseSelection = false;

	rpl::event_stream<Ui::ScrollToRequest> _mustScrollTo;

};

[[nodiscard]] bool ForwardCurrencyNavigation(
		not_null<QKeyEvent*> e,
		not_null<CurrencyListWidget*> list,
		int pageHeight) {
	if (e->key() == Qt::Key_Down) {
		list->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		list->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		list->selectSkipPage(pageHeight, 1);
	} else if (e->key() == Qt::Key_PageUp) {
		list->selectSkipPage(pageHeight, -1);
	} else {
		return false;
	}
	return true;
}

void WalletChooseCurrencyBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<std::vector<QString>> allowedCodes) {
	box->setTitle(tr::lng_wallet_currency_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	AddBoxCloseButton(box);
	const auto select = box->setPinnedToTopContent(
		object_ptr<Ui::MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_country_ph()));
	const auto list = box->addRow(
		object_ptr<CurrencyListWidget>(
			box,
			show,
			[=](QString code) {
				show->session().wallet().rates().setCurrency(code);
				box->closeBox();
			},
			std::move(allowedCodes)),
		style::margins());
	box->setFocusCallback([=] { select->setInnerFocus(); });
	select->setQueryChangedCallback([=](const QString &query) {
		box->scrollToY(0);
		list->updateFilter(query);
	});
	select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		list->chooseSelected();
	});
	select->setCancelledCallback([=] { box->closeBox(); });
	list->mustScrollTo(
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		box->scrollToY(request.ymin, request.ymax);
	}, list->lifetime());
	base::install_event_filter(select, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(e.get());
		const auto pageHeight = box->height() - select->height();
		return ForwardCurrencyNavigation(key, list, pageHeight)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	box->setShowFinishedCallback([=] { list->scrollToCurrent(); });
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

CurrencyListWidget::CurrencyListWidget(
	not_null<QWidget*> parent,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(QString)> chosen,
	std::optional<std::vector<QString>> allowedCodes)
: RpWidget(parent)
, _show(std::move(show))
, _chosen(std::move(chosen))
, _allowedCodes(std::move(allowedCodes)) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	_show->session().wallet().rates().value(
	) | rpl::on_next([=] {
		refreshRows();
	}, lifetime());
}

void CurrencyListWidget::updateFilter(const QString &query) {
	const auto filter = query.trimmed();
	if (_filter == filter) {
		return;
	}
	_filter = filter;
	refreshFiltered();
	_selected = current().empty() ? -1 : 0;
	update();
}

void CurrencyListWidget::selectSkip(int direction) {
	_mouseSelection = false;
	const auto &list = current();
	const auto moved = _selected + direction;
	const auto next = (moved <= 0)
		? (list.empty() ? -1 : 0)
		: (moved >= int(list.size()))
		? -1
		: moved;
	setSelected(next);
	if (_selected >= 0) {
		_mustScrollTo.fire(Ui::ScrollToRequest(
			st::walletCurrencyListSkip
				+ _selected * st::walletCurrencyRowHeight,
			st::walletCurrencyListSkip
				+ (_selected + 1) * st::walletCurrencyRowHeight));
	}
}

void CurrencyListWidget::selectSkipPage(int height, int direction) {
	const auto rows = height / st::walletCurrencyRowHeight;
	if (!rows) {
		return;
	}
	selectSkip(rows * direction);
}

void CurrencyListWidget::chooseSelected() {
	const auto &list = current();
	if (_selected < 0 || _selected >= int(list.size())) {
		return;
	}
	const auto code = list[_selected].code;
	_chosen(code);
}

void CurrencyListWidget::scrollToCurrent() {
	const auto &list = current();
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		if (list[i].code == _activeCode) {
			_mustScrollTo.fire(Ui::ScrollToRequest(
				st::walletCurrencyListSkip
					+ i * st::walletCurrencyRowHeight,
				st::walletCurrencyListSkip
					+ (i + 1) * st::walletCurrencyRowHeight));
			return;
		}
	}
}

rpl::producer<Ui::ScrollToRequest> CurrencyListWidget::mustScrollTo() const {
	return _mustScrollTo.events();
}

void CurrencyListWidget::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	p.setClipRect(clip);

	const auto &list = current();
	if (list.empty()) {
		p.fillRect(clip, st::windowBg);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(
			QRect(0, 0, width(), st::noContactsHeight),
			tr::lng_wallet_currency_none(tr::now),
			style::al_center);
		return;
	}
	const auto skip = st::walletCurrencyListSkip;
	const auto rowHeight = st::walletCurrencyRowHeight;
	const auto count = int(list.size());
	const auto top = QRect(0, 0, width(), skip);
	if (clip.intersects(top)) {
		p.fillRect(clip.intersected(top), st::windowBg);
	}
	const auto from = std::clamp((clip.y() - skip) / rowHeight, 0, count);
	const auto till = std::clamp(
		(clip.y() + clip.height() - skip + rowHeight - 1) / rowHeight,
		0,
		count);
	const auto &icon = st::walletCurrencyCheckIcon;
	const auto left = st::walletCurrencyRowPadding.left();
	const auto right = st::walletCurrencyRowPadding.right();
	for (auto i = from; i != till; ++i) {
		const auto &row = list[i];
		const auto selected = (i == (_pressed >= 0 ? _pressed : _selected));
		const auto y = skip + i * rowHeight;
		p.fillRect(
			0,
			y,
			width(),
			rowHeight,
			selected ? st::windowBgOver : st::windowBg);
		if (i < int(_ripples.size()) && _ripples[i]) {
			_ripples[i]->paint(p, 0, y, width());
			if (_ripples[i]->empty()) {
				_ripples[i].reset();
			}
		}
		const auto textTop = y + st::walletCurrencyRowPadding.top();
		p.setFont(st::walletCurrencyRowCodeFont);
		p.setPen(st::walletCurrencyRowCodeFg);
		p.drawTextLeft(left, textTop, width(), row.code);
		if (!row.name.isEmpty()) {
			const auto codeWidth = st::walletCurrencyRowCodeFont->width(
				row.code);
			const auto nameLeft = left
				+ codeWidth
				+ st::walletCurrencyRowNameSkip;
			const auto available = width()
				- nameLeft
				- right
				- icon.width()
				- st::walletCurrencyRowNameSkip;
			p.setFont(st::normalFont);
			p.setPen(st::walletCurrencyRowNameFg);
			p.drawTextLeft(
				nameLeft,
				textTop,
				width(),
				st::normalFont->elided(row.name, available));
		}
		if (row.code == _activeCode) {
			icon.paint(
				p,
				width() - right - icon.width(),
				y + (rowHeight - icon.height()) / 2,
				width());
		}
	}
}

void CurrencyListWidget::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void CurrencyListWidget::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	setSelected(-1);
}

void CurrencyListWidget::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
}

void CurrencyListWidget::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
	setPressed(_selected);
	const auto &list = current();
	if (_pressed < 0 || _pressed >= int(list.size())) {
		return;
	}
	if (int(_ripples.size()) <= _pressed) {
		_ripples.resize(_pressed + 1);
	}
	if (!_ripples[_pressed]) {
		auto mask = Ui::RippleAnimation::RectMask(
			QSize(width(), st::walletCurrencyRowHeight));
		_ripples[_pressed] = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			std::move(mask),
			[this, index = _pressed] { updateRow(index); });
		_ripples[_pressed]->add(e->pos() - QPoint(
			0,
			st::walletCurrencyListSkip
				+ _pressed * st::walletCurrencyRowHeight));
	}
}

void CurrencyListWidget::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = _pressed;
	setPressed(-1);
	updateRow(_selected);
	if (e->button() == Qt::LeftButton
		&& pressed >= 0
		&& pressed == _selected) {
		chooseSelected();
	}
}

auto CurrencyListWidget::current() const
-> const std::vector<CurrencyListWidget::Row> & {
	return _filtered;
}

bool CurrencyListWidget::rowMatches(const Row &row) const {
	return row.code.startsWith(_filter, Qt::CaseInsensitive)
		|| row.name.startsWith(_filter, Qt::CaseInsensitive);
}

void CurrencyListWidget::refreshRows() {
	auto &rates = _show->session().wallet().rates();
	_activeCode = rates.current().currency;
	_rows.clear();
	const auto codes = rates.currencies();
	_rows.reserve(codes.size());
	for (const auto &code : codes) {
		if (_allowedCodes
			&& !ranges::contains(*_allowedCodes, code.toUpper())) {
			continue;
		}
		const auto name = Ui::CurrencyName(code);
		_rows.push_back({ code, (name == code) ? QString() : name });
	}
	refreshFiltered();
}

void CurrencyListWidget::refreshFiltered() {
	_filtered.clear();
	_filtered.reserve(_rows.size());
	for (const auto &row : _rows) {
		if (_filter.isEmpty() || rowMatches(row)) {
			_filtered.push_back(row);
		}
	}
	if (_filter.isEmpty()) {
		ranges::stable_partition(_filtered, [&](const Row &row) {
			return (row.code == _activeCode);
		});
	}
	_ripples.clear();
	_pressed = -1;
	if (_selected >= int(current().size())) {
		_selected = -1;
	}
	refreshHeight();
	update();
}

void CurrencyListWidget::refreshHeight() {
	const auto &list = current();
	resize(
		width(),
		list.empty()
			? st::noContactsHeight
			: (st::walletCurrencyListSkip
				+ int(list.size()) * st::walletCurrencyRowHeight));
}

void CurrencyListWidget::updateSelected(QPoint localPos) {
	if (!_mouseSelection) {
		return;
	}
	const auto in = visibleRegion().boundingRect().contains(
		mapFromGlobal(QCursor::pos()));
	const auto &list = current();
	const auto skip = st::walletCurrencyListSkip;
	const auto rowsHeight = int(list.size()) * st::walletCurrencyRowHeight;
	const auto selected = (in
		&& localPos.y() >= skip
		&& localPos.y() < skip + rowsHeight)
		? ((localPos.y() - skip) / st::walletCurrencyRowHeight)
		: -1;
	setSelected(selected);
}

void CurrencyListWidget::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	updateRow(_selected);
	_selected = index;
	updateRow(_selected);
}

void CurrencyListWidget::setPressed(int pressed) {
	if (_pressed >= 0
		&& _pressed < int(_ripples.size())
		&& _ripples[_pressed]) {
		_ripples[_pressed]->lastStop();
	}
	_pressed = pressed;
}

void CurrencyListWidget::updateRow(int index) {
	if (index >= 0) {
		update(
			0,
			st::walletCurrencyListSkip
				+ index * st::walletCurrencyRowHeight,
			width(),
			st::walletCurrencyRowHeight);
	}
}

} // namespace

rpl::producer<bool> TransactionsShownValue(
		not_null<Main::Session*> session) {
	return rpl::combine(
		HistoryShownValue(session),
		session->wallet().collectiblesTabValue()
	) | rpl::map([](bool history, bool collectibles) {
		return history && !collectibles;
	}) | rpl::distinct_until_changed();
}

base::unique_qptr<Ui::RpWidget> CreateContent(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Main::SessionShow> show) {
	return base::make_unique_q<Content>(parent.get(), std::move(show));
}

void FillMenu(
		std::shared_ptr<Main::SessionShow> show,
		const Ui::Menu::MenuCallback &addAction) {
	const auto currency = show->session().wallet().rates().current().currency;
	addAction(
		(Ui::Text::FixAmpersandInAction(
			tr::lng_wallet_menu_currency(tr::now))
			+ u"\t"_q
			+ currency),
		[=] {
			show->showBox(Box(
				WalletChooseCurrencyBox,
				show,
				std::nullopt));
		},
		&st::walletMenuCurrencyIcon);
	if (show->session().wallet().presence() == Presence::Ready) {
		addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_keys_title(tr::now)),
			[=] { show->showBox(Box(WalletKeysBackupBox, show)); },
			&st::menuIconPermissions);
	}
	addAction({ .isSeparator = true });
	addAction(
		Ui::Text::FixAmpersandInAction(tr::lng_wallet_how_menu(tr::now)),
		[=] { show->showBox(Box(WalletHowItWorksBox)); },
		&st::menuIconFaq);
}

bool TransferLinkValid(const QString &url) {
	return ParseRecipientFlow(url).has_value();
}

void ShowTransferLink(
		std::shared_ptr<Main::SessionShow> show,
		const QString &url) {
	const auto flow = ParseRecipientFlow(url);
	if (!flow) {
		return;
	}
	RunKeyRequiringAction(show, [=] {
		show->showBox(Box(WalletSendBox, show, flow));
	});
}

} // namespace Wallet
