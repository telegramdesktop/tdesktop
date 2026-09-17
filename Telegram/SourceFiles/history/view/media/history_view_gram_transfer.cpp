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
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_gradient.h"
#include "wallet/wallet_comment.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "window/window_session_controller.h"

#include <QtCore/QLocale>

#include <limits>

#include "styles/style_chat.h"
#include "styles/style_wallet.h"

namespace HistoryView {
namespace {

constexpr auto kAddressGroupSize = 4;
constexpr auto kAddressGroupsPerLine = 6;

struct GramTransferAction {
	FullMsgId itemId;
	int64 amount = 0;
	QString address;
	QString transactionId;
	QString comment;
	bool outgoing = false;
	bool encrypted = false;

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

class GramTransferCardPart final
	: public MediaGenericPart
	, public base::has_weak_ptr {
public:
	explicit GramTransferCardPart(GramTransferOrigin origin);
	~GramTransferCardPart();

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	struct Layout {
		QRect card;
		QRect info;
		QString identity;
		QString badge;
		QStringList addressLines;
		int amountTop = 0;
		int amountWidth = 0;
		int identityTop = 0;
		int addressTop = 0;
		float64 amountScale = 1.;
		float64 amountShift = 0.;
	};

	[[nodiscard]] int resolveLayout(int outerWidth);
	[[nodiscard]] QString tagText() const;
	void validateMark() const;
	void validateBadge() const;
	void showDetails(const ClickContext &context);

	const GramTransferOrigin _origin;
	const ClickHandlerPtr _infoLink;
	const QString _amount;
	const QString _address;
	const QString _identity;
	Layout _layout;
	mutable QImage _mark;
	mutable QColor _markColor;
	mutable float64 _markTop = 0.;
	mutable QImage _badge;
	mutable Info::PeerGifts::GiftBadge _badgeKey;
	mutable QMargins _badgePadding;
	mutable style::font _badgeFont;
	mutable QPoint _lastPoint;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
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
	Expects(transfer != nullptr);

	return {
		.itemId = item->fullId(),
		.amount = transfer->amount,
		.address = transfer->peerAddress,
		.transactionId = transfer->transactionId,
		.comment = transfer->comment,
		.outgoing = item->out(),
		.encrypted = transfer->commentEncrypted,
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
	item.commentEncrypted = action.encrypted;
	if (action.encrypted) {
		item.encryptedPayload = Wallet::DecodeServerEncryptedComment(
			action.comment);
		if (!item.encryptedPayload.isEmpty()) {
			item.encryptedFormat
				= Wallet::TransferItem::EncryptedFormat::ServerPayload;
		}
	} else {
		item.comment = action.comment;
	}
	// The message's own date stands in for the transaction's until the
	// served record names the moment the chain accepted it, which is the
	// same moment give or take the delivery.
	if (const auto message = session->data().message(action.itemId)) {
		item.date = message->date();
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

[[nodiscard]] QString SignedAmount(int64 value, bool outgoing) {
	auto amount = Ui::FormatTonAmount(value).full;
	const auto negativeSign = QString(QLocale::system().negativeSign());
	if (value < 0 && amount.startsWith(negativeSign)) {
		amount.remove(0, negativeSign.size());
	}
	return (outgoing ? QChar(0x2212) : QChar('+')) + amount;
}

[[nodiscard]] QString FriendlyAddress(const QString &address) {
	const auto parsed = Wallet::ParseAddress(address);
	return parsed
		? Wallet::FormatFriendly(
			parsed->raw,
			parsed->bounceable,
			parsed->testnet)
		: QString();
}

[[nodiscard]] QString ReadableIdentity(
		not_null<HistoryItem*> item,
		bool hasAddress) {
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

GramTransferCardPart::GramTransferCardPart(GramTransferOrigin origin)
: _origin(std::move(origin))
, _infoLink(std::make_shared<LambdaClickHandler>([
		weak = base::make_weak(this)](ClickContext context) {
	if (weak) {
		weak->showDetails(context);
	}
}))
, _amount(SignedAmount(_origin.action.amount, _origin.action.outgoing))
, _address(FriendlyAddress(_origin.action.address))
, _identity(ReadableIdentity(_origin.view->data(), !_address.isEmpty())) {
}

QString GramTransferCardPart::tagText() const {
	if (!_origin.action.outgoing) {
		return tr::lng_action_gram_transfer_received_tag(tr::now);
	}
	const auto view = _origin.view.get();
	const auto sending = view && view->data()->isSending();
	return (sending
		? tr::lng_action_gram_transfer_sending_tag
		: tr::lng_action_gram_transfer_sent_tag)(tr::now);
}

GramTransferCardPart::~GramTransferCardPart() {
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
		});
}

QSize GramTransferCardPart::countOptimalSize() {
	const auto height = resolveLayout(st::chatUniqueGiftMaxWidth);
	return { st::chatUniqueGiftMaxWidth, height };
}

QSize GramTransferCardPart::countCurrentSize(int newWidth) {
	return { newWidth, resolveLayout(newWidth) };
}

int GramTransferCardPart::resolveLayout(int outerWidth) {
	validateMark();
	const auto border = st::chatUniqueGiftBorder;
	const auto inset = st::walletCardContentLeft;
	const auto gap = st::walletCardContentSkip;
	const auto cardWidth = GramTransferCardWidth(outerWidth);
	const auto available = std::max(cardWidth - 2 * inset, 1);
	const auto &badgeFont = st::msgServiceGiftBoxBadgeFont;
	const auto badgePadding = st::chatUniqueGiftBadgePadding;
	_layout.badge = badgeFont->elided(tagText(), std::max(
		cardWidth - 2 * badgeFont->height
			- badgePadding.left() - badgePadding.right(),
		0));
	const auto badgeTextWidth = badgeFont->width(_layout.badge)
		+ badgePadding.left() + badgePadding.right();
	const auto badgeSide = badgeTextWidth + 2 * badgeFont->height;
	const auto badgeHeight = badgePadding.top()
		+ badgeFont->height + badgePadding.bottom();
	const auto bandReach = badgePadding.top()
		+ int(std::ceil(badgeTextWidth / M_SQRT2))
		+ int(std::ceil(M_SQRT2 * badgeHeight));
	_layout.amountTop = std::max(
		st::walletCardBalanceTop,
		std::min(badgeSide, bandReach) + gap);
	const auto &amountFont = st::walletCardBalanceMajorLabel.style.font;
	_layout.amountWidth = amountFont->width(_amount);
	const auto groupWidth = _layout.amountWidth
		+ st::walletCardIconMargin.right() + st::walletCardMarkSize;
	const auto infoSize = st::walletCardInfoSize;
	const auto sameRow = groupWidth + gap + infoSize.width() <= available;
	_layout.amountScale = std::min(1., available / float64(groupWidth));
	_layout.amountShift = std::max(-_markTop, 0.);
	const auto amountHeight = int(std::ceil(_layout.amountScale
		* (_layout.amountShift + std::max(
			float64(amountFont->height),
			_markTop + st::walletCardMarkSize))));
	const auto rowTop = _layout.amountTop;
	const auto rowHeight = std::max(amountHeight, infoSize.height());
	_layout.info = QRect(
		QPoint(
			cardWidth - inset - infoSize.width(),
			sameRow
				? (rowTop + (rowHeight - infoSize.height()) / 2)
				: (rowTop + amountHeight + gap)),
		infoSize);
	if (sameRow) {
		_layout.amountTop += (rowHeight - amountHeight) / 2;
	}
	_layout.identityTop = (sameRow
		? (rowTop + rowHeight)
		: (_layout.info.y() + infoSize.height())) + gap;
	_layout.identity = st::walletCardNameFont->elided(_identity, available);
	auto bottom = _layout.identityTop + st::walletCardNameFont->height;
	_layout.addressLines = AddressLines(_address, available);
	if (!_layout.addressLines.isEmpty()) {
		_layout.addressTop = bottom + gap;
		const auto addressFont
			= st::walletDetailsCollectionLabel.style.font->monospace();
		bottom = _layout.addressTop
			+ int(_layout.addressLines.size()) * addressFont->height;
	}
	const auto cardHeight = bottom + inset;
	_layout.card = QRect(border, border, cardWidth, cardHeight);
	return cardHeight + 2 * border;
}

void GramTransferCardPart::validateMark() const {
	const auto size = st::walletCardMarkSize;
	const auto color = st::activeButtonFg->c;
	const auto ratio = style::DevicePixelRatio();
	if (!_mark.isNull()
		&& _markColor == color
		&& _mark.size() == QSize(size, size) * ratio
		&& _mark.devicePixelRatio() == ratio) {
		return;
	}
	_markColor = color;
	_mark = Ui::Earn::IconCurrencyColored(size, color);
	_markTop = Ui::Earn::AlignedMarkTop(
		st::walletCardBalanceMajorLabel.style.font,
		_mark);
}

void GramTransferCardPart::validateBadge() const {
	const auto badge = Info::PeerGifts::GiftBadge{
		.text = _layout.badge,
		.bg1 = st::windowActiveTextFg->c,
		.fg = st::activeButtonFg->c,
	};
	const auto padding = st::chatUniqueGiftBadgePadding;
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	if (!_badge.isNull()
		&& _badgeKey == badge
		&& _badgePadding == padding
		&& _badgeFont == font
		&& _badge.devicePixelRatio() == style::DevicePixelRatio()) {
		return;
	}
	_badgeKey = badge;
	_badgePadding = padding;
	_badgeFont = font;
	_badge = Info::PeerGifts::ValidateRotatedBadge(badge, padding);
}

void GramTransferCardPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	validateMark();
	validateBadge();
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	const auto outer = QRect(0, 0, width(), height());
	const auto radius = st::msgServiceGiftBoxRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(outer, radius, radius);
	p.setClipPath(clip, Qt::IntersectClip);
	Wallet::PaintCardBackground(p, _layout.card);
	p.translate(_layout.card.topLeft());
	p.setPen(st::activeButtonFg);
	p.setFont(st::walletCardBalanceMajorLabel.style.font);
	p.save();
	p.translate(st::walletCardContentLeft, _layout.amountTop);
	p.scale(_layout.amountScale, _layout.amountScale);
	p.translate(0., _layout.amountShift);
	p.drawText(
		QPointF(0., st::walletCardBalanceMajorLabel.style.font->ascent),
		_amount);
	p.drawImage(
		QPointF(
			_layout.amountWidth + st::walletCardIconMargin.right(),
			_markTop),
		_mark);
	p.restore();
	p.setBrush(st::windowBgOver);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(
		_layout.info,
		st::walletCardInfoRadius,
		st::walletCardInfoRadius);
	p.translate(_layout.info.topLeft());
	if (_ripple) {
		_ripple->paint(
			p,
			0,
			0,
			_layout.info.width());
	}
	const auto &infoIcon = st::walletCardInfoIcon;
	infoIcon.paint(
		p,
		(_layout.info.width() - infoIcon.width()) / 2,
		(_layout.info.height() - infoIcon.height()) / 2,
		_layout.info.width());
	p.translate(-_layout.info.topLeft());
	p.setPen(st::activeButtonFg);
	p.setFont(st::walletCardNameFont);
	p.drawText(
		st::walletCardContentLeft,
		_layout.identityTop + st::walletCardNameFont->ascent,
		_layout.identity);
	const auto addressFont
		= st::walletDetailsCollectionLabel.style.font->monospace();
	p.setFont(addressFont);
	auto top = _layout.addressTop;
	for (const auto &line : _layout.addressLines) {
		p.drawText(st::walletCardContentLeft, top + addressFont->ascent, line);
		top += addressFont->height;
	}
	p.drawImage(
		QPointF(
			_layout.card.width() - _badge.width() / _badge.devicePixelRatio(),
			0.),
		_badge);
	p.restore();
}

TextState GramTransferCardPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	point -= _layout.card.topLeft() + _layout.info.topLeft();
	if (QRect(QPoint(), _layout.info.size()).contains(point)) {
		auto result = TextState();
		result.link = _infoLink;
		_lastPoint = point;
		return result;
	}
	return {};
}

void GramTransferCardPart::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	if (p != _infoLink) {
		return;
	} else if (pressed) {
		if (!_ripple) {
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(
					_layout.info.size(),
					st::walletCardInfoRadius),
				[view = _origin.view] {
					if (view) {
						view->repaint();
					}
				});
		}
		_ripple->add(_lastPoint);
	} else if (_ripple) {
		_ripple->lastStop();
	}
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
	if (item.commentEncrypted) {
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
			view->setPendingResize();
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
			if (weak) {
				weak->activate(context);
			}
			return false;
		});
	}
}

void GramTransferCommentPart::invalidate() {
	_retired = true;
	_comment->reset();
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
	const auto skip = st::walletCardContentSkip;
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

std::unique_ptr<Media> CreateGramTransferMedia(not_null<Element*> parent) {
	return std::make_unique<MediaGeneric>(
		parent,
		[parent](
				not_null<MediaGeneric*> media,
				Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
			const auto item = parent->data();
			const auto transfer = item->Get<HistoryServiceGramTransfer>();
			Assert(transfer != nullptr);
			const auto origin = GramTransferOrigin{
				.session = base::make_weak(&item->history()->session()),
				.view = base::make_weak(parent),
				.media = base::make_weak(media),
				.action = SnapshotGramTransfer(item),
			};
			push(std::make_unique<GramTransferCardPart>(origin));
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
