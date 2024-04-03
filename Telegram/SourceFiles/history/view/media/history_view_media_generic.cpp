/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_generic.h"

#include "data/data_document.h"
#include "data/data_peer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/tooltip.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kAdditionalPrizesWithLineOpacity = 0.6;

[[nodiscard]] QSize CountOptimalTextSize(
		const Ui::Text::String &text,
		int minWidth,
		int maxWidth) {
	if (text.maxWidth() <= maxWidth) {
		return { text.maxWidth(), text.minHeight() };
	}
	const auto height = text.countHeight(maxWidth);
	return { Ui::FindNiceTooltipWidth(minWidth, maxWidth, [&](int width) {
		return text.countHeight(width);
	}), height };
}

} // namespace

TextState MediaGenericPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	return {};
}

void MediaGenericPart::clickHandlerPressedChanged(
	const ClickHandlerPtr &p,
	bool pressed) {
}

bool MediaGenericPart::hasHeavyPart() {
	return false;
}

void MediaGenericPart::unloadHeavyPart() {
}

auto MediaGenericPart::stickerTakePlayer(
	not_null<DocumentData*> data,
	const Lottie::ColorReplacements *replacements
) -> std::unique_ptr<StickerPlayer> {
	return nullptr;
}

MediaGeneric::MediaGeneric(
	not_null<Element*> parent,
	Fn<void(Fn<void(std::unique_ptr<Part>)>)> generate,
	MediaGenericDescriptor &&descriptor)
: Media(parent)
, _maxWidthCap(descriptor.maxWidth)
, _service(descriptor.service)
, _hideServiceText(descriptor.hideServiceText) {
	generate([&](std::unique_ptr<Part> part) {
		_entries.push_back({
			.object = std::move(part),
		});
	});
	if (descriptor.serviceLink) {
		parent->data()->setCustomServiceLink(
			std::move(descriptor.serviceLink));
	}
}

MediaGeneric::~MediaGeneric() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

QSize MediaGeneric::countOptimalSize() {
	const auto maxWidth = _maxWidthCap
		? _maxWidthCap
		: st::chatGiveawayWidth;

	auto top = 0;
	for (auto &entry : _entries) {
		const auto raw = entry.object.get();
		raw->initDimensions();
		top += raw->resizeGetHeight(maxWidth);
	}
	return { maxWidth, top };
}

QSize MediaGeneric::countCurrentSize(int newWidth) {
	return { maxWidth(), minHeight() };
}

void MediaGeneric::draw(Painter &p, const PaintContext &context) const {
	const auto outer = width();
	if (outer < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	} else if (_service) {
		PainterHighQualityEnabler hq(p);
		const auto radius = st::msgServiceGiftBoxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(context.st->msgServiceBg());
		p.drawRoundedRect(QRect(0, 0, width(), height()), radius, radius);
	}

	auto translated = 0;
	for (const auto &entry : _entries) {
		const auto raw = entry.object.get();
		const auto height = raw->height();
		raw->draw(p, this, context, outer);
		translated += height;
		p.translate(0, height);
	}
	p.translate(0, -translated);
}

TextState MediaGeneric::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);

	const auto outer = width();
	if (outer < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	for (const auto &entry : _entries) {
		const auto raw = entry.object.get();
		const auto height = raw->height();
		if (point.y() >= 0 && point.y() < height) {
			const auto part = raw->textState(point, request, outer);
			result.link = part.link;
			return result;
		}
		point.setY(point.y() - height);
	}
	return result;
}

void MediaGeneric::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
}

void MediaGeneric::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (const auto &entry : _entries) {
		entry.object->clickHandlerPressedChanged(p, pressed);
	}
}

std::unique_ptr<StickerPlayer> MediaGeneric::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	for (const auto &entry : _entries) {
		if (auto result = entry.object->stickerTakePlayer(
				data,
				replacements)) {
			return result;
		}
	}
	return nullptr;
}

bool MediaGeneric::hideFromName() const {
	return !parent()->data()->Has<HistoryMessageForwarded>();
}

bool MediaGeneric::hideServiceText() const {
	return _hideServiceText;
}

bool MediaGeneric::hasHeavyPart() const {
	for (const auto &entry : _entries) {
		if (entry.object->hasHeavyPart()) {
			return true;
		}
	}
	return false;
}

void MediaGeneric::unloadHeavyPart() {
	for (const auto &entry : _entries) {
		entry.object->unloadHeavyPart();
	}
}

QMargins MediaGeneric::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom()
		? st::msgPadding.top()
		: st::mediaInBubbleSkip;
	auto tshift = isBubbleTop()
		? st::msgPadding.bottom()
		: st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

MediaGenericTextPart::MediaGenericTextPart(
	TextWithEntities text,
	QMargins margins,
	const base::flat_map<uint16, ClickHandlerPtr> &links)
: _text(st::msgMinWidth)
, _margins(margins) {
	_text.setMarkedText(st::defaultTextStyle, text);
	for (const auto &[index, link] : links) {
		_text.setLink(index, link);
	}
}

void MediaGenericTextPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto service = owner->service();
	p.setPen(service
		? context.st->msgServiceFg()
		: context.messageStyle()->historyTextFg);
	_text.draw(p, {
		.position = { (outerWidth - width()) / 2, _margins.top() },
		.outerWidth = outerWidth,
		.availableWidth = width(),
		.align = style::al_top,
		.palette = &(service
			? context.st->serviceTextPalette()
			: context.messageStyle()->textPalette),
		.now = context.now,
	});
}

TextState MediaGenericTextPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	point -= QPoint{ (outerWidth - width()) / 2, _margins.top() };
	auto result = TextState();
	auto forText = request.forText();
	forText.align = style::al_top;
	result.link = _text.getState(point, width(), forText).link;
	return result;
}

QSize MediaGenericTextPart::countOptimalSize() {
	return {
		_margins.left() + _text.maxWidth() + _margins.right(),
		_margins.top() + _text.minHeight() + _margins.bottom(),
	};
}

QSize MediaGenericTextPart::countCurrentSize(int newWidth) {
	auto skip = _margins.left() + _margins.right();
	const auto size = CountOptimalTextSize(
		_text,
		st::msgMinWidth,
		newWidth - skip);
	return {
		size.width() + skip,
		_margins.top() + size.height() + _margins.bottom(),
	};
}

TextDelimeterPart::TextDelimeterPart(
	const QString &text,
	QMargins margins)
: _margins(margins) {
	_text.setText(st::defaultTextStyle, text);
}

void TextDelimeterPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto stm = context.messageStyle();
	const auto available = outerWidth - _margins.left() - _margins.right();
	p.setPen(stm->msgDateFg);
	_text.draw(p, {
		.position = { _margins.left(), _margins.top() },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.align = style::al_top,
		.palette = &stm->textPalette,
		.now = context.now,
		.elisionLines = 1,
	});
	const auto skip = st::chatGiveawayPrizesWithSkip;
	const auto inner = available - 2 * skip;
	const auto sub = _text.maxWidth();
	if (inner > sub + 1) {
		const auto fill = (inner - sub) / 2;
		const auto stroke = st::lineWidth;
		const auto top = _margins.top()
			+ st::chatGiveawayPrizesWithLineTop;
		p.setOpacity(kAdditionalPrizesWithLineOpacity);
		p.fillRect(_margins.left(), top, fill, stroke, stm->msgDateFg);
		const auto start = outerWidth - _margins.right() - fill;
		p.fillRect(start, top, fill, stroke, stm->msgDateFg);
		p.setOpacity(1.);
	}
}

QSize TextDelimeterPart::countOptimalSize() {
	return {
		_margins.left() + _text.maxWidth() + _margins.right(),
		_margins.top() + st::normalFont->height + _margins.bottom(),
	};
}

QSize TextDelimeterPart::countCurrentSize(int newWidth) {
	return { newWidth, minHeight() };
}

StickerInBubblePart::StickerInBubblePart(
	not_null<Element*> parent,
	Element *replacing,
	Fn<Data()> lookup,
	QMargins padding)
: _parent(parent)
, _lookup(std::move(lookup))
, _padding(padding) {
	ensureCreated(replacing);
}

void StickerInBubblePart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	ensureCreated();
	if (_sticker) {
		const auto stickerSize = _sticker->countOptimalSize();
		const auto sticker = QRect(
			(outerWidth - stickerSize.width()) / 2,
			_padding.top() + _skipTop,
			stickerSize.width(),
			stickerSize.height());
		_sticker->draw(p, context, sticker);
	}
}

TextState StickerInBubblePart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	auto result = TextState(_parent);
	if (_sticker) {
		const auto stickerSize = _sticker->countOptimalSize();
		const auto sticker = QRect(
			(outerWidth - stickerSize.width()) / 2,
			_padding.top() + _skipTop,
			stickerSize.width(),
			stickerSize.height());
		if (sticker.contains(point)) {
			result.link = _link;
		}
	}
	return result;
}

bool StickerInBubblePart::hasHeavyPart() {
	return _sticker && _sticker->hasHeavyPart();
}

void StickerInBubblePart::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

std::unique_ptr<StickerPlayer> StickerInBubblePart::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker
		? _sticker->stickerTakePlayer(data, replacements)
		: nullptr;
}

QSize StickerInBubblePart::countOptimalSize() {
	ensureCreated();
	const auto size = _sticker ? _sticker->countOptimalSize() : [&] {
		const auto fallback = _lookup().size;
		return QSize{ fallback, fallback };
	}();
	return {
		_padding.left() + size.width() + _padding.right(),
		_padding.top() + size.height() + _padding.bottom(),
	};
}

QSize StickerInBubblePart::countCurrentSize(int newWidth) {
	return { newWidth, minHeight() };
}

void StickerInBubblePart::ensureCreated(Element *replacing) const {
	if (_sticker) {
		return;
	} else if (const auto data = _lookup()) {
		const auto sticker = data.sticker;
		if (const auto info = sticker->sticker()) {
			const auto skipPremiumEffect = true;
			_link = data.link;
			_skipTop = data.skipTop;
			_sticker.emplace(_parent, sticker, skipPremiumEffect, replacing);
			if (data.singleTimePlayback) {
				_sticker->setDiceIndex(info->alt, 1);
			}
			_sticker->initSize(data.size);
			_sticker->setCustomCachingTag(data.cacheTag);
		}
	}
}

StickerWithBadgePart::StickerWithBadgePart(
	not_null<Element*> parent,
	Element *replacing,
	Fn<Data()> lookup,
	QMargins padding,
	QString badge)
: _sticker(parent, replacing, std::move(lookup), padding)
, _badgeText(badge) {
}

void StickerWithBadgePart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	_sticker.draw(p, owner, context, outerWidth);
	if (_sticker.resolved()) {
		paintBadge(p, context);
	}
}

TextState StickerWithBadgePart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	return _sticker.textState(point, request, outerWidth);
}

bool StickerWithBadgePart::hasHeavyPart() {
	return _sticker.hasHeavyPart();
}

void StickerWithBadgePart::unloadHeavyPart() {
	_sticker.unloadHeavyPart();
}

std::unique_ptr<StickerPlayer> StickerWithBadgePart::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker.stickerTakePlayer(data, replacements);
}

QSize StickerWithBadgePart::countOptimalSize() {
	_sticker.initDimensions();
	return { _sticker.maxWidth(), _sticker.minHeight() };
}

QSize StickerWithBadgePart::countCurrentSize(int newWidth) {
	return _sticker.countCurrentSize(newWidth);
}

void StickerWithBadgePart::paintBadge(
		Painter &p,
		const PaintContext &context) const {
	validateBadge(context);

	const auto badge = _badge.size() / _badge.devicePixelRatio();
	const auto left = (width() - badge.width()) / 2;
	const auto top = st::chatGiveawayBadgeTop;
	const auto rect = QRect(left, top, badge.width(), badge.height());
	const auto paintContent = [&](QPainter &q) {
		q.drawImage(rect.topLeft(), _badge);
	};

	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(context.messageStyle()->msgFileBg);
		const auto half = st::chatGiveawayBadgeStroke / 2.;
		const auto inner = QRectF(rect).marginsRemoved(
			{ half, half, half, half });
		const auto radius = inner.height() / 2.;
		p.drawRoundedRect(inner, radius, radius);
	}

	if (!_sticker.parent()->usesBubblePattern(context)) {
		paintContent(p);
	} else {
		Ui::PaintPatternBubblePart(
			p,
			context.viewport,
			context.bubblesPattern->pixmap,
			rect,
			paintContent,
			_badgeCache);
	}
}

void StickerWithBadgePart::validateBadge(
		const PaintContext &context) const {
	const auto stm = context.messageStyle();
	const auto &badgeFg = stm->historyFileRadialFg->c;
	const auto &badgeBorder = stm->msgBg->c;
	if (!_badge.isNull()
		&& _badgeFg == badgeFg
		&& _badgeBorder == badgeBorder) {
		return;
	}
	const auto &font = st::chatGiveawayBadgeFont;
	_badgeFg = badgeFg;
	_badgeBorder = badgeBorder;
	const auto width = font->width(_badgeText);
	const auto inner = QRect(0, 0, width, font->height);
	const auto rect = inner.marginsAdded(st::chatGiveawayBadgePadding);
	const auto size = rect.size();
	const auto ratio = style::DevicePixelRatio();
	_badge = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	_badge.setDevicePixelRatio(ratio);
	_badge.fill(Qt::transparent);

	auto p = QPainter(&_badge);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(_badgeBorder, st::chatGiveawayBadgeStroke * 1.));
	p.setBrush(Qt::NoBrush);
	const auto half = st::chatGiveawayBadgeStroke / 2.;
	const auto smaller = QRectF(
		rect.translated(-rect.topLeft())
	).marginsRemoved({ half, half, half, half });
	const auto radius = smaller.height() / 2.;
	p.drawRoundedRect(smaller, radius, radius);
	p.setPen(_badgeFg);
	p.setFont(font);
	p.drawText(
		st::chatGiveawayBadgePadding.left(),
		st::chatGiveawayBadgePadding.top() + font->ascent,
		_badgeText);
}

PeerBubbleListPart::PeerBubbleListPart(
	not_null<Element*> parent,
	const std::vector<not_null<PeerData*>> &list)
: _parent(parent) {
	for (const auto &peer : list) {
		_peers.push_back({
			.name = Ui::Text::String(
				st::semiboldTextStyle,
				peer->name(),
				kDefaultTextOptions,
				st::msgMinWidth),
			.thumbnail = Ui::MakeUserpicThumbnail(peer),
			.link = peer->openLink(),
			.colorIndex = peer->colorIndex(),
		});
	}
}

PeerBubbleListPart::~PeerBubbleListPart() = default;

void PeerBubbleListPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	if (_peers.empty()) {
		return;
	}

	const auto size = _peers[0].geometry.height();
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto selected = context.selected();
	const auto padding = st::chatGiveawayPeerPadding;
	for (const auto &peer : _peers) {
		const auto &thumbnail = peer.thumbnail;
		const auto &geometry = peer.geometry;
		if (!_subscribed) {
			thumbnail->subscribeToUpdates([=] { _parent->repaint(); });
		}

		const auto colorIndex = peer.colorIndex;
		const auto cache = context.outbg
			? stm->replyCache[st->colorPatternIndex(colorIndex)].get()
			: st->coloredReplyCache(selected, colorIndex).get();
		if (peer.corners[0].isNull() || peer.bg != cache->bg) {
			peer.bg = cache->bg;
			peer.corners = Images::CornersMask(size / 2);
			for (auto &image : peer.corners) {
				style::colorizeImage(image, cache->bg, &image);
			}
		}
		p.setPen(cache->icon);
		Ui::DrawRoundedRect(p, geometry, peer.bg, peer.corners);
		if (peer.ripple) {
			peer.ripple->paint(
				p,
				geometry.x(),
				geometry.y(),
				width(),
				&cache->bg);
			if (peer.ripple->empty()) {
				peer.ripple = nullptr;
			}
		}

		p.drawImage(geometry.topLeft(), thumbnail->image(size));
		const auto left = size + padding.left();
		const auto top = padding.top();
		const auto available = geometry.width() - left - padding.right();
		peer.name.draw(p, {
			.position = { geometry.left() + left, geometry.top() + top },
			.outerWidth = width(),
			.availableWidth = available,
			.align = style::al_left,
			.palette = &stm->textPalette,
			.now = context.now,
			.elisionLines = 1,
			.elisionBreakEverywhere = true,
		});
	}
	_subscribed = true;
}

int PeerBubbleListPart::layout(int x, int y, int available) {
	const auto size = st::chatGiveawayPeerSize;
	const auto skip = st::chatGiveawayPeerSkip;
	const auto padding = st::chatGiveawayPeerPadding;
	auto left = available;
	const auto shiftRow = [&](int i, int top, int shift) {
		for (auto j = i; j != 0; --j) {
			auto &geometry = _peers[j - 1].geometry;
			if (geometry.top() != top) {
				break;
			}
			geometry.moveLeft(geometry.x() + shift);
		}
	};
	const auto count = int(_peers.size());
	for (auto i = 0; i != count; ++i) {
		const auto desired = size
			+ padding.left()
			+ _peers[i].name.maxWidth()
			+ padding.right();
		const auto width = std::min(desired, available);
		if (left < width) {
			shiftRow(i, y, (left + skip) / 2);
			left = available;
			y += size + skip;
		}
		_peers[i].geometry = { x + available - left, y, width, size };
		left -= width + skip;
	}
	shiftRow(count, y, (left + skip) / 2);
	return y + size + skip;
}

TextState PeerBubbleListPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	auto result = TextState(_parent);
	for (const auto &peer : _peers) {
		if (peer.geometry.contains(point)) {
			result.link = peer.link;
			_lastPoint = point;
			break;
		}
	}
	return result;
}

void PeerBubbleListPart::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (auto &peer : _peers) {
		if (peer.link != p) {
			continue;
		}
		if (pressed) {
			if (!peer.ripple) {
				peer.ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(
					peer.geometry.size(),
					peer.geometry.height() / 2),
					[=] { _parent->repaint(); });
			}
			peer.ripple->add(_lastPoint - peer.geometry.topLeft());
		} else if (peer.ripple) {
			peer.ripple->lastStop();
		}
		break;
	}
}

bool PeerBubbleListPart::hasHeavyPart() {
	return _subscribed;
}

void PeerBubbleListPart::unloadHeavyPart() {
	if (_subscribed) {
		_subscribed = false;
		for (const auto &peer : _peers) {
			peer.thumbnail->subscribeToUpdates(nullptr);
		}
	}
}

QSize PeerBubbleListPart::countOptimalSize() {
	if (_peers.empty()) {
		return {};
	}
	const auto size = st::chatGiveawayPeerSize;
	const auto skip = st::chatGiveawayPeerSkip;
	const auto padding = st::chatGiveawayPeerPadding;
	auto left = st::msgPadding.left();
	for (const auto &peer : _peers) {
		const auto desired = size
			+ padding.left()
			+ peer.name.maxWidth()
			+ padding.right();
		left += desired + skip;
	}
	return { left - skip + st::msgPadding.right(), size };
}

QSize PeerBubbleListPart::countCurrentSize(int newWidth) {
	if (_peers.empty()) {
		return {};
	}
	const auto padding = st::msgPadding;
	const auto available = newWidth - padding.left() - padding.right();
	const auto channelsBottom = layout(
		padding.left(),
		0,
		available);
	return { newWidth, channelsBottom };
}

} // namespace HistoryView
