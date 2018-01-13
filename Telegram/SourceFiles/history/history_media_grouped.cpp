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
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "ui/grouped_layout.h"
#include "ui/text_options.h"
#include "styles/style_history.h"
#include "layout.h"

HistoryGroupedMedia::Element::Element(not_null<HistoryItem*> item)
: item(item) {
}

HistoryGroupedMedia::HistoryGroupedMedia(
	not_null<HistoryItem*> parent,
	const std::vector<not_null<HistoryItem*>> &others)
: HistoryMedia(parent)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto result = applyGroup(others);

	Ensures(result);
}

std::unique_ptr<HistoryMedia> HistoryGroupedMedia::clone(
		not_null<HistoryItem*> newParent,
		not_null<HistoryItem*> realParent) const {
	return main()->clone(newParent, realParent);
}

QSize HistoryGroupedMedia::countOptimalSize() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	std::vector<QSize> sizes;
	sizes.reserve(_elements.size());
	for (const auto &element : _elements) {
		const auto &media = element.content;
		media->initDimensions();
		sizes.push_back(media->sizeForGrouping());
	}

	const auto layout = Ui::LayoutMediaGroup(
		sizes,
		st::historyGroupWidthMax,
		st::historyGroupWidthMin,
		st::historyGroupSkip);
	Assert(layout.size() == _elements.size());

	auto maxWidth = 0;
	auto minHeight = 0;
	for (auto i = 0, count = int(layout.size()); i != count; ++i) {
		const auto &item = layout[i];
		accumulate_max(maxWidth, item.geometry.x() + item.geometry.width());
		accumulate_max(minHeight, item.geometry.y() + item.geometry.height());
		_elements[i].initialGeometry = item.geometry;
		_elements[i].sides = item.sides;
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
	for (auto &element : _elements) {
		const auto sides = element.sides;
		const auto initialGeometry = element.initialGeometry;
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
		element.geometry = QRect(left, top, width, height);

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
	for (const auto &element : _elements) {
		element.content->refreshParentId(element.item);
	}
}

void HistoryGroupedMedia::draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const {
	for (auto i = 0, count = int(_elements.size()); i != count; ++i) {
		const auto &element = _elements[i];
		const auto elementSelection = (selection == FullSelection)
			? FullSelection
			: IsGroupItemSelection(selection, i)
			? FullSelection
			: TextSelection();
		auto corners = Ui::GetCornersFromSides(element.sides);
		if (!isBubbleTop()) {
			corners &= ~(RectPart::TopLeft | RectPart::TopRight);
		}
		if (!isBubbleBottom() || !_caption.isEmpty()) {
			corners &= ~(RectPart::BottomLeft | RectPart::BottomRight);
		}
		element.content->drawGrouped(
			p,
			clip,
			elementSelection,
			ms,
			element.geometry,
			corners,
			&element.cacheKey,
			&element.cache);
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
	} else if (_parent->getMedia() == this) {
		auto fullRight = width();
		auto fullBottom = height();
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, width(), selected, InfoDisplayOverImage);
		}
		if (!_parent->hasBubble() && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, width());
		}
	}
}

HistoryTextState HistoryGroupedMedia::getElementState(
		QPoint point,
		HistoryStateRequest request) const {
	for (const auto &element : _elements) {
		if (element.geometry.contains(point)) {
			auto result = element.content->getStateGrouped(
				element.geometry,
				point,
				request);
			result.itemId = element.item->fullId();
			return result;
		}
	}
	return HistoryTextState(_parent);
}

HistoryTextState HistoryGroupedMedia::getState(
		QPoint point,
		HistoryStateRequest request) const {
	auto result = getElementState(point, request);
	if (!result.link && !_caption.isEmpty()) {
		const auto captionw = width() - st::msgPadding.left() - st::msgPadding.right();
		const auto captiony = height()
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		if (QRect(st::msgPadding.left(), captiony, captionw, height() - captiony).contains(point)) {
			return HistoryTextState(_parent, _caption.getState(
				point - QPoint(st::msgPadding.left(), captiony),
				captionw,
				request.forText()));
		}
	} else if (_parent->getMedia() == this) {
		auto fullRight = width();
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayOverImage)) {
			result.cursor = HistoryInDateCursorState;
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
	for (const auto &element : _elements) {
		if (element.content->toggleSelectionByHandlerClick(p)) {
			return true;
		}
	}
	return false;
}

bool HistoryGroupedMedia::dragItemByHandler(const ClickHandlerPtr &p) const {
	for (const auto &element : _elements) {
		if (element.content->dragItemByHandler(p)) {
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

QString HistoryGroupedMedia::notificationText() const {
	return WithCaptionNotificationText(lang(lng_in_dlg_photo), _caption);
}

QString HistoryGroupedMedia::inDialogsText() const {
	return WithCaptionDialogsText(lang(lng_in_dlg_album), _caption);
}

TextWithEntities HistoryGroupedMedia::selectedText(
		TextSelection selection) const {
	if (!IsSubGroupSelection(selection)) {
		return WithCaptionSelectedText(
			lang(lng_in_dlg_album),
			_caption,
			selection);
	} else if (IsGroupItemSelection(selection, int(_elements.size()) - 1)) {
		return main()->selectedText(FullSelection);
	}
	return TextWithEntities();
}

void HistoryGroupedMedia::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	for (const auto &element : _elements) {
		element.content->clickHandlerActiveChanged(p, active);
	}
}

void HistoryGroupedMedia::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (const auto &element : _elements) {
		element.content->clickHandlerPressedChanged(p, pressed);
		if (pressed && element.content->dragItemByHandler(p)) {
			// #TODO pressedLinkItem
			//App::pressedLinkItem(element.item);
		}
	}
}

void HistoryGroupedMedia::attachToParent() {
	for (const auto &element : _elements) {
		element.content->attachToParent();
	}
}

void HistoryGroupedMedia::detachFromParent() {
	for (const auto &element : _elements) {
		if (element.content) {
			element.content->detachFromParent();
		}
	}
}

std::unique_ptr<HistoryMedia> HistoryGroupedMedia::takeLastFromGroup() {
	return std::move(_elements.back().content);
}

bool HistoryGroupedMedia::applyGroup(
		const std::vector<not_null<HistoryItem*>> &others) {
	if (others.empty()) {
		return false;
	}
	const auto pushElement = [&](not_null<HistoryItem*> item) {
		const auto media = item->getMedia();
		Assert(media != nullptr && media->canBeGrouped());

		_elements.push_back(Element(item));
		_elements.back().content = item->getMedia()->clone(_parent, item);
	};
	if (_elements.empty()) {
		pushElement(_parent);
	} else if (validateGroupElements(others)) {
		return true;
	}

	// We're updating other elements, so we just need to preserve the main.
	auto mainElement = std::move(_elements.back());
	_elements.erase(_elements.begin(), _elements.end());
	_elements.reserve(others.size() + 1);
	for (const auto item : others) {
		pushElement(item);
	}
	_elements.push_back(std::move(mainElement));
	//_parent->setPendingInitDimensions(); // #TODO group view
	return true;
}

bool HistoryGroupedMedia::validateGroupElements(
		const std::vector<not_null<HistoryItem*>> &others) const {
	if (_elements.size() != others.size() + 1) {
		return false;
	}
	for (auto i = 0, count = int(others.size()); i != count; ++i) {
		if (_elements[i].item != others[i]) {
			return false;
		}
	}
	return true;
}

not_null<HistoryMedia*> HistoryGroupedMedia::main() const {
	Expects(!_elements.empty());

	return _elements.back().content.get();
}

bool HistoryGroupedMedia::hasReplyPreview() const {
	return main()->hasReplyPreview();
}

ImagePtr HistoryGroupedMedia::replyPreview() {
	return main()->replyPreview();
}

TextWithEntities HistoryGroupedMedia::getCaption() const {
	return main()->getCaption();
}

Storage::SharedMediaTypesMask HistoryGroupedMedia::sharedMediaTypes() const {
	return main()->sharedMediaTypes();
}

void HistoryGroupedMedia::updateSentMedia(const MTPMessageMedia &media) {
	return main()->updateSentMedia(media);
}

bool HistoryGroupedMedia::needReSetInlineResultMedia(
		const MTPMessageMedia &media) {
	return main()->needReSetInlineResultMedia(media);
}

PhotoData *HistoryGroupedMedia::getPhoto() const {
	return main()->getPhoto();
}

DocumentData *HistoryGroupedMedia::getDocument() const {
	return main()->getDocument();
}

HistoryMessageEdited *HistoryGroupedMedia::displayedEditBadge() const {
	if (!_caption.isEmpty()) {
		return _elements.front().item->Get<HistoryMessageEdited>();
	}
	return nullptr;
}

void HistoryGroupedMedia::updateNeedBubbleState() {
	const auto getItemCaption = [](const Element &element) {
		if (const auto media = element.item->getMedia()) {
			return media->getCaption();
		}
		return element.content->getCaption();
	};
	const auto captionText = [&] {
		auto result = getItemCaption(_elements.front());
		if (result.text.isEmpty()) {
			return result;
		}
		for (auto i = 1, count = int(_elements.size()); i != count; ++i) {
			if (!getItemCaption(_elements[i]).text.isEmpty()) {
				return TextWithEntities();
			}
		}
		return result;
	}();
	_caption.setText(
		st::messageTextStyle,
		captionText.text + _parent->skipBlock(),
		Ui::ItemTextNoMonoOptions(_parent));
	_needBubble = computeNeedBubble();
}

bool HistoryGroupedMedia::needsBubble() const {
	return _needBubble;
}

bool HistoryGroupedMedia::canEditCaption() const {
	return main()->canEditCaption();
}

bool HistoryGroupedMedia::computeNeedBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	if (const auto message = _parent->toHistoryMessage()) {
		if (message->viaBot()
			|| message->Has<HistoryMessageReply>()
			|| message->displayForwardedFrom()
//			|| message->displayFromName() // #TODO media views
			) {
			return true;
		}
	}
	return false;
}

bool HistoryGroupedMedia::needInfoDisplay() const {
	return (_parent->id < 0 || _parent->isUnderCursor());
}
