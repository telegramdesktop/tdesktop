/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_grouped.h"

#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "media/streaming/media_streaming_utility.h"
#include "ui/grouped_layout.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "layout/layout_selection.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

std::vector<Ui::GroupMediaLayout> LayoutPlaylist(
		const std::vector<QSize> &sizes) {
	Expects(!sizes.empty());

	auto result = std::vector<Ui::GroupMediaLayout>();
	result.reserve(sizes.size());
	const auto width = ranges::max_element(
		sizes,
		std::less<>(),
		&QSize::width)->width();
	auto top = 0;
	for (const auto &size : sizes) {
		result.push_back({
			.geometry = QRect(0, top, width, size.height()),
			.sides = RectPart::Left | RectPart::Right
		});
		top += size.height();
	}
	result.front().sides |= RectPart::Top;
	result.back().sides |= RectPart::Bottom;
	return result;
}

} // namespace

GroupedMedia::Part::Part(
	not_null<Element*> parent,
	not_null<Data::Media*> media)
: item(media->parent())
, content(media->createView(parent, item)) {
	Assert(media->canBeGrouped());
}

GroupedMedia::GroupedMedia(
	not_null<Element*> parent,
	const std::vector<std::unique_ptr<Data::Media>> &medias)
: Media(parent) {
	const auto truncated = ranges::views::all(
		medias
	) | ranges::views::transform([](const std::unique_ptr<Data::Media> &v) {
		return v.get();
	}) | ranges::views::take(kMaxSize);
	const auto result = applyGroup(truncated);

	Ensures(result);
}

GroupedMedia::GroupedMedia(
	not_null<Element*> parent,
	const std::vector<not_null<HistoryItem*>> &items)
: Media(parent) {
	const auto medias = ranges::views::all(
		items
	) | ranges::views::transform([](not_null<HistoryItem*> item) {
		return item->media();
	}) | ranges::views::take(kMaxSize);
	const auto result = applyGroup(medias);

	Ensures(result);
}

GroupedMedia::~GroupedMedia() {
	// Destroy all parts while the media object is still not destroyed.
	base::take(_parts);
}

HistoryItem *GroupedMedia::itemForText() const {
	if (_mode == Mode::Column) {
		return Media::itemForText();
	} else if (!_captionItem) {
		_captionItem = [&]() -> HistoryItem* {
			auto result = (HistoryItem*)nullptr;
			for (const auto &part : _parts) {
				if (!part.item->emptyText()) {
					if (result == part.item) {
						// All parts are from the same message, that means
						// this is an album with a single item, single text.
						return result;
					} else if (result) {
						return nullptr;
					} else {
						result = part.item;
					}
				}
			}
			return result;
		}();
	}
	return *_captionItem;
}

bool GroupedMedia::hideMessageText() const {
	return (_mode == Mode::Column);
}

GroupedMedia::Mode GroupedMedia::DetectMode(not_null<Data::Media*> media) {
	const auto document = media->document();
	return (document && !document->isVideoFile())
		? Mode::Column
		: Mode::Grid;
}

QSize GroupedMedia::countOptimalSize() {
	_purchasedPriceTag = hasPurchasedTag();

	std::vector<QSize> sizes;
	const auto partsCount = _parts.size();
	sizes.reserve(partsCount);
	auto maxWidth = 0;
	if (_mode == Mode::Column) {
		for (const auto &part : _parts) {
			const auto &media = part.content;
			media->setBubbleRounding(bubbleRounding());
			media->initDimensions();
			accumulate_max(maxWidth, media->maxWidth());
		}
	}
	auto index = 0;
	for (const auto &part : _parts) {
		const auto last = (++index == _parts.size());
		sizes.push_back(
			part.content->sizeForGroupingOptimal(maxWidth, last));
	}

	const auto layout = (_mode == Mode::Grid)
		? Ui::LayoutMediaGroup(
			sizes,
			st::historyGroupWidthMax,
			st::historyGroupWidthMin,
			st::historyGroupSkip)
		: LayoutPlaylist(sizes);
	Assert(layout.size() == _parts.size());

	auto minHeight = 0;
	for (auto i = 0, count = int(layout.size()); i != count; ++i) {
		const auto &item = layout[i];
		accumulate_max(maxWidth, item.geometry.x() + item.geometry.width());
		accumulate_max(minHeight, item.geometry.y() + item.geometry.height());
		_parts[i].initialGeometry = item.geometry;
		_parts[i].sides = item.sides;
	}

	if (_mode == Mode::Column && _parts.back().item->emptyText()) {
		const auto item = _parent->data();
		const auto msgsigned = item->Get<HistoryMessageSigned>();
		const auto views = item->Get<HistoryMessageViews>();
		if ((msgsigned && !msgsigned->isAnonymousRank)
			|| (views
				&& (views->views.count >= 0 || views->replies.count > 0))
			|| displayedEditBadge()) {
			minHeight += st::msgDateFont->height - st::msgDateDelta.y();
		}
	}

	const auto groupPadding = groupedPadding();
	minHeight += groupPadding.top() + groupPadding.bottom();

	return { maxWidth, minHeight };
}

QSize GroupedMedia::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto newHeight = 0;
	if (_mode == Mode::Grid && newWidth < st::historyGroupWidthMin) {
		return { newWidth, newHeight };
	} else if (_mode == Mode::Column) {
		auto top = 0;
		for (auto &part : _parts) {
			const auto size = part.content->sizeForGrouping(newWidth);
			part.geometry = QRect(0, top, newWidth, size.height());
			top += size.height();
		}
		newHeight = top;
	} else {
		const auto initialSpacing = st::historyGroupSkip;
		const auto factor = newWidth / float64(maxWidth());
		const auto scale = [&](int value) {
			return int(base::SafeRound(value * factor));
		};
		const auto spacing = scale(initialSpacing);
		for (auto &part : _parts) {
			const auto sides = part.sides;
			const auto initialGeometry = part.initialGeometry;
			const auto needRightSkip = !(sides & RectPart::Right);
			const auto needBottomSkip = !(sides & RectPart::Bottom);
			const auto initialLeft = initialGeometry.x();
			const auto initialTop = initialGeometry.y();
			const auto initialRight = initialLeft
				+ initialGeometry.width()
				+ (needRightSkip ? initialSpacing : 0);
			const auto initialBottom = initialTop
				+ initialGeometry.height()
				+ (needBottomSkip ? initialSpacing : 0);
			const auto left = scale(initialLeft);
			const auto top = scale(initialTop);
			const auto width = scale(initialRight)
				- left
				- (needRightSkip ? spacing : 0);
			const auto height = scale(initialBottom)
				- top
				- (needBottomSkip ? spacing : 0);
			part.geometry = QRect(left, top, width, height);

			accumulate_max(newHeight, top + height);
		}
	}
	if (_mode == Mode::Column && _parts.back().item->emptyText()) {
		const auto item = _parent->data();
		const auto msgsigned = item->Get<HistoryMessageSigned>();
		const auto views = item->Get<HistoryMessageViews>();
		if ((msgsigned && !msgsigned->isAnonymousRank)
			|| (views
				&& (views->views.count >= 0 || views->replies.count > 0))
			|| displayedEditBadge()) {
			newHeight += st::msgDateFont->height - st::msgDateDelta.y();
		}
	}

	const auto groupPadding = groupedPadding();
	newHeight += groupPadding.top() + groupPadding.bottom();

	return { newWidth, newHeight };
}

void GroupedMedia::refreshParentId(
		not_null<HistoryItem*> realParent) {
	for (const auto &part : _parts) {
		part.content->refreshParentId(part.item);
	}
}

Ui::BubbleRounding GroupedMedia::applyRoundingSides(
		Ui::BubbleRounding already,
		RectParts sides) const {
	auto result = Ui::GetCornersFromSides(sides);
	if (!(result & RectPart::TopLeft)) {
		already.topLeft = Ui::BubbleCornerRounding::None;
	}
	if (!(result & RectPart::TopRight)) {
		already.topRight = Ui::BubbleCornerRounding::None;
	}
	if (!(result & RectPart::BottomLeft)) {
		already.bottomLeft = Ui::BubbleCornerRounding::None;
	}
	if (!(result & RectPart::BottomRight)) {
		already.bottomRight = Ui::BubbleCornerRounding::None;
	}
	return already;
}

QMargins GroupedMedia::groupedPadding() const {
	if (_mode != Mode::Column) {
		return QMargins();
	}
	const auto normal = st::msgFileLayout.padding;
	const auto grouped = st::msgFileLayoutGrouped.padding;
	const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto lastHasCaption = isBubbleBottom()
		&& !_parts.back().item->emptyText();
	const auto addToBottom = lastHasCaption ? st::msgPadding.bottom() : 0;
	return QMargins(
		0,
		(normal.top() - grouped.top()) - topMinus,
		0,
		(normal.bottom() - grouped.bottom()) + addToBottom);
}

Media *GroupedMedia::lookupSpoilerTagMedia() const {
	if (_parts.empty()) {
		return nullptr;
	}
	const auto media = _parts.front().content.get();
	if (media && _parts.front().item->isMediaSensitive()) {
		return media;
	}
	const auto photo = media ? media->getPhoto() : nullptr;
	return (photo && photo->extendedMediaPreview()) ? media : nullptr;
}

QImage GroupedMedia::generateSpoilerTagBackground(QRect full) const {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		full.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	auto p = QPainter(&result);
	const auto shift = -full.topLeft();
	const auto skip1 = st::historyGroupSkip / 2;
	const auto skip2 = st::historyGroupSkip - skip1;
	for (const auto &part : _parts) {
		auto background = part.content->spoilerTagBackground();
		const auto extended = part.geometry.translated(shift).marginsAdded(
			{ skip1, skip1, skip2, skip2 });
		if (background.isNull()) {
			p.fillRect(extended, Qt::black);
		} else {
			p.drawImage(extended, background);
		}
	}
	p.end();

	return ::Media::Streaming::PrepareBlurredBackground(
		full.size(),
		std::move(result));
}

void GroupedMedia::drawHighlight(
		Painter &p,
		const PaintContext &context,
		int top) const {
	if (context.highlight.opacity == 0.) {
		return;
	}
	auto selection = context.highlight.range;
	if (_mode != Mode::Column) {
		if (!selection.empty() && !IsSubGroupSelection(selection)) {
			_parent->paintCustomHighlight(
				p,
				context,
				top,
				height(),
				_parent->data().get());
		}
		return;
	}
	const auto empty = selection.empty();
	const auto subpart = IsSubGroupSelection(selection);
	const auto skip = top + groupedPadding().top();
	for (auto i = 0, count = int(_parts.size()); i != count; ++i) {
		const auto &part = _parts[i];
		const auto rect = part.geometry.translated(0, skip);
		const auto full = (!i && empty)
			|| (subpart && IsGroupItemSelection(selection, i))
			|| (!subpart
				&& !selection.empty()
				&& (selection.from < part.content->fullSelectionLength()));
		if (!subpart) {
			selection = part.content->skipSelection(selection);
		}
		if (full) {
			auto copy = context;
			copy.highlight.range = {};
			_parent->paintCustomHighlight(
				p,
				copy,
				rect.y(),
				rect.height(),
				part.item);
		}
	}
}

void GroupedMedia::draw(Painter &p, const PaintContext &context) const {
	auto wasCache = false;
	auto nowCache = false;
	const auto groupPadding = groupedPadding();
	auto selection = context.selection;
	const auto fullSelection = (selection == FullSelection);
	const auto textSelection = (_mode == Mode::Column)
		&& !fullSelection
		&& !IsSubGroupSelection(selection);
	const auto inWebPage = (_parent->media() != this);
	constexpr auto kSmall = Ui::BubbleCornerRounding::Small;
	const auto rounding = inWebPage
		? Ui::BubbleRounding{ kSmall, kSmall, kSmall, kSmall }
		: adjustedBubbleRounding();
	auto highlight = context.highlight.range;
	const auto tagged = lookupSpoilerTagMedia();
	auto fullRect = QRect();
	const auto subpartHighlight = IsSubGroupSelection(highlight);
	for (auto i = 0, count = int(_parts.size()); i != count; ++i) {
		const auto &part = _parts[i];
		auto partContext = context.withSelection(fullSelection
			? FullSelection
			: textSelection
			? selection
			: IsGroupItemSelection(selection, i)
			? FullSelection
			: TextSelection());
		const auto highlighted = (highlight.empty() && !i)
			|| IsGroupItemSelection(highlight, i);
		const auto highlightOpacity = highlighted
			? context.highlight.opacity
			: 0.;
		partContext.highlight.range = highlighted
			? TextSelection()
			: highlight;
		if (textSelection) {
			selection = part.content->skipSelection(selection);
		}
		if (!subpartHighlight) {
			highlight = part.content->skipSelection(highlight);
		}
		if (!part.cache.isNull()) {
			wasCache = true;
		}
		part.content->drawGrouped(
			p,
			partContext,
			part.geometry.translated(0, groupPadding.top()),
			part.sides,
			applyRoundingSides(rounding, part.sides),
			highlightOpacity,
			&part.cacheKey,
			&part.cache);
		if (!part.cache.isNull()) {
			nowCache = true;
		}
		if (tagged || _purchasedPriceTag) {
			fullRect = fullRect.united(part.geometry);
		}
	}
	if (nowCache && !wasCache) {
		history()->owner().registerHeavyViewPart(_parent);
	}

	if (tagged) {
		tagged->drawSpoilerTag(p, fullRect, context, [&] {
			return generateSpoilerTagBackground(fullRect);
		});
	} else if (_purchasedPriceTag) {
		drawPurchasedTag(p, fullRect, context);
	}

	// date
	if (_parent->media() == this && (!_parent->hasBubble() || isBubbleBottom())) {
		auto fullRight = width();
		auto fullBottom = height();
		if (needInfoDisplay()) {
			_parent->drawInfo(
				p,
				context,
				fullRight,
				fullBottom,
				width(),
				InfoDisplayType::Image);
		}
		if (const auto size = _parent->hasBubble() ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = _parent->hasRightLayout()
				? (-size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			_parent->drawRightAction(p, context, fastShareLeft, fastShareTop, width());
		}
	}
}

TextState GroupedMedia::getPartState(
		QPoint point,
		StateRequest request) const {
	auto shift = 0;
	for (const auto &part : _parts) {
		if (part.geometry.contains(point)) {
			auto result = part.content->getStateGrouped(
				part.geometry,
				part.sides,
				point,
				request);
			result.symbol += shift;
			result.itemId = part.item->fullId();
			return result;
		}
		shift += part.content->fullSelectionLength();
	}
	return TextState(_parent->data());
}

PointState GroupedMedia::pointState(QPoint point) const {
	if (!QRect(0, 0, width(), height()).contains(point)) {
		return PointState::Outside;
	}
	const auto groupPadding = groupedPadding();
	point -= QPoint(0, groupPadding.top());
	for (const auto &part : _parts) {
		if (part.geometry.contains(point)) {
			return PointState::GroupPart;
		}
	}
	return PointState::Inside;
}

TextState GroupedMedia::textState(QPoint point, StateRequest request) const {
	const auto groupPadding = groupedPadding();
	auto result = getPartState(point - QPoint(0, groupPadding.top()), request);
	if (const auto tagged = lookupSpoilerTagMedia()) {
		if (QRect(0, 0, width(), height()).contains(point)) {
			if (auto link = tagged->spoilerTagLink()) {
				result.link = std::move(link);
			}
		}
	}
	if (_parent->media() == this && (!_parent->hasBubble() || isBubbleBottom())) {
		auto fullRight = width();
		auto fullBottom = height();
		const auto bottomInfoResult = _parent->bottomInfoTextState(
			fullRight,
			fullBottom,
			point,
			InfoDisplayType::Image);
		if (bottomInfoResult.link
			|| bottomInfoResult.cursor != CursorState::None
			|| bottomInfoResult.customTooltip) {
			return bottomInfoResult;
		}
		if (const auto size = _parent->hasBubble() ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = _parent->hasRightLayout()
				? (-size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			if (QRect(fastShareLeft, fastShareTop, size->width(), size->height()).contains(point)) {
				result.link = _parent->rightActionLink(point
					- QPoint(fastShareLeft, fastShareTop));
			}
		}
	}
	return result;
}

bool GroupedMedia::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	for (const auto &part : _parts) {
		if (part.content->toggleSelectionByHandlerClick(p)) {
			return true;
		}
	}
	return false;
}

bool GroupedMedia::dragItemByHandler(const ClickHandlerPtr &p) const {
	for (const auto &part : _parts) {
		if (part.content->dragItemByHandler(p)) {
			return true;
		}
	}
	return false;
}

TextSelection GroupedMedia::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	if (_mode != Mode::Column) {
		return {};
	}
	auto checked = 0;
	for (const auto &part : _parts) {
		const auto modified = ShiftItemSelection(
			part.content->adjustSelection(
				UnshiftItemSelection(selection, checked),
				type),
			checked);
		const auto till = checked + part.content->fullSelectionLength();
		if (selection.from >= checked && selection.from < till) {
			selection.from = modified.from;
		}
		if (selection.to <= till) {
			selection.to = modified.to;
			return selection;
		}
		checked = till;
	}
	return selection;
}

uint16 GroupedMedia::fullSelectionLength() const {
	if (_mode != Mode::Column) {
		return {};
	}
	auto result = 0;
	for (const auto &part : _parts) {
		result += part.content->fullSelectionLength();
	}
	return result;
}

bool GroupedMedia::hasTextForCopy() const {
	if (_mode != Mode::Column) {
		return {};
	}
	for (const auto &part : _parts) {
		if (part.content->hasTextForCopy()) {
			return true;
		}
	}
	return false;
}

TextForMimeData GroupedMedia::selectedText(
		TextSelection selection) const {
	if (_mode != Mode::Column) {
		return {};
	}
	auto result = TextForMimeData();
	for (const auto &part : _parts) {
		auto text = part.content->selectedText(selection);
		if (!text.empty()) {
			if (result.empty()) {
				result = std::move(text);
			} else {
				result.append(u"\n\n"_q).append(std::move(text));
			}
		}
		selection = part.content->skipSelection(selection);
	}
	return result;
}

SelectedQuote GroupedMedia::selectedQuote(TextSelection selection) const {
	if (_mode != Mode::Column) {
		return {};
	}
	for (const auto &part : _parts) {
		const auto next = part.content->skipSelection(selection);
		if (next.to - next.from != selection.to - selection.from) {
			if (!next.empty()) {
				return SelectedQuote();
			}
			auto result = part.content->selectedQuote(selection);
			result.item = part.item;
			return result;
		}
		selection = next;
	}
	return {};
}

TextSelection GroupedMedia::selectionFromQuote(
		const SelectedQuote &quote) const {
	Expects(quote.item != nullptr);

	if (_mode != Mode::Column) {
		return {};
	}
	const auto i = ranges::find(_parts, not_null(quote.item), &Part::item);
	if (i == end(_parts)) {
		return {};
	}
	const auto index = int(i - begin(_parts));
	auto result = i->content->selectionFromQuote(quote);
	if (result.empty()) {
		return AddGroupItemSelection({}, index);
	}
	for (auto j = i; j != begin(_parts);) {
		result = (--j)->content->unskipSelection(result);
	}
	return result;
}

auto GroupedMedia::getBubbleSelectionIntervals(
	TextSelection selection) const
-> std::vector<Ui::BubbleSelectionInterval> {
	if (_mode != Mode::Column) {
		return {};
	}
	auto result = std::vector<Ui::BubbleSelectionInterval>();
	for (auto i = 0, count = int(_parts.size()); i != count; ++i) {
		const auto &part = _parts[i];
		if (!IsGroupItemSelection(selection, i)) {
			continue;
		}
		const auto &geometry = part.geometry;
		if (result.empty()
			|| (result.back().top + result.back().height
				< geometry.top())
			|| (result.back().top > geometry.top() + geometry.height())) {
			result.push_back({ geometry.top(), geometry.height() });
		} else {
			auto &last = result.back();
			const auto newTop = std::min(last.top, geometry.top());
			const auto newHeight = std::max(
				last.top + last.height - newTop,
				geometry.top() + geometry.height() - newTop);
			last = Ui::BubbleSelectionInterval{ newTop, newHeight };
		}
	}
	const auto groupPadding = groupedPadding();
	for (auto &part : result) {
		part.top += groupPadding.top();
	}
	if (IsGroupItemSelection(selection, 0)) {
		result.front().top -= groupPadding.top();
		result.front().height += groupPadding.top();
	}
	if (IsGroupItemSelection(selection, _parts.size() - 1)) {
		result.back().height = height() - result.back().top;
	}
	return result;
}

void GroupedMedia::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	for (const auto &part : _parts) {
		part.content->clickHandlerActiveChanged(p, active);
	}
}

void GroupedMedia::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (const auto &part : _parts) {
		part.content->clickHandlerPressedChanged(p, pressed);
		if (pressed && part.content->dragItemByHandler(p)) {
			// #TODO drag by item from album
			// App::pressedLinkItem(part.view);
		}
	}
}

template <typename DataMediaRange>
bool GroupedMedia::applyGroup(const DataMediaRange &medias) {
	if (validateGroupParts(medias)) {
		return true;
	}

	auto modeChosen = false;
	for (const auto media : medias) {
		const auto mediaMode = DetectMode(media);
		if (!modeChosen) {
			_mode = mediaMode;
			modeChosen = true;
		} else if (mediaMode != _mode) {
			continue;
		}
		_parts.push_back(Part(_parent, media));
	}
	if (_parts.empty()) {
		return false;
	}

	Ensures(_parts.size() <= kMaxSize);
	return true;
}

template <typename DataMediaRange>
bool GroupedMedia::validateGroupParts(
		const DataMediaRange &medias) const {
	auto i = 0;
	const auto count = _parts.size();
	for (const auto media : medias) {
		if (i >= count || _parts[i].item != media->parent()) {
			return false;
		}
		++i;
	}
	return (i == count);
}

not_null<Media*> GroupedMedia::main() const {
	Expects(!_parts.empty());

	return _parts.back().content.get();
}

void GroupedMedia::hideSpoilers() {
	for (const auto &part : _parts) {
		part.content->hideSpoilers();
	}
}

Storage::SharedMediaTypesMask GroupedMedia::sharedMediaTypes() const {
	return main()->sharedMediaTypes();
}

PhotoData *GroupedMedia::getPhoto() const {
	return main()->getPhoto();
}

DocumentData *GroupedMedia::getDocument() const {
	return main()->getDocument();
}

HistoryMessageEdited *GroupedMedia::displayedEditBadge() const {
	for (const auto &part : _parts) {
		if (!part.item->hideEditedBadge()) {
			if (const auto edited = part.item->Get<HistoryMessageEdited>()) {
				return edited;
			}
		}
	}
	return nullptr;
}

void GroupedMedia::updateNeedBubbleState() {
	_needBubble = computeNeedBubble();
}

void GroupedMedia::stopAnimation() {
	for (const auto &part : _parts) {
		part.content->stopAnimation();
	}
}

void GroupedMedia::checkAnimation() {
	for (const auto &part : _parts) {
		part.content->checkAnimation();
	}
}

bool GroupedMedia::hasHeavyPart() const {
	for (const auto &part : _parts) {
		if (!part.cache.isNull() || part.content->hasHeavyPart()) {
			return true;
		}
	}
	return false;
}

void GroupedMedia::unloadHeavyPart() {
	for (const auto &part : _parts) {
		part.content->unloadHeavyPart();
		part.cacheKey = 0;
		part.cache = QPixmap();
	}
}

void GroupedMedia::parentTextUpdated() {
	if (_parent->media() == this) {
		if (_mode == Mode::Column) {
			for (const auto &part : _parts) {
				part.content->parentTextUpdated();
			}
		} else {
			_captionItem = std::nullopt;
		}
	}
}

bool GroupedMedia::needsBubble() const {
	return _needBubble;
}

QPoint GroupedMedia::resolveCustomInfoRightBottom() const {
	const auto skipx = (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto skipy = (st::msgDateImgDelta + st::msgDateImgPadding.y());
	return QPoint(width() - skipx, height() - skipy);
}

bool GroupedMedia::enforceBubbleWidth() const {
	return _mode == Mode::Grid;
}

bool GroupedMedia::computeNeedBubble() const {
	Expects(_mode == Mode::Column || _captionItem.has_value());

	if (_mode == Mode::Column || *_captionItem) {
		return true;
	}
	if (const auto item = _parent->data()) {
		if (item->repliesAreComments()
			|| item->externalReply()
			|| item->viaBot()
			|| _parent->displayReply()
			|| _parent->displayForwardedFrom()
			|| _parent->displayFromName()
			|| _parent->displayedTopicButton()
			) {
			return true;
		}
	}
	return false;
}

bool GroupedMedia::needInfoDisplay() const {
	return (_mode != Mode::Column)
		&& (_parent->data()->isSending()
			|| _parent->data()->hasFailed()
			|| _parent->isUnderCursor()
			|| (_parent->delegate()->elementContext() == Context::ChatPreview)
			|| _parent->isLastAndSelfMessage());
}

} // namespace HistoryView
