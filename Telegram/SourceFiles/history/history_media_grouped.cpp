/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_media_grouped.h"

#include "history/history_item_components.h"
#include "history/history_media_types.h"
#include "history/history_message.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "ui/grouped_layout.h"
#include "styles/style_history.h"

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

void HistoryGroupedMedia::initDimensions() {
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

	_maxw = _minh = 0;
	for (auto i = 0, count = int(layout.size()); i != count; ++i) {
		const auto &item = layout[i];
		accumulate_max(_maxw, item.geometry.x() + item.geometry.width());
		accumulate_max(_minh, item.geometry.y() + item.geometry.height());
		_elements[i].initialGeometry = item.geometry;
		_elements[i].sides = item.sides;
	}

	if (!_caption.isEmpty()) {
		auto captionw = _maxw - st::msgPadding.left() - st::msgPadding.right();
		_minh += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			_minh += st::msgPadding.bottom();
		}
	}
}

int HistoryGroupedMedia::resizeGetHeight(int width) {
	_width = std::min(width, _maxw);
	_height = 0;
	if (_width < st::historyGroupWidthMin) {
		return _height;
	}

	const auto initialSpacing = st::historyGroupSkip;
	const auto factor = _width / float64(_maxw);
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

		accumulate_max(_height, top + height);
	}

	if (!_caption.isEmpty()) {
		const auto captionw = _width - st::msgPadding.left() - st::msgPadding.right();
		_height += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			_height += st::msgPadding.bottom();
		}
	}

	return _height;
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
		const auto captionw = _width - st::msgPadding.left() - st::msgPadding.right();
		const auto outbg = _parent->hasOutLayout();
		const auto captiony = _height
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), captiony, captionw, style::al_left, 0, -1, selection);
	} else if (_parent->getMedia() == this) {
		auto fullRight = _width;
		auto fullBottom = _height;
		if (_parent->id < 0 || App::hoveredItem() == _parent) {
			_parent->drawInfo(p, fullRight, fullBottom, _width, selected, InfoDisplayOverImage);
		}
		if (!_parent->hasBubble() && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, _width);
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
		const auto captionw = _width - st::msgPadding.left() - st::msgPadding.right();
		const auto captiony = _height
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		if (QRect(st::msgPadding.left(), captiony, captionw, _height - captiony).contains(point)) {
			return HistoryTextState(_parent, _caption.getState(
				point - QPoint(st::msgPadding.left(), captiony),
				captionw,
				request.forText()));
		}
	} else if (_parent->getMedia() == this) {
		auto fullRight = _width;
		auto fullBottom = _height;
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
			App::pressedLinkItem(element.item);
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
	_parent->setPendingInitDimensions();
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
		itemTextNoMonoOptions(_parent));
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
			|| message->displayFromName()) {
			return true;
		}
	}
	return false;
}
