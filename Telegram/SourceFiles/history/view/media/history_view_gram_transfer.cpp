/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_gram_transfer.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/star_burst.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_amount_painter.h"
#include "wallet/wallet_card_angle.h"
#include "wallet/wallet_card_gradient.h"
#include "wallet/wallet_comment.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_sending_effects.h"
#include "window/window_session_controller.h"

#include <QtCore/QLocale>

#include <limits>

#include "styles/style_chat.h"
#include "styles/style_polls.h"
#include "styles/style_wallet.h"

namespace HistoryView {
namespace {

constexpr auto kAddressGroupSize = 4;
constexpr auto kAddressGroupsPerLine = 6;
constexpr auto kGlareDuration = crl::time(1100);
constexpr auto kGlareTimeout = crl::time(400);
constexpr auto kRevealClockDuration = crl::time(43);
constexpr auto kRevealFillDuration = crl::time(160);
constexpr auto kRevealColorDelay = crl::time(20);
constexpr auto kRevealColorDuration = crl::time(200);
constexpr auto kRevealWordDelay = crl::time(33);
constexpr auto kRevealWordDuration = crl::time(140);
constexpr auto kRevealDuration = std::max({
	kRevealFillDuration,
	kRevealColorDelay + kRevealColorDuration,
	kRevealWordDelay + kRevealWordDuration,
});
constexpr auto kBumpRiseDuration = crl::time(150);
constexpr auto kBumpHoldDuration = crl::time(33);
constexpr auto kBumpFallDuration = crl::time(200);
constexpr auto kBumpSettleDuration = crl::time(200);
constexpr auto kBumpDuration = kBumpRiseDuration
	+ kBumpHoldDuration
	+ kBumpFallDuration
	+ kBumpSettleDuration;
constexpr auto kBumpAmplitude = 0.07;
constexpr auto kBumpFallEase = 1.2;
constexpr auto kBumpUndershoot = 0.043;
constexpr auto kSendingSpeed = 90.;
constexpr auto kSpinTurn = 360.;
constexpr auto kSpinDuration = crl::time(1700);
constexpr auto kSweepPeriod = 360.;
constexpr auto kBurstDelay = crl::time(90);
constexpr auto kBurstSpread = crl::time(250);
constexpr auto kBurstLifeMin = crl::time(850);
constexpr auto kBurstLifeMax = crl::time(1100);
constexpr auto kBurstDuration = kBurstDelay + kBurstSpread + kBurstLifeMax;
constexpr auto kBurstStarsPerSide = 48;
constexpr auto kBurstAppearTill = 0.2;
constexpr auto kBurstFadeAfter = 0.8;
constexpr auto kBurstDeformation = 0.1;
constexpr auto kTransitionDuration = std::max({
	kRevealDuration,
	kBumpDuration,
	kSpinDuration,
	kBurstDuration,
});

[[nodiscard]] QColor CardTickerFg() {
	return QColor(0x0f, 0xdd, 0xff);
}

[[nodiscard]] QColor CardAddressFg() {
	return QColor(0x00, 0x5e, 0xda);
}

[[nodiscard]] QColor SentBadgeBg() {
	return QColor(0x4a, 0xb4, 0x4a);
}

[[nodiscard]] QColor ReceivedBadgeBg() {
	return QColor(0x5e, 0xc2, 0xff);
}

[[nodiscard]] QWidget *PaintWidget(const QPainter &p) {
	const auto device = p.device();
	return (device && device->devType() == QInternal::Widget)
		? static_cast<QWidget*>(device)
		: nullptr;
}

struct GramTransferAction {
	FullMsgId itemId;
	int64 amount = 0;
	QString address;
	QString transactionId;
	QString comment;
	QString failReason;
	bool outgoing = false;
	bool encrypted = false;
	bool failed = false;

	friend bool operator==(
		const GramTransferAction &,
		const GramTransferAction &) = default;
};

struct GramTransferOrigin {
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<Element> view;
	base::weak_ptr<MediaGeneric> media;
	GramTransferAction action;
};

struct GramTransferDetails {
	Wallet::TransferItem item;
	// True while the box has only what the message said, and the
	// transaction it names has not been served yet.
	bool partial = true;
};

struct TransferTag {
	QString text;
	QColor bg;
};

// The corner ribbon, laid out as ValidateRotatedBadge lays out a gift badge,
// except that the word gets a fixed area and is centered inside it.
struct RibbonGeometry {
	QPoint textpos;
	int textWidth = 0;
	int twidth = 0;
	int height = 0;
	int size = 0;
};

[[nodiscard]] Wallet::ClockStyle CardClockStyle() {
	return {
		.size = st::walletChatCardClockSize,
		.stroke = st::walletChatCardClockStroke,
		.minuteHand = st::walletChatCardClockMinuteHand,
		.hourHand = st::walletChatCardClockHourHand,
	};
}

struct SettleSpin {
	float64 from = 0.;
	float64 turn = 0.;
	float64 target = 0.;
};

// Lives from the end of the sending look until all the settle started is over.
struct CardTransition {
	Ui::Animations::Basic animation;
	Wallet::ClockPose pose;
	SettleSpin spin;
	std::unique_ptr<Ui::StarBurst> burst;
	QString toText;
	QImage toWord;
	QImage frame;
	Wallet::CardBackground background;
	crl::time started = 0;
	int wordsTextWidth = 0;
};

struct SendingClock {
	Ui::Animations::Basic animation;
	Wallet::GlareCycle glare;
	Wallet::CardBackground background;
	crl::time started = 0;
	float64 angle = 0.;
};

[[nodiscard]] float64 SendingAngle(const SendingClock &clock, crl::time now) {
	return clock.angle + kSendingSpeed * (now - clock.started) / 1000.;
}

[[nodiscard]] float64 ClockwiseRemainder(float64 degrees) {
	return degrees - kSweepPeriod * std::floor(degrees / kSweepPeriod);
}

[[nodiscard]] SettleSpin StartSpin(float64 from, float64 target) {
	return {
		.from = from,
		.turn = kSpinTurn + ClockwiseRemainder(target - from),
		.target = target,
	};
}

// WHY: the live target's drift since the settle is added scaled by the
// eased progress, so the spin lands exactly where the mouse rule points now
// while a moving cursor never makes the sweep jump.
[[nodiscard]] float64 SpinAngle(
		const SettleSpin &spin,
		float64 target,
		crl::time elapsed) {
	const auto progress = std::clamp(
		elapsed / float64(kSpinDuration),
		0.,
		1.);
	return spin.from
		+ (spin.turn + target - spin.target) * anim::easeOutCubic(1., progress);
}

[[nodiscard]] Ui::StarBurstDescriptor CardBurstDescriptor() {
	const auto side = [](float64 sign) {
		return Ui::StarBurstSide{
			.sign = sign,
			.count = kBurstStarsPerSide,
			.angle = { -40., 12. },
			.reach = { 0.25, 0.65 },
		};
	};
	return {
		.sides = { side(-1.), side(1.) },
		.delay = kBurstDelay,
		.spread = kBurstSpread,
		.lifeMin = kBurstLifeMin,
		.lifeMax = kBurstLifeMax,
		.fall = { 0.20, 0.40 },
		.startX = { 0.35, 0.65 },
		.startY = { -0.25, 0.20 },
		.size = { 0.010, 0.034 },
		.alpha = { 0.40, 0.70 },
		.twinkle = { 0.1, 1.9 },
		.appearTill = kBurstAppearTill,
		.fadeAfter = kBurstFadeAfter,
		.deformation = kBurstDeformation,
	};
}

// What a card being replaced by a refreshed view passes to its successor.
struct GramTransferHandover {
	std::unique_ptr<Lottie::Icon> mark;
	std::unique_ptr<CardTransition> transition;
	std::unique_ptr<SendingClock> clock;
	base::weak_ptr<Wallet::CardAngle> angle;
	bool markStarted = false;
};

class GramTransferCardPart final
	: public MediaGenericPart
	, public base::has_weak_ptr {
public:
	GramTransferCardPart(
		GramTransferOrigin origin,
		GramTransferHandover handover);
	~GramTransferCardPart();

	[[nodiscard]] GramTransferHandover takeHandover();

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	[[nodiscard]] bool hasHeavyPart() override;
	void unloadHeavyPart() override;
	[[nodiscard]] Media::BubbleRoll bubbleRoll(QSize outer) const override;
	[[nodiscard]] QMargins bubbleRollRepaintMargins(
		QSize outer) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	struct Layout {
		QRect card;
		QString identity;
		QString badge;
		QColor badgeBg;
		int badgeTextWidth = 0;
		bool sending = false;
		QPointF clockCenter;
		QStringList addressLines;
		int markTop = 0;
		int amountTop = 0;
		int identityTop = 0;
		int addressTop = 0;
	};

	struct RibbonKey {
		QString text;
		QColor bg;
		int textWidth = 0;
		int ratio = 0;

		friend bool operator==(const RibbonKey &, const RibbonKey &) = default;
	};

	[[nodiscard]] int resolveLayout(int outerWidth);
	[[nodiscard]] bool sending() const;
	[[nodiscard]] bool waitingForPass(crl::time now) const;
	[[nodiscard]] bool sendingLook(crl::time now) const;
	[[nodiscard]] std::optional<Wallet::GlareBand> glarePass(
		crl::time now) const;
	struct Sweep {
		float64 angle = 0.;
		Wallet::CardBackground *own = nullptr;
	};
	[[nodiscard]] Sweep sweep(
		crl::time now,
		crl::time frame,
		bool still) const;
	void paintBurst(QPainter &p, crl::time now) const;
	[[nodiscard]] bool transitionFinished(crl::time now) const;
	void adopt(GramTransferHandover &&handover);
	void animateTransition() const;
	void startReveal(const SendingClock &clock, crl::time now) const;
	void attachClock() const;
	void validateReveal(crl::time now) const;
	void validateMark() const;
	void validateClock(crl::time now) const;
	void validateBadge() const;
	void validateAngle(
		QPainter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const;
	void paintGlareBorder(QPainter &p, Wallet::GlareBand band) const;
	void paintSendingClock(QPainter &p, crl::time now) const;
	void paintReveal(QPainter &p, int cardWidth, crl::time now) const;
	void showDetails(const ClickContext &context);

	const GramTransferOrigin _origin;
	const ClickHandlerPtr _detailsLink;
	Wallet::AmountPainter _amount;
	const QString _address;
	const QString _identity;
	// Rows keep clear of the widest ribbon an outgoing card can settle to.
	const int _ribbonTextWidth = 0;
	Layout _layout;
	mutable std::unique_ptr<Lottie::Icon> _mark;
	mutable std::unique_ptr<SendingClock> _clock;
	mutable std::unique_ptr<CardTransition> _transition;
	mutable base::weak_ptr<Wallet::CardAngle> _angle;
	mutable bool _markStarted = false;
	mutable bool _heavyPending = false;
	mutable QImage _badge;
	mutable RibbonKey _badgeKey;
	rpl::event_stream<> _destroyed;

};

class GramTransferCommentPart final
	: public MediaGenericPart
	, public base::has_weak_ptr {
public:
	GramTransferCommentPart(
		GramTransferOrigin origin,
		Wallet::TransferItem item,
		QString display);
	~GramTransferCommentPart();

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	void hideSpoilers() override;

	[[nodiscard]] uint16 fullSelectionLength() const override;
	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	[[nodiscard]] TextForMimeData selectedText(
		TextSelection selection) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	[[nodiscard]] int resolveLayout(int outerWidth);
	[[nodiscard]] bool covered() const;
	void createComment(Wallet::TransferItem item);
	void updateText();
	void invalidate();
	void activate(const ClickContext &context);

	const GramTransferOrigin _origin;
	const TextWithEntities _cover;
	std::unique_ptr<Wallet::TransferComment> _comment;
	std::optional<Wallet::TransferWalletIdentity> _commentIdentity;
	Ui::Text::String _text;
	QRect _textRect;
	bool _revealed = false;
	bool _retired = false;
	rpl::lifetime _commentLifetime;
	rpl::lifetime _lifetime;

};

[[nodiscard]] GramTransferAction SnapshotGramTransfer(
		not_null<HistoryItem*> item) {
	const auto transfer = item->Get<HistoryServiceGramTransfer>();
	if (!transfer) {
		return {};
	}
	return {
		.itemId = item->fullId(),
		.amount = transfer->amount,
		.address = transfer->peerAddress,
		.transactionId = transfer->transactionId,
		.comment = transfer->comment,
		.failReason = transfer->failReason,
		.outgoing = item->out(),
		.encrypted = transfer->commentEncrypted,
		.failed = item->hasFailed(),
	};
}

[[nodiscard]] HistoryItem *CurrentGramTransfer(
		const GramTransferOrigin &origin) {
	if (!origin.session
		|| !origin.view
		|| !origin.media
		|| origin.session->account().loggingOut()
		|| origin.session->account().destroyingSession()
		|| origin.session->account().maybeSession() != origin.session.get()
		|| origin.view->media() != origin.media.get()) {
		return nullptr;
	}
	const auto item = origin.session->data().message(origin.action.itemId);
	return (item
		&& origin.view->data() == item
		&& item->Has<HistoryServiceGramTransfer>()
		&& SnapshotGramTransfer(item) == origin.action)
		? item
		: nullptr;
}

[[nodiscard]] rpl::producer<> GramTransferInvalidations(
		const GramTransferOrigin &origin) {
	const auto data = &origin.session->data();
	return rpl::merge(
		data->itemDataChanges() | rpl::filter([=](not_null<HistoryItem*> item) {
			return item->fullId() == origin.action.itemId
				&& !CurrentGramTransfer(origin);
		}) | rpl::to_empty,
		data->itemViewRefreshRequest(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return item->fullId() == origin.action.itemId
				&& !CurrentGramTransfer(origin);
		}) | rpl::to_empty,
		data->itemRemoved(origin.action.itemId) | rpl::to_empty,
		data->itemIdChanged() | rpl::filter([=](Data::Session::IdChange change) {
			return change.oldId == origin.action.itemId.msg
				&& change.newId.peer == origin.action.itemId.peer;
		}) | rpl::to_empty,
		data->viewAboutToBeRemoved(
		) | rpl::filter([=](const Data::ViewRemoval &removal) {
			return removal.view == origin.view.get();
		}) | rpl::to_empty,
		origin.session->account().sessionChanges(
		) | rpl::filter([=](Main::Session *session) {
			return session != origin.session.get();
		}) | rpl::to_empty);
}

[[nodiscard]] auto GramTransferShow(
		const GramTransferOrigin &origin,
		const ClickContext &context)
-> std::shared_ptr<Main::SessionShow> {
	const auto my = context.other.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get();
	if (context.button != Qt::LeftButton
		|| !controller
		|| &controller->session() != origin.session.get()
		|| my.itemId != origin.action.itemId
		|| !CurrentGramTransfer(origin)) {
		return nullptr;
	}
	return controller->uiShow();
}

[[nodiscard]] GramTransferDetails ResolveGramTransfer(
		not_null<Main::Session*> session,
		const GramTransferAction &action) {
	auto result = GramTransferDetails();
	auto &item = result.item;
	item.source = Wallet::TransferItem::Source::Server;
	item.id = action.transactionId;
	// The id is the root of the transfer's trace, which the explorer shows.
	if (!action.transactionId.isEmpty()) {
		item.traceId = Wallet::TransactionHashFromServer(
			action.transactionId);
	}
	item.incoming = !action.outgoing;
	item.amountNano = (action.amount < 0
		&& action.amount != std::numeric_limits<int64>::min())
		? -action.amount
		: action.amount;
	item.counterparty = Wallet::CanonicalAddress(action.address);
	if (!session->data().peer(action.itemId.peer)->isNotificationsUser()) {
		// The chat names the counterparty before the served record does.
		item.kind = Wallet::TransferItem::Kind::PeerTransfer;
		item.counterpartyPeer = action.itemId.peer.value;
	}
	item.commentEncrypted = action.encrypted;
	if (!action.encrypted) {
		item.comment = action.comment;
	} else if (IsClientMsgId(action.itemId.msg)) {
		// The draft this device made for a transfer it is sending carries
		// the private comment the user typed: only a served transfer
		// carries a payload, because only the server assigns one.
		item.comment = action.comment;
	} else {
		item.encryptedPayload = Wallet::DecodeServerEncryptedComment(
			action.comment);
		if (!item.encryptedPayload.isEmpty()) {
			item.encryptedFormat
				= Wallet::TransferItem::EncryptedFormat::ServerPayload;
		}
	}
	// The message's own date stands in for the transaction's until the
	// served record names the moment the chain accepted it, which is the
	// same moment give or take the delivery.
	if (const auto message = session->data().message(action.itemId)) {
		item.date = message->date();
	}
	if (action.failed) {
		item.status = Wallet::TransferItem::Status::Failure;
		item.failureReason = action.failReason;
	}
	if (item.id.isEmpty() || item.counterparty.isEmpty()) {
		return result;
	}
	item.walletIdentity = session->wallet().transferWalletIdentity();
	const auto &history = session->wallet().history();
	const Wallet::TransferItem *match = nullptr;
	for (const auto &entry : history) {
		if (entry.source != Wallet::TransferItem::Source::Server
			|| entry.id != item.id) {
			continue;
		} else if (match) {
			return result;
		}
		match = &entry;
	}
	using Kind = Wallet::TransferItem::Kind;
	if (!match
		|| (match->kind != Kind::Transfer && match->kind != Kind::PeerTransfer)
		|| match->incoming != item.incoming
		|| match->amountNano != item.amountNano
		|| Wallet::CanonicalAddress(match->counterparty) != item.counterparty
		|| match->commentEncrypted != item.commentEncrypted
		|| (item.commentEncrypted
			? (match->encryptedFormat != item.encryptedFormat
				|| match->encryptedPayload != item.encryptedPayload)
			: (match->comment != item.comment))) {
		return result;
	}
	item = *match;
	result.partial = false;
	return result;
}

[[nodiscard]] Wallet::AmountStyle CardAmountStyle() {
	return {
		.big = st::walletCardBalanceMajorLabel.style.font,
		.small = st::walletCardBalanceMinorLabel.style.font,
		.tickerSkip = st::walletCardTickerSkip,
		.hinted = true,
	};
}

[[nodiscard]] Wallet::AmountParts SignedAmount(int64 value, bool outgoing) {
	const auto formatted = Ui::FormatTonAmount(value);
	auto whole = formatted.wholeString;
	const auto negativeSign = QString(QLocale::system().negativeSign());
	if (value < 0 && whole.startsWith(negativeSign)) {
		whole.remove(0, negativeSign.size());
	}
	return {
		.whole = (outgoing ? QChar(0x2212) : QChar('+')) + whole,
		.fraction = formatted.separator + formatted.nanoString,
		.ticker = tr::lng_action_gram_transfer_ticker(
			tr::now,
			lt_count,
			std::abs(value / float64(Ui::kNanosInOne))),
	};
}

[[nodiscard]] TransferTag ResolveTag(bool outgoing, bool failed) {
	if (!outgoing) {
		return {
			.text = tr::lng_action_gram_transfer_received_tag(tr::now),
			.bg = ReceivedBadgeBg(),
		};
	} else if (failed) {
		return {
			.text = tr::lng_action_gram_transfer_failed_tag(tr::now),
			.bg = Info::PeerGifts::BurnedBadgeBg(),
		};
	}
	return {
		.text = tr::lng_action_gram_transfer_sent_tag(tr::now),
		.bg = SentBadgeBg(),
	};
}

[[nodiscard]] int OutgoingRibbonTextWidth() {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	return std::max(
		font->width(tr::lng_action_gram_transfer_sent_tag(tr::now)),
		font->width(tr::lng_action_gram_transfer_failed_tag(tr::now)));
}

[[nodiscard]] RibbonGeometry ComputeRibbon(int textWidth) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto padding = st::chatUniqueGiftBadgePadding;
	auto result = RibbonGeometry();
	result.textWidth = textWidth;
	result.twidth = textWidth + padding.left() + padding.right();
	result.height = padding.top() + font->height + padding.bottom();
	result.size = result.twidth + font->height * 2;
	const auto skip = int(std::ceil(result.twidth / M_SQRT2));
	result.textpos = QPoint(result.size - skip, padding.top());
	return result;
}

[[nodiscard]] QPoint RibbonWordPosition(
		const RibbonGeometry &ribbon,
		const QString &text) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto padding = st::chatUniqueGiftBadgePadding;
	return QPoint(
		padding.left() + (ribbon.textWidth - font->width(text)) / 2,
		padding.top() + font->ascent);
}

[[nodiscard]] QPointF RibbonWordCenter(
		const RibbonGeometry &ribbon,
		const QString &text) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto origin = QPointF(RibbonWordPosition(ribbon, text))
		+ font->metrics().tightBoundingRect(text).center();
	return QTransform()
		.translate(ribbon.textpos.x(), ribbon.textpos.y())
		.rotate(45.)
		.map(origin);
}

// The word alone, rotated and supersampled the way the gift badges are.
[[nodiscard]] QImage RenderRibbonWord(
		const RibbonGeometry &ribbon,
		const QString &text) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto ratio = style::DevicePixelRatio();
	const auto multiplier = ratio * 3;
	const auto size = QSize(ribbon.size, ribbon.size);
	auto image = QImage(size * multiplier, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(multiplier);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.translate(ribbon.textpos);
		p.rotate(45.);
		p.setFont(font);
		p.setPen(st::activeButtonFg);
		p.drawText(RibbonWordPosition(ribbon, text), text);
	}
	auto result = image.scaled(
		size * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] QRect RibbonBandRect(const RibbonGeometry &ribbon) {
	return QRect(-5 * ribbon.twidth, 0, ribbon.twidth * 12, ribbon.height);
}

void PaintRibbonBand(
		QPainter &p,
		const RibbonGeometry &ribbon,
		const QColor &bg) {
	p.save();
	p.translate(ribbon.textpos);
	p.rotate(45.);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRect(RibbonBandRect(ribbon));
	p.restore();
}

[[nodiscard]] QImage RenderRibbon(
		const RibbonGeometry &ribbon,
		const QString &text,
		const QColor &bg) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(ribbon.size, ribbon.size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		PaintRibbonBand(p, ribbon, bg);
		p.drawImage(0, 0, RenderRibbonWord(ribbon, text));
	}
	return result;
}

[[nodiscard]] float64 RibbonReach(
		const RibbonGeometry &ribbon,
		QPointF center) {
	const auto band = QTransform()
		.translate(ribbon.textpos.x(), ribbon.textpos.y())
		.rotate(45.)
		.map(QPolygonF(QRectF(RibbonBandRect(ribbon))));
	const auto inside = band.intersected(
		QPolygonF(QRectF(0, 0, ribbon.size, ribbon.size)));
	auto result = 0.;
	for (const auto &point : inside) {
		accumulate_max(result, QLineF(center, point).length());
	}
	return result;
}

[[nodiscard]] float64 RevealProgress(
		crl::time elapsed,
		crl::time delay,
		crl::time duration) {
	return std::clamp((elapsed - delay) / float64(duration), 0., 1.);
}

[[nodiscard]] float64 BumpShape(crl::time elapsed) {
	if (elapsed <= 0 || elapsed >= kBumpDuration) {
		return 0.;
	} else if (elapsed < kBumpRiseDuration) {
		return elapsed / float64(kBumpRiseDuration);
	}
	const auto fall = elapsed - kBumpRiseDuration - kBumpHoldDuration;
	if (fall < 0) {
		return 1.;
	} else if (fall < kBumpFallDuration) {
		const auto progress = fall / float64(kBumpFallDuration);
		return std::pow(1. - progress, kBumpFallEase);
	}
	const auto settle = (fall - kBumpFallDuration)
		/ float64(kBumpSettleDuration);
	return -kBumpUndershoot * std::sin(M_PI * settle);
}

// The service sentence sits msgServiceMargin.top() above the whole block.
[[nodiscard]] float64 BumpAmplitude(QSize outer) {
	return outer.isEmpty()
		? 0.
		: std::min(
			kBumpAmplitude,
			2. * st::msgServiceMargin.top() / outer.height());
}

[[nodiscard]] QString FriendlyAddress(const QString &address) {
	const auto parsed = Wallet::ParseAddress(address);
	return parsed
		? Wallet::FormatFriendly(
			parsed->raw,
			false,
			parsed->testnet)
		: QString();
}

[[nodiscard]] QString ReadableIdentity(
		not_null<HistoryItem*> item,
		bool hasAddress) {
	if (item->history()->peer->isNotificationsUser()) {
		return tr::lng_credits_box_history_entry_anonymous(tr::now);
	}
	const auto peer = item->out() ? item->history()->peer : item->from();
	const auto user = item->history()->owner().userLoaded(peerToUser(peer->id));
	if (user) {
		const auto name = TextUtilities::SingleLine(user->name());
		if (!name.isEmpty()) {
			return name;
		}
	}
	return hasAddress
		? (item->out()
			? tr::lng_wallet_details_recipient
			: tr::lng_wallet_details_sender)(tr::now)
		: (item->out()
			? tr::lng_wallet_row_outgoing
			: tr::lng_wallet_row_incoming)(tr::now);
}

[[nodiscard]] QStringList AddressLines(
		const QString &address,
		int available) {
	if (address.isEmpty()) {
		return {};
	}
	const auto font = st::walletDetailsCollectionLabel.style.font->monospace();
	auto groups = QStringList();
	for (auto i = 0; i < address.size(); i += kAddressGroupSize) {
		groups.push_back(address.mid(i, kAddressGroupSize));
	}
	const auto linesFor = [&](int perLine) {
		auto result = QStringList();
		for (auto i = 0; i < groups.size(); i += perLine) {
			result.push_back(groups.mid(i, perLine).join(QChar(' ')));
		}
		return result;
	};
	for (auto perLine = kAddressGroupsPerLine; perLine > 1; --perLine) {
		auto lines = linesFor(perLine);
		const auto fits = ranges::all_of(lines, [&](const QString &line) {
			return font->width(line) <= available;
		});
		if (fits) {
			return lines;
		}
	}
	return groups;
}

[[nodiscard]] int GramTransferCardWidth(int outerWidth) {
	return std::max(outerWidth - 2 * st::chatUniqueGiftBorder, 0);
}

GramTransferCardPart::GramTransferCardPart(
	GramTransferOrigin origin,
	GramTransferHandover handover)
: _origin(std::move(origin))
, _detailsLink(std::make_shared<LambdaClickHandler>([
		weak = base::make_weak(this)](ClickContext context) {
	if (weak) {
		weak->showDetails(context);
	}
}))
, _amount(
	CardAmountStyle(),
	SignedAmount(_origin.action.amount, _origin.action.outgoing))
, _address(FriendlyAddress(_origin.action.address))
, _identity(
	ReadableIdentity(_origin.view->data(), !_address.isEmpty()).toUpper())
, _ribbonTextWidth(_origin.action.outgoing ? OutgoingRibbonTextWidth() : 0) {
	adopt(std::move(handover));
}

GramTransferHandover GramTransferCardPart::takeHandover() {
	return {
		.mark = std::move(_mark),
		.transition = std::move(_transition),
		.clock = std::move(_clock),
		.angle = _angle,
		.markStarted = std::exchange(_markStarted, false),
	};
}

// WHY: a sent or failed transfer refreshes its view, so the card that showed
// the clock is replaced by a new one. The new card continues what the old one
// was showing instead of restarting it: the mark keeps its frame, a sending
// or waiting card keeps its glare, and a settling card keeps its transition.
void GramTransferCardPart::adopt(GramTransferHandover &&handover) {
	_angle = handover.angle;
	if (handover.mark) {
		_mark = std::move(handover.mark);
		_markStarted = handover.markStarted;
		_heavyPending = true;
		if (_mark->valid() && _mark->animating()) {
			_mark->animate([view = _origin.view] {
				if (const auto strong = view.get()) {
					strong->repaint();
				}
			}, _mark->frameIndex(), _mark->framesCount() - 1);
		}
	}
	if (sending()) {
		if (handover.clock) {
			_clock = std::move(handover.clock);
			_heavyPending = true;
			attachClock();
		}
		return;
	} else if (anim::Disabled()) {
		return;
	} else if (handover.transition) {
		_transition = std::move(handover.transition);
		_heavyPending = true;
		animateTransition();
	} else if (handover.clock) {
		_clock = std::move(handover.clock);
		validateReveal(crl::now());
		if (_clock) {
			_heavyPending = true;
			attachClock();
		}
	}
}

void GramTransferCardPart::startReveal(
		const SendingClock &clock,
		crl::time now) const {
	_transition = std::make_unique<CardTransition>();
	_transition->pose = Wallet::SendingClockPose(now - clock.started);
	_transition->started = now;
	_transition->spin = StartSpin(
		SendingAngle(clock, now),
		_angle ? _angle->value(now) : 0.);
	const auto view = _origin.view.get();
	if (view && !view->data()->hasFailed()) {
		_transition->burst = Ui::StarBurst::Make(CardBurstDescriptor());
	}
	_heavyPending = true;
	animateTransition();
}

void GramTransferCardPart::validateReveal(crl::time now) const {
	if (!_clock || _layout.sending || sending() || waitingForPass(now)) {
		return;
	} else if (!_transition && !anim::Disabled()) {
		startReveal(*_clock, now);
	}
	_clock = nullptr;
}

void GramTransferCardPart::animateTransition() const {
	_transition->animation.init([weak = base::make_weak(this)](
			crl::time now) {
		const auto strong = weak.get();
		if (!strong || !strong->_transition) {
			return false;
		}
		auto &transition = *strong->_transition;
		// Frees the stars even while the card is not painted.
		if (transition.burst && now >= transition.started + kBurstDuration) {
			transition.burst = nullptr;
		}
		if (const auto view = strong->_origin.view.get()) {
			view->repaint();
		}
		return !strong->transitionFinished(now);
	});
	_transition->animation.start();
}

bool GramTransferCardPart::transitionFinished(crl::time now) const {
	return !_transition || (now >= _transition->started + kTransitionDuration);
}

Media::BubbleRoll GramTransferCardPart::bubbleRoll(QSize outer) const {
	if (!_transition) {
		return {};
	}
	const auto elapsed = crl::now() - _transition->started;
	return { .scale = 1. + BumpAmplitude(outer) * BumpShape(elapsed) };
}

QMargins GramTransferCardPart::bubbleRollRepaintMargins(
		QSize outer) const {
	if (!_transition) {
		return {};
	}
	const auto amplitude = BumpAmplitude(outer);
	const auto x = int(std::ceil(amplitude * outer.width() / 2.));
	const auto y = int(std::ceil(amplitude * outer.height() / 2.));
	return QMargins(x, y, x, y);
}

GramTransferCardPart::~GramTransferCardPart() {
	if (const auto angle = _angle.get()) {
		angle->forget(this);
	}
	invalidate_weak_ptrs(this);
	_destroyed.fire({});
}

void GramTransferCardPart::showDetails(const ClickContext &context) {
	const auto origin = _origin;
	const auto show = GramTransferShow(origin, context);
	if (!show) {
		return;
	}
	auto details = ResolveGramTransfer(origin.session.get(), origin.action);
	const auto firstGrams = Wallet::ShowFirstGramsIfPending(show);
	Wallet::ShowTransactionDetails(
		show,
		std::move(details.item),
		details.partial,
		nullptr,
		[weak = base::make_weak(this), origin] {
			return weak && CurrentGramTransfer(origin);
		},
		rpl::merge(GramTransferInvalidations(origin), _destroyed.events()),
		[session = origin.session] {
			if (const auto strong = session.get()) {
				Wallet::ShowWallet(strong);
			}
		},
		(firstGrams
			? (Ui::LayerOption::KeepOther | Ui::LayerOption::ShowAfterOther)
			: Ui::LayerOptions(Ui::LayerOption::KeepOther)));
}

QSize GramTransferCardPart::countOptimalSize() {
	const auto height = resolveLayout(st::chatUniqueGiftMaxWidth);
	return { st::chatUniqueGiftMaxWidth, height };
}

QSize GramTransferCardPart::countCurrentSize(int newWidth) {
	return { newWidth, resolveLayout(newWidth) };
}

int GramTransferCardPart::resolveLayout(int outerWidth) {
	const auto border = st::chatUniqueGiftBorder;
	const auto inset = st::walletCardContentLeft;
	const auto cardWidth = GramTransferCardWidth(outerWidth);
	const auto available = std::max(cardWidth - 2 * inset, 1);
	const auto tag = ResolveTag(_origin.action.outgoing, _origin.action.failed);
	_layout.badgeBg = tag.bg;
	_layout.sending = sending();
	const auto &badgeFont = st::msgServiceGiftBoxBadgeFont;
	const auto badgePadding = st::chatUniqueGiftBadgePadding;
	const auto badgeLimit = std::max(
		cardWidth - 2 * badgeFont->height
			- badgePadding.left() - badgePadding.right(),
		0);
	const auto badgeArea = std::min(badgeFont->width(tag.text), badgeLimit);
	_layout.badge = badgeFont->elided(tag.text, badgeArea);
	_layout.badgeTextWidth = badgeArea;
	if (_origin.action.outgoing) {
		const auto sent = ResolveTag(true, false).text;
		const auto sentArea = std::min(badgeFont->width(sent), badgeLimit);
		const auto ribbon = ComputeRibbon(sentArea);
		_layout.clockCenter = QPointF(cardWidth - ribbon.size, 0.)
			+ RibbonWordCenter(ribbon, badgeFont->elided(sent, sentArea));
	}
	const auto reservedArea = std::min(
		std::max(_ribbonTextWidth, badgeArea),
		badgeLimit);
	const auto badgeTextWidth = reservedArea
		+ badgePadding.left() + badgePadding.right();
	const auto badgeHeight = badgePadding.top()
		+ badgeFont->height + badgePadding.bottom();
	const auto bandReach = badgePadding.top()
		+ int(std::ceil(badgeTextWidth / M_SQRT2))
		+ int(std::ceil(M_SQRT2 * badgeHeight));
	// WHY: the ribbon's painted strip is the 45-degree band
	// W - bandReach <= x - y, so a row is clear exactly when its right
	// edge sits above that line; this pushes a row down instead of under it.
	const auto clearOfBand = [&](int right) {
		return right - cardWidth + bandReach;
	};

	const auto markSize = st::walletChatCardMarkSize;
	_layout.markTop = std::max(
		st::walletChatCardMarkTop,
		clearOfBand((cardWidth + markSize) / 2));

	_amount.setAvailableWidth(available);
	const auto amountSize = _amount.size();
	const auto scaledWidth = int(std::ceil(amountSize.width()));
	const auto amountHeight = int(std::ceil(amountSize.height()));
	_layout.amountTop = std::max(
		_layout.markTop + markSize + st::walletChatCardAmountSkip,
		clearOfBand((cardWidth + scaledWidth) / 2));

	_layout.identity = st::walletCardNameFont->elided(_identity, available);
	const auto identityWidth = st::walletCardNameFont->width(_layout.identity);
	_layout.identityTop = std::max(
		_layout.amountTop + amountHeight + st::walletChatCardNameSkip,
		clearOfBand((cardWidth + identityWidth) / 2));
	auto bottom = _layout.identityTop + st::walletCardNameFont->height;

	_layout.addressLines = AddressLines(_address, available);
	if (!_layout.addressLines.isEmpty()) {
		const auto addressFont
			= st::walletDetailsCollectionLabel.style.font->monospace();
		auto widest = 0;
		for (const auto &line : _layout.addressLines) {
			accumulate_max(widest, addressFont->width(line));
		}
		_layout.addressTop = std::max(
			bottom + st::walletChatCardAddressSkip,
			clearOfBand((cardWidth + widest) / 2));
		bottom = _layout.addressTop
			+ int(_layout.addressLines.size()) * addressFont->height;
	}
	const auto cardHeight = bottom + st::walletChatCardBottom;
	_layout.card = QRect(border, border, cardWidth, cardHeight);
	return cardHeight + 2 * border;
}

void GramTransferCardPart::validateMark() const {
	if (_mark) {
		return;
	}
	const auto size = st::walletChatCardMarkPaintSize;
	_mark = Lottie::MakeIcon({
		.name = u"gram_light"_q,
		.sizeOverride = { size, size },
	});
	if (const auto view = _origin.view.get()) {
		view->history()->owner().registerHeavyViewPart(view);
	}
}

bool GramTransferCardPart::sending() const {
	const auto view = _origin.view.get();
	return view
		&& _origin.action.outgoing
		&& !_origin.action.failed
		&& view->data()->isSending();
}

bool GramTransferCardPart::waitingForPass(crl::time now) const {
	return _clock
		&& !_transition
		&& !sending()
		&& !anim::Disabled()
		&& _clock->animation.animating()
		&& _clock->glare.progress(now).has_value();
}

bool GramTransferCardPart::sendingLook(crl::time now) const {
	return _layout.sending || waitingForPass(now);
}

void GramTransferCardPart::validateClock(crl::time now) const {
	if (waitingForPass(now)) {
		return;
	} else if (!_layout.sending) {
		_clock = nullptr;
		return;
	} else if (!sending()) {
		// A card that stopped sending must not resume its glare pass later.
		if (_clock) {
			_clock->animation.stop();
			_clock->glare = {};
		}
		return;
	} else if (_clock) {
		if (!_clock->animation.animating()) {
			attachClock();
		}
		return;
	}
	_clock = std::make_unique<SendingClock>();
	_clock->started = crl::now();
	_clock->angle = _angle ? _angle->value(_clock->started) : 0.;
	attachClock();
}

void GramTransferCardPart::attachClock() const {
	if (anim::Disabled()) {
		return;
	}
	_clock->animation.init([weak = base::make_weak(this)](crl::time now) {
		const auto strong = weak.get();
		if (!strong || !strong->_clock) {
			return false;
		}
		const auto sendingNow = strong->sending();
		if (sendingNow) {
			strong->_clock->glare.tick(now, kGlareDuration, kGlareTimeout);
		}
		if (const auto view = strong->_origin.view.get()) {
			view->repaint();
		}
		if (sendingNow || strong->waitingForPass(now)) {
			return !anim::Disabled();
		}
		// WHY: settling drops the clock and this animation with it;
		// Basic::call runs a copy of this callback, so returning is safe
		// but nothing below may touch the clock.
		strong->validateReveal(now);
		return false;
	});
	_clock->animation.start();
}

std::optional<Wallet::GlareBand> GramTransferCardPart::glarePass(
		crl::time now) const {
	if (!_clock || !sendingLook(now)) {
		return {};
	}
	const auto progress = _clock->glare.progress(now);
	if (!progress) {
		return {};
	}
	return Wallet::ComputeGlareBand(
		*progress,
		_layout.card.width(),
		st::walletChatCardGlareWidth);
}

GramTransferCardPart::Sweep GramTransferCardPart::sweep(
		crl::time now,
		crl::time frame,
		bool still) const {
	const auto shared = _angle->value(frame);
	if (still) {
		return { shared };
	} else if (_clock && sendingLook(now)) {
		return { SendingAngle(*_clock, now), &_clock->background };
	} else if (_transition) {
		const auto elapsed = now - _transition->started;
		if (elapsed < kSpinDuration) {
			return {
				SpinAngle(_transition->spin, shared, elapsed),
				&_transition->background,
			};
		}
	}
	return { shared };
}

void GramTransferCardPart::paintBurst(QPainter &p, crl::time now) const {
	const auto cardWidth = float64(_layout.card.width());
	const auto cardHeight = float64(_layout.card.height());
	const auto mark = float64(st::walletChatCardMarkSize);
	const auto radius = st::walletCardRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(0, 0, cardWidth, cardHeight), radius, radius);
	_transition->burst->paint(p, {
		.origin = QPointF(cardWidth / 2., _layout.markTop + mark / 2.),
		.emitter = mark,
		.extent = cardWidth,
		.elapsed = now - _transition->started,
		.clip = std::move(clip),
	});
}

bool GramTransferCardPart::hasHeavyPart() {
	return _mark || _clock || _transition;
}

void GramTransferCardPart::unloadHeavyPart() {
	_mark = nullptr;
	_clock = nullptr;
	_transition = nullptr;
	_markStarted = false;
	if (const auto angle = _angle.get()) {
		angle->forget(this);
	}
}

// The outline has to keep the card's own gradient under it, so the fill and
// the stroke are two passes: the background brush with no pen, then a pen
// whose gradient fades in and out with the pass and no brush at all.
void GramTransferCardPart::paintGlareBorder(
		QPainter &p,
		Wallet::GlareBand band) const {
	Wallet::PaintGlare(
		p,
		QRectF(0, 0, _layout.card.width(), _layout.card.height()),
		st::msgServiceGiftBoxRadius,
		band,
		{
			.stroke = float64(st::walletChatCardGlareStroke),
			.slope = 0.,
			.border = 1.,
			.background = 0.,
		},
		CardTickerFg());
}

void GramTransferCardPart::validateAngle(
		QPainter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const {
	const auto angle = context.st->gramCardAngle();
	if (_angle.get() != angle.get()) {
		if (const auto previous = _angle.get()) {
			previous->forget(this);
		}
		_angle = angle;
	}
	if (const auto widget = PaintWidget(p)) {
		// A swipe moves the card on screen, not the angle it is turned from.
		const auto shift = context.gestureHorizontal.visualTranslationFor(
			owner->parent()->data()->id.bare);
		const auto rect = p.transform().mapRect(QRectF(_layout.card));
		angle->track(this, widget, rect.translated(-shift, 0.));
	}
}

void GramTransferCardPart::validateBadge() const {
	if (_layout.sending) {
		return;
	}
	const auto key = RibbonKey{
		.text = _layout.badge,
		.bg = _layout.badgeBg,
		.textWidth = _layout.badgeTextWidth,
		.ratio = style::DevicePixelRatio(),
	};
	if (!_badge.isNull() && _badgeKey == key) {
		return;
	}
	_badgeKey = key;
	_badge = RenderRibbon(
		ComputeRibbon(_layout.badgeTextWidth),
		_layout.badge,
		_layout.badgeBg);
}

void GramTransferCardPart::paintSendingClock(
		QPainter &p,
		crl::time now) const {
	const auto pose = Wallet::SendingClockPose((_clock && !anim::Disabled())
		? (now - _clock->started)
		: 0);
	Wallet::PaintClock(
		p,
		CardClockStyle(),
		_layout.clockCenter,
		pose,
		CardTickerFg(),
		1.);
}

// The band fills the way a ripple fills its mask, from the clock out.
void GramTransferCardPart::paintReveal(
		QPainter &p,
		int cardWidth,
		crl::time now) const {
	const auto ribbon = ComputeRibbon(_layout.badgeTextWidth);
	auto &transition = *_transition;
	if (transition.wordsTextWidth != ribbon.textWidth
		|| transition.toText != _layout.badge) {
		transition.toWord = RenderRibbonWord(ribbon, _layout.badge);
		transition.toText = _layout.badge;
		transition.wordsTextWidth = ribbon.textWidth;
	}
	const auto elapsed = now - transition.started;
	const auto origin = QPointF(cardWidth - ribbon.size, 0.);
	const auto center = _layout.clockCenter - origin;
	const auto c = RevealProgress(
		elapsed,
		kRevealColorDelay,
		kRevealColorDuration);
	const auto color = anim::color(
		CardTickerFg(),
		_layout.badgeBg,
		1. - (1. - c) * (1. - c));
	const auto outer = st::walletChatCardClockSize / 2.;
	const auto inner = outer - st::walletChatCardClockStroke;
	const auto reach = RibbonReach(ribbon, center);
	const auto radius = outer
		+ (reach - outer) * RevealProgress(elapsed, 0, kRevealFillDuration);
	const auto scale = std::max(
		1. - elapsed / float64(kRevealClockDuration),
		0.);
	const auto w = RevealProgress(
		elapsed,
		kRevealWordDelay,
		kRevealWordDuration);
	const auto word = 1. - (1. - w) * (1. - w);
	const auto ratio = style::DevicePixelRatio();
	const auto size = QSize(ribbon.size, ribbon.size) * ratio;
	if (transition.frame.size() != size) {
		transition.frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
	}
	transition.frame.setDevicePixelRatio(ratio);
	transition.frame.fill(Qt::transparent);
	{
		auto q = QPainter(&transition.frame);
		auto hq = PainterHighQualityEnabler(q);
		PaintRibbonBand(q, ribbon, color);
		q.setPen(Qt::NoPen);
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		if (radius < reach) {
			auto outside = QPainterPath();
			outside.setFillRule(Qt::OddEvenFill);
			outside.addRect(QRectF(0., 0., ribbon.size, ribbon.size));
			outside.addEllipse(center, radius, radius);
			q.fillPath(outside, QColor(0, 0, 0));
		}
		if (scale > 0.) {
			q.setBrush(QColor(0, 0, 0));
			q.drawEllipse(center, inner * scale, inner * scale);
		}
		if (word > 0.) {
			const auto shift = st::walletChatCardRevealWordShift
				* (1. - word)
				/ M_SQRT2;
			q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
			q.setOpacity(word);
			q.drawImage(QPointF(-shift, shift), transition.toWord);
			q.setOpacity(1.);
		}
		if (scale > 0.) {
			q.setCompositionMode(QPainter::CompositionMode_SourceOver);
			Wallet::PaintClock(
				q,
				CardClockStyle(),
				center,
				transition.pose,
				color,
				scale);
		}
	}
	p.drawImage(origin, transition.frame);
}

void GramTransferCardPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto now = crl::now();
	const auto frame = context.now ? context.now : now;
	if (_transition && transitionFinished(now)) {
		_transition = nullptr;
	}
	validateAngle(p, owner, context);
	// WHY: a card relaid out as settled keeps its clock until this paint,
	// so a stale paint shows the live pose and the reveal starts from it
	// here; a later replacement continues this transition.
	validateReveal(now);
	validateMark();
	validateClock(now);
	validateBadge();
	if (std::exchange(_heavyPending, false)) {
		if (const auto view = _origin.view.get()) {
			view->history()->owner().registerHeavyViewPart(view);
		}
	}
	const auto still = context.paused || anim::Disabled();
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	const auto outer = QRect(0, 0, width(), height());
	const auto radius = st::msgServiceGiftBoxRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(outer, radius, radius);
	p.setClipPath(clip, Qt::IntersectClip);
	const auto sweep = this->sweep(now, frame, still);
	(sweep.own ? *sweep.own : _angle->background()).paint(
		p,
		_layout.card,
		sweep.angle);
	p.translate(_layout.card.topLeft());
	const auto cardWidth = _layout.card.width();
	const auto pass = glarePass(now);
	if (pass) {
		paintGlareBorder(p, *pass);
	}
	if (_transition && _transition->burst && !still) {
		paintBurst(p, now);
	}
	if (_mark->valid()) {
		const auto last = _mark->framesCount() - 1;
		const auto paused = context.paused
			|| anim::Disabled()
			|| On(PowerSaving::kStickersChat);
		const auto again = sending() && !_mark->animating();
		if (paused) {
			if (!_markStarted && _mark->frameIndex() != last) {
				_mark->jumpTo(last, nullptr);
			}
		} else if (!_markStarted || again) {
			_markStarted = true;
			_mark->animate([view = _origin.view] {
				if (const auto strong = view.get()) {
					strong->repaint();
				}
			}, 0, last);
		}
	}
	const auto markPaint = st::walletChatCardMarkPaintSize;
	const auto markShift = (markPaint - st::walletChatCardMarkSize) / 2;
	_mark->paint(
		p,
		(cardWidth - markPaint) / 2,
		_layout.markTop - markShift);
	_amount.paint(
		p,
		QPointF(
			(cardWidth - _amount.size().width()) / 2.,
			_layout.amountTop),
		{ .digits = st::activeButtonFg->c, .ticker = CardTickerFg() });
	p.setPen(CardTickerFg());
	p.setFont(st::walletCardNameFont);
	p.drawText(
		(cardWidth - st::walletCardNameFont->width(_layout.identity)) / 2,
		_layout.identityTop + st::walletCardNameFont->ascent,
		_layout.identity);
	const auto addressFont
		= st::walletDetailsCollectionLabel.style.font->monospace();
	if (pass) {
		auto gradient = QLinearGradient(
			QPointF(pass->from, 0),
			QPointF(pass->till, 0));
		gradient.setStops({
			{ 0., CardAddressFg() },
			{ 0.5, CardTickerFg() },
			{ 1., CardAddressFg() },
		});
		p.setPen(QPen(QBrush(gradient), 0));
	} else {
		p.setPen(CardAddressFg());
	}
	p.setFont(addressFont);
	auto top = _layout.addressTop;
	for (const auto &line : _layout.addressLines) {
		p.drawText(
			(cardWidth - addressFont->width(line)) / 2,
			top + addressFont->ascent,
			line);
		top += addressFont->height;
	}
	if (sendingLook(now)) {
		paintSendingClock(p, now);
	} else if (_transition
		&& now < _transition->started + kRevealDuration) {
		paintReveal(p, cardWidth, now);
	} else {
		p.drawImage(
			QPointF(cardWidth - _badge.width() / _badge.devicePixelRatio(), 0.),
			_badge);
	}
	p.restore();
}

TextState GramTransferCardPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	if (_layout.card.contains(point)) {
		auto result = TextState();
		result.link = _detailsLink;
		return result;
	}
	return {};
}

GramTransferCommentPart::GramTransferCommentPart(
	GramTransferOrigin origin,
	Wallet::TransferItem item,
	QString display)
: _origin(std::move(origin))
, _cover(item.commentEncrypted
	? Wallet::TransferCommentCover(item)
	: tr::marked(std::move(display)))
, _text(0) {
	if (Wallet::EncryptedCommentRevealable(item)) {
		createComment(std::move(item));
		GramTransferInvalidations(_origin) | rpl::on_next([=] {
			invalidate();
		}, _lifetime);
	}
	updateText();
}

GramTransferCommentPart::~GramTransferCommentPart() {
	_retired = true;
	_lifetime.destroy();
	_commentLifetime.destroy();
	_comment = nullptr;
}

bool GramTransferCommentPart::covered() const {
	return _comment && !_revealed;
}

void GramTransferCommentPart::createComment(Wallet::TransferItem item) {
	_commentLifetime.destroy();
	_comment = nullptr;
	_commentIdentity = item.walletIdentity;
	const auto weak = base::make_weak(this);
	_comment = std::make_unique<Wallet::TransferComment>(
		_origin.session.get(),
		std::move(item),
		[weak] {
			return weak
				&& !weak->_retired
				&& CurrentGramTransfer(weak->_origin);
		});
	_comment->changes() | rpl::on_next([=] {
		const auto revealed = _comment->plaintext().has_value();
		if (revealed == _revealed) {
			return;
		}
		_revealed = revealed;
		updateText();
		if (const auto view = _origin.view.get()) {
			if (revealed) {
				view->history()->owner().registerShownSpoiler(view);
			}
			if (_retired) {
				// The list may be mid-removal and re-lays the view out anyway.
				view->setPendingResize();
			} else {
				view->history()->owner().requestViewResize(view);
			}
			view->repaint();
		}
	}, _commentLifetime);
}

void GramTransferCommentPart::updateText() {
	const auto view = _origin.view;
	auto text = Ui::Text::String(0);
	text.setMarkedText(
		st::serviceTextStyle,
		_revealed ? tr::marked(*_comment->plaintext()) : _cover,
		kPlainTextOptions,
		{
			.repaint = [view] {
				if (view) {
					view->repaint();
				}
			},
		});
	_text = std::move(text);
	if (_text.hasSpoilers()) {
		const auto weak = base::make_weak(this);
		_text.setSpoilerLinkFilter([weak](const ClickContext &context) {
			const auto strong = weak.get();
			if (!strong || context.button != Qt::LeftButton) {
				return false;
			} else if (strong->_comment) {
				strong->activate(context);
				return false;
			}
			// The user's own comment waits for no key, so it lifts the way
			// a spoiler in a message text does.
			if (const auto view = strong->_origin.view.get()) {
				view->history()->owner().registerShownSpoiler(view);
			}
			return true;
		});
	}
}

// A revealed comment is covered again wherever a spoiler is, and dropping
// the plaintext re-covers it through the same changes() handler.
void GramTransferCommentPart::hideSpoilers() {
	if (_text.hasSpoilers()) {
		_text.setSpoilerRevealed(false, anim::type::instant);
	}
	if (_comment) {
		_comment->reset();
	}
}

void GramTransferCommentPart::invalidate() {
	_retired = true;
	if (_comment) {
		_comment->reset();
	}
}

void GramTransferCommentPart::activate(const ClickContext &context) {
	if (_retired || !_comment) {
		return;
	}
	if (const auto show = GramTransferShow(_origin, context)) {
		if (_comment->pending() || _comment->plaintext().has_value()) {
			return;
		}
		auto details = ResolveGramTransfer(_origin.session.get(), _origin.action);
		if (details.item.walletIdentity != _commentIdentity) {
			if (!Wallet::EncryptedCommentRevealable(details.item)) {
				return;
			}
			createComment(std::move(details.item));
		}
		_comment->activate(show);
	}
}

QSize GramTransferCommentPart::countOptimalSize() {
	const auto height = resolveLayout(st::chatUniqueGiftMaxWidth);
	return { st::chatUniqueGiftMaxWidth, height };
}

QSize GramTransferCommentPart::countCurrentSize(int newWidth) {
	return { newWidth, resolveLayout(newWidth) };
}

int GramTransferCommentPart::resolveLayout(int outerWidth) {
	if (_text.isEmpty()) {
		_textRect = QRect();
		return 0;
	}
	const auto skip = st::walletChatCardCommentSkip;
	const auto limit = std::max(GramTransferCardWidth(outerWidth), 1);
	const auto size = Ui::Text::CountOptimalTextSize(_text, 0, limit);
	_textRect = QRect(
		(outerWidth - size.width()) / 2,
		skip,
		size.width(),
		size.height());
	return skip + size.height() + skip + st::chatUniqueGiftBorder;
}

void GramTransferCommentPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	if (_textRect.isEmpty()) {
		return;
	}
	p.setPen(context.st->msgServiceFg());
	_text.draw(p, {
		.position = _textRect.topLeft(),
		.outerWidth = outerWidth,
		.availableWidth = _textRect.width(),
		.align = style::al_top,
		.palette = &context.st->serviceTextPalette(),
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		.selection = covered()
			? TextSelection()
			: (context.selection == FullSelection)
			? AllTextSelection
			: context.selection,
	});
}

TextState GramTransferCommentPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	if (_textRect.isEmpty()) {
		return {};
	}
	auto textRequest = request.forText();
	textRequest.align = style::al_top;
	auto result = TextState(nullptr, _text.getState(
		point - _textRect.topLeft(),
		_textRect.width(),
		textRequest));
	if (covered()) {
		auto cover = TextState();
		if (_textRect.contains(point)) {
			cover.link = result.link;
		}
		return cover;
	}
	result.link = nullptr;
	if (!_textRect.contains(point)) {
		result.cursor = CursorState::None;
	}
	result.overMessageText = (result.cursor == CursorState::Text);
	return result;
}

uint16 GramTransferCommentPart::fullSelectionLength() const {
	return covered() ? 0 : _text.length();
}

TextSelection GramTransferCommentPart::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return covered()
		? TextSelection()
		: (selection == FullSelection)
		? selection
		: _text.adjustSelection(selection, type);
}

TextForMimeData GramTransferCommentPart::selectedText(
		TextSelection selection) const {
	return covered()
		? TextForMimeData()
		: _text.toTextForMimeData((selection == FullSelection)
			? AllTextSelection
			: selection);
}

} // namespace

std::unique_ptr<Media> CreateGramTransferMedia(
		not_null<Element*> parent,
		Element *replacing) {
	return std::make_unique<MediaGeneric>(
		parent,
		[parent, replacing](
				not_null<MediaGeneric*> media,
				Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
			const auto item = parent->data();
			const auto transfer = item->Get<HistoryServiceGramTransfer>();
			if (!transfer) {
				return;
			}
			const auto origin = GramTransferOrigin{
				.session = base::make_weak(&item->history()->session()),
				.view = base::make_weak(parent),
				.media = base::make_weak(media),
				.action = SnapshotGramTransfer(item),
			};
			// WHY: the card is replaced both with the whole view, when the
			// view is refreshed, and in place, when an edit refreshes the
			// view's text; the media being replaced is |parent|'s own then.
			auto handover = GramTransferHandover();
			const auto source = replacing ? replacing : parent.get();
			const auto previous = dynamic_cast<MediaGeneric*>(
				source->media());
			const auto card = previous
				? dynamic_cast<GramTransferCardPart*>(previous->partAt(0))
				: nullptr;
			if (card && source->data() == item) {
				handover = card->takeHandover();
				if (replacing) {
					// The replaced view no longer holds what it registered.
					replacing->checkHeavyPart();
				}
			}
			push(std::make_unique<GramTransferCardPart>(
				origin,
				std::move(handover)));
			if (transfer->commentEncrypted || !transfer->comment.isEmpty()) {
				auto details = ResolveGramTransfer(
					origin.session.get(),
					origin.action);
				push(std::make_unique<GramTransferCommentPart>(
					origin,
					std::move(details.item),
					transfer->commentText()));
			}
		},
		MediaGenericDescriptor{
			.maxWidth = st::chatUniqueGiftMaxWidth,
			.service = true,
			.hideServiceText = false,
		});
}

} // namespace HistoryView
