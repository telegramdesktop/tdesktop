/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_location.h"

#include "layout.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/image/image.h"
#include "ui/text_options.h"
#include "styles/style_history.h"

namespace {

using TextState = HistoryView::TextState;

} // namespace

HistoryLocation::HistoryLocation(
	not_null<Element*> parent,
	not_null<LocationData*> location,
	const QString &title,
	const QString &description)
: HistoryMedia(parent)
, _data(location)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(std::make_shared<LocationClickHandler>(_data->coords)) {
	if (!title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			TextUtilities::Clean(title),
			Ui::WebpageTextTitleOptions());
	}
	if (!description.isEmpty()) {
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			TextUtilities::ParseEntities(
				TextUtilities::Clean(description),
				TextParseLinks | TextParseMultiline | TextParseRichText),
			Ui::WebpageTextDescriptionOptions());
	}
}

QSize HistoryLocation::countOptimalSize() {
	auto tw = fullWidth();
	auto th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	auto maxWidth = qMax(tw, minWidth);
	auto minHeight = qMax(th, st::minPhotoSize);

	if (_parent->hasBubble()) {
		if (!_title.isEmpty()) {
			minHeight += qMin(_title.countHeight(maxWidth - st::msgPadding.left() - st::msgPadding.right()), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			minHeight += qMin(_description.countHeight(maxWidth - st::msgPadding.left() - st::msgPadding.right()), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			minHeight += st::mediaInBubbleSkip;
			if (isBubbleTop()) {
				minHeight += st::msgPadding.top();
			}
		}
	}
	return { maxWidth, minHeight };
}

QSize HistoryLocation::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());

	auto tw = fullWidth();
	auto th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	auto newHeight = th;
	if (tw > newWidth) {
		newHeight = (newWidth * newHeight / tw);
	} else {
		newWidth = tw;
	}
	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	accumulate_max(newWidth, minWidth);
	accumulate_max(newHeight, st::minPhotoSize);
	if (_parent->hasBubble()) {
		if (!_title.isEmpty()) {
			newHeight += qMin(_title.countHeight(newWidth - st::msgPadding.left() - st::msgPadding.right()), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			newHeight += qMin(_description.countHeight(newWidth - st::msgPadding.left() - st::msgPadding.right()), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			newHeight += st::mediaInBubbleSkip;
			if (isBubbleTop()) {
				newHeight += st::msgPadding.top();
			}
		}
	}
	return { newWidth, newHeight };
}

TextSelection HistoryLocation::toDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(selection, _title);
}

TextSelection HistoryLocation::fromDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, _title);
}

void HistoryLocation::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	if (bubble) {
		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (isBubbleTop()) {
				painty += st::msgPadding.top();
			}
		}

		auto textw = width() - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
			_title.drawLeftElided(p, paintx + st::msgPadding.left(), painty, textw, width(), 2, style::al_left, 0, -1, 0, false, selection);
			painty += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
			_description.drawLeftElided(p, paintx + st::msgPadding.left(), painty, textw, width(), 3, style::al_left, 0, -1, 0, false, toDescriptionSelection(selection));
			painty += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			painty += st::mediaInBubbleSkip;
		}
		painth -= painty;
	} else {
		App::roundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	const auto contextId = _parent->data()->fullId();
	_data->load(contextId);
	auto roundRadius = ImageRoundRadius::Large;
	auto roundCorners = ((isBubbleTop() && _title.isEmpty() && _description.isEmpty()) ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| (isBubbleBottom() ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None);
	auto rthumb = QRect(paintx, painty, paintw, painth);
	if (_data && !_data->thumb->isNull()) {
		const auto &pix = _data->thumb->pixSingle(contextId, paintw, painth, paintw, painth, roundRadius, roundCorners);
		p.drawPixmap(rthumb.topLeft(), pix);
	} else {
		App::complexLocationRect(p, rthumb, roundRadius, roundCorners);
	}
	const auto paintMarker = [&](const style::icon &icon) {
		icon.paint(
			p,
			rthumb.x() + ((rthumb.width() - icon.width()) / 2),
			rthumb.y() + (rthumb.height() / 2) - icon.height(),
			width());
	};
	paintMarker(st::historyMapPoint);
	paintMarker(st::historyMapPointInner);
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		_parent->drawInfo(p, fullRight, fullBottom, paintx * 2 + paintw, selected, InfoDisplayType::Image);
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

TextState HistoryLocation::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	auto symbolAdd = 0;

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();

	if (bubble) {
		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (isBubbleTop()) {
				painty += st::msgPadding.top();
			}
		}

		auto textw = width() - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			auto titleh = qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
			if (point.y() >= painty && point.y() < painty + titleh) {
				result = TextState(_parent, _title.getStateLeft(
					point - QPoint(paintx + st::msgPadding.left(), painty),
					textw,
					width(),
					request.forText()));
				return result;
			} else if (point.y() >= painty + titleh) {
				symbolAdd += _title.length();
			}
			painty += titleh;
		}
		if (!_description.isEmpty()) {
			auto descriptionh = qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
			if (point.y() >= painty && point.y() < painty + descriptionh) {
				result = TextState(_parent, _description.getStateLeft(
					point - QPoint(paintx + st::msgPadding.left(), painty),
					textw,
					width(),
					request.forText()));
			} else if (point.y() >= painty + descriptionh) {
				symbolAdd += _description.length();
			}
			painty += descriptionh;
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			painty += st::mediaInBubbleSkip;
		}
		painth -= painty;
	}
	if (QRect(paintx, painty, paintw, painth).contains(point) && _data) {
		result.link = _link;
	}
	if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Image)) {
			result.cursor = CursorState::Date;
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryLocation::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (_description.isEmpty() || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

TextWithEntities HistoryLocation::selectedText(TextSelection selection) const {
	auto titleResult = _title.originalTextWithEntities(selection);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection));
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}
	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

bool HistoryLocation::needsBubble() const {
	if (!_title.isEmpty() || !_description.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return item->viaBot()
		|| item->Has<HistoryMessageReply>()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName();
	return false;
}

int HistoryLocation::fullWidth() const {
	return st::locationSize.width();
}

int HistoryLocation::fullHeight() const {
	return st::locationSize.height();
}
