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

#include "history/history_media_types.h"
#include "history/history_message.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "ui/grouped_layout.h"
#include "styles/style_history.h"

namespace {

RectParts GetCornersFromSides(RectParts sides) {
	const auto convert = [&](
			RectPart side1,
			RectPart side2,
			RectPart corner) {
		return ((sides & side1) && (sides & side2))
			? corner
			: RectPart::None;
	};
	return RectPart::None
		| convert(RectPart::Top, RectPart::Left, RectPart::TopLeft)
		| convert(RectPart::Top, RectPart::Right, RectPart::TopRight)
		| convert(RectPart::Bottom, RectPart::Left, RectPart::BottomLeft)
		| convert(RectPart::Bottom, RectPart::Right, RectPart::BottomRight);
}

} // namespace

HistoryGroupedMedia::Element::Element(not_null<HistoryItem*> item)
: item(item) {
}

HistoryGroupedMedia::HistoryGroupedMedia(
	not_null<HistoryItem*> parent,
	const std::vector<not_null<HistoryItem*>> &others)
: HistoryMedia(parent) {
	const auto result = applyGroup(others);

	Ensures(result);
}

void HistoryGroupedMedia::initDimensions() {
	std::vector<QSize> sizes;
	sizes.reserve(_elements.size());
	for (const auto &element : _elements) {
		const auto &media = element.content;
		media->initDimensions();
		sizes.push_back(media->sizeForGrouping());
	}

	const auto layout = Data::LayoutMediaGroup(
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
}

int HistoryGroupedMedia::resizeGetHeight(int width) {
	_width = width;
	_height = 0;
	if (_width < st::historyGroupWidthMin) {
		return _height;
	}

	const auto initialSpacing = st::historyGroupSkip;
	const auto factor = width / float64(st::historyGroupWidthMax);
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
	return _height;
}

void HistoryGroupedMedia::draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const {
	for (const auto &element : _elements) {
		auto corners = GetCornersFromSides(element.sides);
		if (!isBubbleTop()) {
			corners &= ~(RectPart::TopLeft | RectPart::TopRight);
		}
		if (!isBubbleBottom() || !_caption.isEmpty()) {
			corners &= ~(RectPart::BottomLeft | RectPart::BottomRight);
		}
		element.content->drawGrouped(
			p,
			clip,
			selection,
			ms,
			element.geometry,
			corners,
			&element.cacheKey,
			&element.cache);
	}
}

HistoryTextState HistoryGroupedMedia::getState(
		QPoint point,
		HistoryStateRequest request) const {
	for (const auto &element : _elements) {
		if (element.geometry.contains(point)) {
			return element.content->getStateGrouped(
				element.geometry,
				point,
				request);
		}
	}
	return HistoryTextState();
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

TextWithEntities HistoryGroupedMedia::selectedText(
		TextSelection selection) const {
	return WithCaptionSelectedText(
		lang(lng_in_dlg_album),
		_caption,
		selection);
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
		_elements.back().content = item->getMedia()->clone(_parent);
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

Storage::SharedMediaTypesMask HistoryGroupedMedia::sharedMediaTypes() const {
	return main()->sharedMediaTypes();
}

void HistoryGroupedMedia::updateNeedBubbleState() {
	auto captionText = [&] {
		for (const auto &element : _elements) {
			auto result = element.content->getCaption();
			if (!result.text.isEmpty()) {
				return result;
			}
		}
		return TextWithEntities();
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

bool HistoryGroupedMedia::computeNeedBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	for (const auto &element : _elements) {
		if (const auto message = element.item->toHistoryMessage()) {
			if (message->viaBot()
				|| message->Has<HistoryMessageForwarded>()
				|| message->Has<HistoryMessageReply>()
				|| message->displayFromName()) {
				return true;
			}
		}
	}
	return false;
}
