/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_giveaway.h"

#include "base/unixtime.h"
#include "boxes/gift_premium_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_dice_pack.h"
#include "countries/countries_instance.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
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

TextState MediaInBubble::Part::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	return {};
}

void MediaInBubble::Part::clickHandlerPressedChanged(
	const ClickHandlerPtr &p,
	bool pressed) {
}

bool MediaInBubble::Part::hasHeavyPart() {
	return false;
}

void MediaInBubble::Part::unloadHeavyPart() {
}

MediaInBubble::MediaInBubble(
	not_null<Element*> parent,
	Fn<void(Fn<void(std::unique_ptr<Part>)>)> generate)
: Media(parent) {
	generate([&](std::unique_ptr<Part> part) {
		_entries.push_back({
			.object = std::move(part),
		});
	});
}

MediaInBubble::~MediaInBubble() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

QSize MediaInBubble::countOptimalSize() {
	const auto maxWidth = st::chatGiveawayWidth;

	auto top = 0;
	for (auto &entry : _entries) {
		const auto raw = entry.object.get();
		raw->initDimensions();
		top += raw->resizeGetHeight(maxWidth);
	}
	return { maxWidth, top };
}

QSize MediaInBubble::countCurrentSize(int newWidth) {
	return { maxWidth(), minHeight()};
}

void MediaInBubble::draw(Painter &p, const PaintContext &context) const {
	const auto outer = width();
	if (outer < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	auto translated = 0;
	for (const auto &entry : _entries) {
		const auto raw = entry.object.get();
		const auto height = raw->height();
		raw->draw(p, context, outer);
		translated += height;
		p.translate(0, height);
	}
	p.translate(0, -translated);
}

TextState MediaInBubble::textState(
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

void MediaInBubble::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
}

void MediaInBubble::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (const auto &entry : _entries) {
		entry.object->clickHandlerPressedChanged(p, pressed);
	}
}

bool MediaInBubble::hideFromName() const {
	return !parent()->data()->Has<HistoryMessageForwarded>();
}

bool MediaInBubble::hasHeavyPart() const {
	for (const auto &entry : _entries) {
		if (entry.object->hasHeavyPart()) {
			return true;
		}
	}
	return false;
}

void MediaInBubble::unloadHeavyPart() {
	for (const auto &entry : _entries) {
		entry.object->unloadHeavyPart();
	}
}

QMargins MediaInBubble::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.top() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.bottom() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

TextMediaInBubblePart::TextMediaInBubblePart(
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

void TextMediaInBubblePart::draw(
		Painter &p,
		const PaintContext &context,
		int outerWidth) const {
	p.setPen(context.messageStyle()->historyTextFg);
	_text.draw(p, {
		.position = { (outerWidth - width()) / 2, _margins.top() },
		.outerWidth = outerWidth,
		.availableWidth = width(),
		.align = style::al_top,
		.palette = &context.messageStyle()->textPalette,
		.now = context.now,
	});
}

TextState TextMediaInBubblePart::textState(
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

QSize TextMediaInBubblePart::countOptimalSize() {
	return {
		_margins.left() + _text.maxWidth() + _margins.right(),
		_margins.top() + _text.minHeight() + _margins.bottom(),
	};
}

QSize TextMediaInBubblePart::countCurrentSize(int newWidth) {
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

StickerWithBadgePart::StickerWithBadgePart(
	not_null<Element*> parent,
	Fn<Data()> lookup,
	QString badge)
: _parent(parent)
, _lookup(std::move(lookup))
, _badgeText(badge) {
	ensureCreated();
}

void StickerWithBadgePart::draw(
		Painter &p,
		const PaintContext &context,
		int outerWidth) const {
	const auto stickerSize = st::msgServiceGiftBoxStickerSize;
	const auto sticker = QRect(
		(outerWidth - stickerSize.width()) / 2,
		st::chatGiveawayStickerTop + _skipTop,
		stickerSize.width(),
		stickerSize.height());

	if (_sticker) {
		_sticker->draw(p, context, sticker);
		paintBadge(p, context);
	} else {
		ensureCreated();
	}
}

bool StickerWithBadgePart::hasHeavyPart() {
	return _sticker && _sticker->hasHeavyPart();
}

void StickerWithBadgePart::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

QSize StickerWithBadgePart::countOptimalSize() {
	const auto size = st::msgServiceGiftBoxStickerSize;
	return { size.width(), st::chatGiveawayStickerTop + size.height() };
}

QSize StickerWithBadgePart::countCurrentSize(int newWidth) {
	return { newWidth, minHeight() };
}

void StickerWithBadgePart::ensureCreated() const {
	if (_sticker) {
		return;
	} else if (const auto data = _lookup()) {
		const auto document = data.sticker;
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = false;
			_skipTop = data.skipTop;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->setDiceIndex(sticker->alt, 1);
			_sticker->setGiftBoxSticker(data.isGiftBoxSticker);
			_sticker->initSize();
		}
	}
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

	if (!_parent->usesBubblePattern(context)) {
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

auto GenerateGiveawayStart(
	not_null<Element*> parent,
	not_null<Data::GiveawayStart*> data)
-> Fn<void(Fn<void(std::unique_ptr<MediaInBubble::Part>)>)> {
	return [=](Fn<void(std::unique_ptr<MediaInBubble::Part>)> push) {
		const auto months = data->months;
		const auto quantity = data->quantity;

		using Data = StickerWithBadgePart::Data;
		const auto sticker = [=] {
			const auto &session = parent->history()->session();
			auto &packs = session.giftBoxStickersPacks();
			return Data{ packs.lookup(months), 0, true };
		};
		push(std::make_unique<StickerWithBadgePart>(
			parent,
			sticker,
			tr::lng_prizes_badge(
				tr::now,
				lt_amount,
				QString::number(quantity))));

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<TextMediaInBubblePart>(
				std::move(text),
				margins,
				links));
		};
		pushText(
			Ui::Text::Bold(
				tr::lng_prizes_title(tr::now, lt_count, quantity)),
			st::chatGiveawayPrizesTitleMargin);

		if (!data->additionalPrize.isEmpty()) {
			pushText(
				tr::lng_prizes_additional(
					tr::now,
					lt_count,
					quantity,
					lt_prize,
					TextWithEntities{ data->additionalPrize },
					Ui::Text::RichLangValue),
				st::chatGiveawayPrizesMargin);
			push(std::make_unique<TextDelimeterPart>(
				tr::lng_prizes_additional_with(tr::now),
				st::chatGiveawayPrizesWithPadding));
		}

		pushText(
			tr::lng_prizes_about(
				tr::now,
				lt_count,
				quantity,
				lt_duration,
				Ui::Text::Bold(GiftDuration(months)),
				Ui::Text::RichLangValue),
			st::chatGiveawayPrizesMargin);
		pushText(
			Ui::Text::Bold(tr::lng_prizes_participants(tr::now)),
			st::chatGiveawayPrizesTitleMargin);

		const auto hasChannel = ranges::any_of(
			data->channels,
			&ChannelData::isBroadcast);
		const auto hasGroup = ranges::any_of(
			data->channels,
			&ChannelData::isMegagroup);
		const auto mixed = (hasChannel && hasGroup);
		pushText({ (data->all
			? (mixed
				? tr::lng_prizes_participants_all_mixed
				: hasGroup
				? tr::lng_prizes_participants_all_group
				: tr::lng_prizes_participants_all)
			: (mixed
				? tr::lng_prizes_participants_new_mixed
				: hasGroup
				? tr::lng_prizes_participants_new_group
				: tr::lng_prizes_participants_new))(
					tr::now,
					lt_count,
					data->channels.size()),
		}, st::chatGiveawayParticipantsMargin);

		auto list = ranges::views::all(
			data->channels
		) | ranges::views::transform([](not_null<ChannelData*> channel) {
			return not_null<PeerData*>(channel);
		}) | ranges::to_vector;
		push(std::make_unique<PeerBubbleListPart>(
			parent,
			std::move(list)));

		const auto &instance = Countries::Instance();
		auto countries = QStringList();
		for (const auto &country : data->countries) {
			const auto name = instance.countryNameByISO2(country);
			const auto flag = instance.flagEmojiByISO2(country);
			countries.push_back(flag + QChar(0xA0) + name);
		}
		if (const auto count = countries.size()) {
			auto united = countries.front();
			for (auto i = 1; i != count; ++i) {
				united = ((i + 1 == count)
					? tr::lng_prizes_countries_and_last
					: tr::lng_prizes_countries_and_one)(
						tr::now,
						lt_countries,
						united,
						lt_country,
						countries[i]);
			}
			pushText({
				tr::lng_prizes_countries(tr::now, lt_countries, united),
			}, st::chatGiveawayPrizesMargin);
		}

		pushText(
			Ui::Text::Bold(tr::lng_prizes_date(tr::now)),
			(countries.empty()
				? st::chatGiveawayNoCountriesTitleMargin
				: st::chatGiveawayPrizesMargin));
		pushText({
			langDateTime(base::unixtime::parse(data->untilDate)),
		}, st::chatGiveawayEndDateMargin);
	};
}

auto GenerateGiveawayResults(
	not_null<Element*> parent,
	not_null<Data::GiveawayResults*> data)
-> Fn<void(Fn<void(std::unique_ptr<MediaInBubble::Part>)>)> {
	return [=](Fn<void(std::unique_ptr<MediaInBubble::Part>)> push) {
		const auto quantity = data->winnersCount;

		using Data = StickerWithBadgePart::Data;
		const auto sticker = [=] {
			const auto &session = parent->history()->session();
			auto &packs = session.diceStickersPacks();
			const auto &emoji = Stickers::DicePacks::kPartyPopper;
			const auto skip = st::chatGiveawayWinnersTopSkip;
			return Data{ packs.lookup(emoji, 0), skip };
		};
		push(std::make_unique<StickerWithBadgePart>(
			parent,
			sticker,
			tr::lng_prizes_badge(
				tr::now,
				lt_amount,
				QString::number(quantity))));

		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			push(std::make_unique<TextMediaInBubblePart>(
				std::move(text),
				margins,
				links));
		};
		pushText(
			Ui::Text::Bold(
				tr::lng_prizes_results_title(tr::now)),
			st::chatGiveawayPrizesTitleMargin);
		const auto showGiveawayHandler = JumpToMessageClickHandler(
			data->channel,
			data->launchId,
			parent->data()->fullId());
		pushText(
			tr::lng_prizes_results_about(
				tr::now,
				lt_count,
				quantity,
				lt_link,
				Ui::Text::Link(tr::lng_prizes_results_link(tr::now)),
				Ui::Text::RichLangValue),
			st::chatGiveawayPrizesMargin,
			{ { 1, showGiveawayHandler } });
		pushText(
			Ui::Text::Bold(tr::lng_prizes_results_winners(tr::now)),
			st::chatGiveawayPrizesTitleMargin);

		push(std::make_unique<PeerBubbleListPart>(
			parent,
			data->winners));
		if (data->winnersCount > data->winners.size()) {
			pushText(
				Ui::Text::Bold(tr::lng_prizes_results_more(
					tr::now,
					lt_count,
					data->winnersCount - data->winners.size())),
				st::chatGiveawayNoCountriesTitleMargin);
		}
		pushText({ data->unclaimedCount
			? tr::lng_prizes_results_some(tr::now)
			: tr::lng_prizes_results_all(tr::now)
		}, st::chatGiveawayEndDateMargin);
	};
}

} // namespace HistoryView
