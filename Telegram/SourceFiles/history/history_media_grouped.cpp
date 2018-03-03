/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_media_grouped.h"

#include "history/history_item_components.h"
#include "history/history_media_types.h"
#include "history/history_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_media_types.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "ui/grouped_layout.h"
#include "ui/text_options.h"
#include "styles/style_history.h"
#include "layout.h"

namespace {

using TextState = HistoryView::TextState;
using PointState = HistoryView::PointState;

constexpr auto kMaxDisplayedGroupSize = 10;

} // namespace

HistoryGroupedMedia::Part::Part(not_null<HistoryItem*> item)
: item(item) {
}

HistoryGroupedMedia::HistoryGroupedMedia(
	not_null<Element*> parent,
	const std::vector<not_null<HistoryItem*>> &items)
: HistoryMedia(parent)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto result = (items.size() <= kMaxDisplayedGroupSize)
		? applyGroup(items)
		: applyGroup(std::vector<not_null<HistoryItem*>>(
			begin(items),
			begin(items) + kMaxDisplayedGroupSize));

	Ensures(result);
}

QSize HistoryGroupedMedia::countOptimalSize() {
	if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	std::vector<QSize> sizes;
	sizes.reserve(_parts.size());
	for (const auto &part : _parts) {
		const auto &media = part.content;
		media->initDimensions();
		sizes.push_back(media->sizeForGrouping());
	}

	const auto layout = Ui::LayoutMediaGroup(
		sizes,
		st::historyGroupWidthMax,
		st::historyGroupWidthMin,
		st::historyGroupSkip);
	Assert(layout.size() == _parts.size());

	auto maxWidth = 0;
	auto minHeight = 0;
	for (auto i = 0, count = int(layout.size()); i != count; ++i) {
		const auto &item = layout[i];
		accumulate_max(maxWidth, item.geometry.x() + item.geometry.width());
		accumulate_max(minHeight, item.geometry.y() + item.geometry.height());
		_parts[i].initialGeometry = item.geometry;
		_parts[i].sides = item.sides;
	}

	if (!_caption.isEmpty()) {
		auto captionw = maxWidth - st::msgPadding.left() - st::msgPadding.right();
		minHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize HistoryGroupedMedia::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto newHeight = 0;
	if (newWidth < st::historyGroupWidthMin) {
		return { newWidth, newHeight };
	}

	const auto initialSpacing = st::historyGroupSkip;
	const auto factor = newWidth / float64(maxWidth());
	const auto scale = [&](int value) {
		return int(std::round(value * factor));
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

	if (!_caption.isEmpty()) {
		const auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
		newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}

	return { newWidth, newHeight };
}

void HistoryGroupedMedia::refreshParentId(
		not_null<HistoryItem*> realParent) {
	for (const auto &part : _parts) {
		part.content->refreshParentId(part.item);
	}
}

void HistoryGroupedMedia::draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const {
	for (auto i = 0, count = int(_parts.size()); i != count; ++i) {
		const auto &part = _parts[i];
		const auto partSelection = (selection == FullSelection)
			? FullSelection
			: IsGroupItemSelection(selection, i)
			? FullSelection
			: TextSelection();
		auto corners = Ui::GetCornersFromSides(part.sides);
		if (!isBubbleTop()) {
			corners &= ~(RectPart::TopLeft | RectPart::TopRight);
		}
		if (!isBubbleBottom() || !_caption.isEmpty()) {
			corners &= ~(RectPart::BottomLeft | RectPart::BottomRight);
		}
		part.content->drawGrouped(
			p,
			clip,
			partSelection,
			ms,
			part.geometry,
			corners,
			&part.cacheKey,
			&part.cache);
	}

	// date
	const auto selected = (selection == FullSelection);
	if (!_caption.isEmpty()) {
		const auto captionw = width() - st::msgPadding.left() - st::msgPadding.right();
		const auto outbg = _parent->hasOutLayout();
		const auto captiony = height()
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), captiony, captionw, style::al_left, 0, -1, selection);
	} else if (_parent->media() == this) {
		auto fullRight = width();
		auto fullBottom = height();
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, width(), selected, InfoDisplayType::Image);
		}
		if (!_parent->hasBubble() && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, width());
		}
	}
}

TextState HistoryGroupedMedia::getPartState(
		QPoint point,
		StateRequest request) const {
	for (const auto &part : _parts) {
		if (part.geometry.contains(point)) {
			auto result = part.content->getStateGrouped(
				part.geometry,
				point,
				request);
			result.itemId = part.item->fullId();
			return result;
		}
	}
	return TextState(_parent->data());
}

PointState HistoryGroupedMedia::pointState(QPoint point) const {
	if (!QRect(0, 0, width(), height()).contains(point)) {
		return PointState::Outside;
	}
	for (const auto &part : _parts) {
		if (part.geometry.contains(point)) {
			return PointState::GroupPart;
		}
	}
	return PointState::Inside;
}

HistoryView::TextState HistoryGroupedMedia::textState(
		QPoint point,
		StateRequest request) const {
	auto result = getPartState(point, request);
	if (!result.link && !_caption.isEmpty()) {
		const auto captionw = width() - st::msgPadding.left() - st::msgPadding.right();
		const auto captiony = height()
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		if (QRect(st::msgPadding.left(), captiony, captionw, height() - captiony).contains(point)) {
			return TextState(_parent->data(), _caption.getState(
				point - QPoint(st::msgPadding.left(), captiony),
				captionw,
				request.forText()));
		}
	} else if (_parent->media() == this) {
		auto fullRight = width();
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Image)) {
			result.cursor = CursorState::Date;
		}
		if (!_parent->hasBubble() && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	return result;
}

bool HistoryGroupedMedia::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	for (const auto &part : _parts) {
		if (part.content->toggleSelectionByHandlerClick(p)) {
			return true;
		}
	}
	return false;
}

bool HistoryGroupedMedia::dragItemByHandler(const ClickHandlerPtr &p) const {
	for (const auto &part : _parts) {
		if (part.content->dragItemByHandler(p)) {
			return true;
		}
	}
	return false;
}

TextSelection HistoryGroupedMedia::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return _caption.adjustSelection(selection, type);
}

TextWithEntities HistoryGroupedMedia::selectedText(
		TextSelection selection) const {
	return _caption.originalTextWithEntities(selection, ExpandLinksAll);
}

void HistoryGroupedMedia::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	for (const auto &part : _parts) {
		part.content->clickHandlerActiveChanged(p, active);
	}
}

void HistoryGroupedMedia::clickHandlerPressedChanged(
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

bool HistoryGroupedMedia::applyGroup(
		const std::vector<not_null<HistoryItem*>> &items) {
	Expects(items.size() <= kMaxDisplayedGroupSize);

	if (items.empty()) {
		return false;
	}
	if (validateGroupParts(items)) {
		return true;
	}

	for (const auto item : items) {
		const auto media = item->media();
		Assert(media != nullptr && media->canBeGrouped());

		_parts.push_back(Part(item));
		_parts.back().content = media->createView(_parent, item);
	};
	return true;
}

bool HistoryGroupedMedia::validateGroupParts(
		const std::vector<not_null<HistoryItem*>> &items) const {
	if (_parts.size() != items.size()) {
		return false;
	}
	for (auto i = 0, count = int(items.size()); i != count; ++i) {
		if (_parts[i].item != items[i]) {
			return false;
		}
	}
	return true;
}

not_null<HistoryMedia*> HistoryGroupedMedia::main() const {
	Expects(!_parts.empty());

	return _parts.back().content.get();
}

TextWithEntities HistoryGroupedMedia::getCaption() const {
	return main()->getCaption();
}

Storage::SharedMediaTypesMask HistoryGroupedMedia::sharedMediaTypes() const {
	return main()->sharedMediaTypes();
}

PhotoData *HistoryGroupedMedia::getPhoto() const {
	return main()->getPhoto();
}

DocumentData *HistoryGroupedMedia::getDocument() const {
	return main()->getDocument();
}

HistoryMessageEdited *HistoryGroupedMedia::displayedEditBadge() const {
	if (!_caption.isEmpty()) {
		return _parts.front().item->Get<HistoryMessageEdited>();
	}
	return nullptr;
}

void HistoryGroupedMedia::updateNeedBubbleState() {
	const auto hasCaption = [&] {
		if (_parts.front().item->emptyText()) {
			return false;
		}
		for (auto i = 1, count = int(_parts.size()); i != count; ++i) {
			if (!_parts[i].item->emptyText()) {
				return false;
			}
		}
		return true;
	}();
	if (hasCaption) {
		_caption = createCaption(_parts.front().item);
	}
	_needBubble = computeNeedBubble();
}

bool HistoryGroupedMedia::needsBubble() const {
	return _needBubble;
}

bool HistoryGroupedMedia::computeNeedBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	if (const auto item = _parent->data()) {
		if (item->viaBot()
			|| item->Has<HistoryMessageReply>()
			|| _parent->displayForwardedFrom()
			|| _parent->displayFromName()
			) {
			return true;
		}
	}
	return false;
}

bool HistoryGroupedMedia::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
}
