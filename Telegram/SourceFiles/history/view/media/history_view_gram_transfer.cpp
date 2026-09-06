/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_gram_transfer.h"

#include "core/ui_integration.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/ton_common.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "wallet/wallet_address.h"

#include <QtCore/QLocale>

#include "styles/style_chat.h"
#include "styles/style_wallet.h"

namespace HistoryView {
namespace {

constexpr auto kAddressGroupSize = 4;
constexpr auto kAddressGroupsPerLine = 6;
constexpr auto kEncryptedBytesLimit = 1024;
constexpr auto kEncryptedEncodedLimit = 4 * ((kEncryptedBytesLimit + 2) / 3);
constexpr auto kEncryptedPayloadOverhead = 64;
constexpr auto kCoverMinLength = 8;
constexpr auto kCoverMaxLength = 128;

class GramTransferCardPart final : public MediaGenericPart {
public:
	explicit GramTransferCardPart(not_null<HistoryItem*> item);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	struct Layout {
		QRect card;
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
	void validateMark() const;
	void validateBadge() const;

	const QString _amount;
	const QString _address;
	const QString _identity;
	const QString _tag;
	Layout _layout;
	mutable QImage _mark;
	mutable QColor _markColor;
	mutable float64 _markTop = 0.;
	mutable QImage _badge;
	mutable Info::PeerGifts::GiftBadge _badgeKey;
	mutable QMargins _badgePadding;
	mutable style::font _badgeFont;

};

class GramTransferCommentPart final : public MediaGenericPart {
public:
	GramTransferCommentPart(
		not_null<Element*> parent,
		QString display,
		bool encrypted);

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

	Ui::Text::String _text;
	QRect _bubble;
	QRect _textRect;
	const bool _encrypted = false;

};

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

[[nodiscard]] QString EncryptedCover(const QString &encoded) {
	if (encoded.isEmpty()) {
		return {};
	}
	const auto length = [&] {
		const auto size = encoded.size();
		if (size > kEncryptedEncodedLimit || (size % 4)) {
			return kCoverMinLength;
		}
		const auto padding = encoded.endsWith(u"=="_q)
			? 2
			: encoded.endsWith(QChar('='))
			? 1
			: 0;
		for (auto i = 0; i != size - padding; ++i) {
			const auto ch = encoded[i].unicode();
			if (!((ch >= 'A' && ch <= 'Z')
				|| (ch >= 'a' && ch <= 'z')
				|| (ch >= '0' && ch <= '9')
				|| ch == '+'
				|| ch == '/')) {
				return kCoverMinLength;
			}
		}
		const auto bytes = encoded.toLatin1();
		const auto decoded = QByteArray::fromBase64Encoding(
			bytes,
			QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded
			|| decoded.decoded.isEmpty()
			|| decoded.decoded.size() > kEncryptedBytesLimit
			|| decoded.decoded.toBase64() != bytes) {
			return kCoverMinLength;
		}
		return std::clamp(
			int(decoded.decoded.size()) - kEncryptedPayloadOverhead,
			kCoverMinLength,
			kCoverMaxLength);
	}();
	const auto pattern = u"mora luma nera vera "_q;
	auto result = QString();
	result.reserve(length);
	for (auto i = 0; i != length; ++i) {
		const auto ch = pattern[i % pattern.size()];
		result += (i + 1 == length && ch == QChar(' ')) ? QChar('a') : ch;
	}
	return result;
}

GramTransferCardPart::GramTransferCardPart(not_null<HistoryItem*> item)
: _amount(SignedAmount(
	item->Get<HistoryServiceGramTransfer>()->amount,
	item->out()))
, _address(FriendlyAddress(
	item->Get<HistoryServiceGramTransfer>()->peerAddress))
, _identity(ReadableIdentity(item, !_address.isEmpty()))
, _tag((item->out()
	? tr::lng_action_gram_transfer_sent_tag
	: tr::lng_action_gram_transfer_received_tag)(tr::now)) {
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
	const auto cardWidth = std::max(outerWidth - 2 * border, 0);
	const auto available = std::max(cardWidth - 2 * inset, 1);
	const auto &badgeFont = st::msgServiceGiftBoxBadgeFont;
	const auto badgePadding = st::chatUniqueGiftBadgePadding;
	_layout.badge = badgeFont->elided(_tag, std::max(
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
	_layout.amountScale = std::min(1., available / float64(groupWidth));
	_layout.amountShift = std::max(-_markTop, 0.);
	const auto amountHeight = int(std::ceil(_layout.amountScale
		* (_layout.amountShift + std::max(
			float64(amountFont->height),
			_markTop + st::walletCardMarkSize))));
	_layout.identityTop = _layout.amountTop + amountHeight + gap;
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
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	p.drawRoundedRect(outer, radius, radius);
	p.setBrush(st::activeButtonBg);
	p.drawRoundedRect(
		_layout.card,
		st::walletCardRadius,
		st::walletCardRadius);
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

GramTransferCommentPart::GramTransferCommentPart(
	not_null<Element*> parent,
	QString display,
	bool encrypted)
: _text(0)
, _encrypted(encrypted) {
	auto marked = tr::marked(std::move(display));
	if (encrypted) {
		marked.entities.push_back({
			EntityType::Spoiler,
			0,
			int(marked.text.size()),
		});
	}
	_text.setMarkedText(
		st::serviceTextStyle,
		marked,
		kPlainTextOptions,
		Core::TextContext({
			.session = &parent->history()->session(),
			.repaint = [parent] { parent->repaint(); },
		}));
	if (_text.hasSpoilers()) {
		_text.setSpoilerRevealed(false, anim::type::instant);
		_text.setSpoilerLinkFilter([](const ClickContext &) { return false; });
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
		_bubble = _textRect = QRect();
		return 0;
	}
	const auto gap = st::walletCardContentSkip;
	const auto padding = QMargins(
		st::msgServicePadding.left(),
		gap,
		st::msgServicePadding.right(),
		gap);
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto textWidth = std::min(_text.maxWidth(), available);
	const auto textHeight = _text.countHeight(textWidth);
	const auto bubbleWidth = textWidth + padding.left() + padding.right();
	_bubble = QRect(
		(outerWidth - bubbleWidth) / 2,
		gap,
		bubbleWidth,
		textHeight + padding.top() + padding.bottom());
	_textRect = _bubble.marginsRemoved(padding);
	return gap + _bubble.height();
}

void GramTransferCommentPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	if (_bubble.isEmpty()) {
		return;
	}
	ServiceMessagePainter::PaintBubble(p, context.st, _bubble);
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
		.selection = _encrypted
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
	if (_encrypted || _textRect.isEmpty()) {
		return {};
	}
	auto textRequest = request.forText();
	textRequest.align = style::al_top;
	auto result = TextState(nullptr, _text.getState(
		point - _textRect.topLeft(),
		_textRect.width(),
		textRequest));
	result.link = nullptr;
	if (!_textRect.contains(point)) {
		result.cursor = CursorState::None;
	}
	result.overMessageText = (result.cursor == CursorState::Text);
	return result;
}

uint16 GramTransferCommentPart::fullSelectionLength() const {
	return _encrypted ? 0 : _text.length();
}

TextSelection GramTransferCommentPart::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return _encrypted
		? TextSelection()
		: (selection == FullSelection)
		? selection
		: _text.adjustSelection(selection, type);
}

TextForMimeData GramTransferCommentPart::selectedText(
		TextSelection selection) const {
	return _encrypted
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
			push(std::make_unique<GramTransferCardPart>(item));
			auto comment = transfer->commentEncrypted
				? EncryptedCover(transfer->comment)
				: transfer->commentText();
			if (!comment.isEmpty()) {
				push(std::make_unique<GramTransferCommentPart>(
					parent,
					std::move(comment),
					transfer->commentEncrypted));
			}
		},
		MediaGenericDescriptor{
			.maxWidth = st::chatUniqueGiftMaxWidth,
			.paintBgFactory = [] {
				return [](
						Painter &,
						const PaintContext &,
						not_null<const MediaGeneric*>) {};
			},
			.service = true,
			.hideServiceText = false,
		});
}

} // namespace HistoryView
