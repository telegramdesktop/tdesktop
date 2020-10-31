/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_location.h"

#include "layout.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/cached_round_corners.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_cloud_file.h"
#include "styles/style_chat.h"

namespace HistoryView {

Location::Location(
	not_null<Element*> parent,
	not_null<Data::CloudImage*> data,
	Data::LocationPoint point,
	const QString &title,
	const QString &description)
: Media(parent)
, _data(data)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(std::make_shared<LocationClickHandler>(point)) {
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

Location::~Location() {
	if (_media) {
		_media = nullptr;
		_parent->checkHeavyPart();
	}
}

void Location::unloadHeavyPart() {
	_media = nullptr;
}

bool Location::hasHeavyPart() const {
	return (_media != nullptr);
}

void Location::ensureMediaCreated() const {
	if (_media) {
		return;
	}
	_media = _data->createView();
	_data->load(&history()->session(), _parent->data()->fullId());
	history()->owner().registerHeavyViewPart(_parent);
}

QSize Location::countOptimalSize() {
	auto tw = fullWidth();
	auto th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	auto minWidth = qMax(st::minPhotoSize, _parent->minWidthForMedia());
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

QSize Location::countCurrentSize(int newWidth) {
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
	auto minWidth = qMax(st::minPhotoSize, _parent->minWidthForMedia());
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

TextSelection Location::toDescriptionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _title);
}

TextSelection Location::fromDescriptionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _title);
}

void Location::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
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
		Ui::FillRoundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? Ui::InSelectedShadowCorners : Ui::InShadowCorners);
	}

	auto roundRadius = ImageRoundRadius::Large;
	auto roundCorners = ((isBubbleTop() && _title.isEmpty() && _description.isEmpty()) ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| (isRoundedInBubbleBottom() ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None);
	auto rthumb = QRect(paintx, painty, paintw, painth);
	ensureMediaCreated();
	if (const auto thumbnail = _media->image()) {
		const auto &pix = thumbnail->pixSingle(paintw, painth, paintw, painth, roundRadius, roundCorners);
		p.drawPixmap(rthumb.topLeft(), pix);
	} else {
		Ui::FillComplexLocationRect(p, rthumb, roundRadius, roundCorners);
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
		Ui::FillComplexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		_parent->drawInfo(p, fullRight, fullBottom, paintx * 2 + paintw, selected, InfoDisplayType::Image);
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

TextState Location::textState(QPoint point, StateRequest request) const {
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
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			if (QRect(fastShareLeft, fastShareTop, size->width(), size->height()).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	result.symbol += symbolAdd;
	return result;
}

TextSelection Location::adjustSelection(TextSelection selection, TextSelectType type) const {
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

TextForMimeData Location::selectedText(TextSelection selection) const {
	auto titleResult = _title.toTextForMimeData(selection);
	auto descriptionResult = _description.toTextForMimeData(
		toDescriptionSelection(selection));
	if (titleResult.empty()) {
		return descriptionResult;
	} else if (descriptionResult.empty()) {
		return titleResult;
	}
	return titleResult.append('\n').append(std::move(descriptionResult));
}

bool Location::needsBubble() const {
	if (!_title.isEmpty() || !_description.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return item->repliesAreComments()
		|| item->externalReply()
		|| item->viaBot()
		|| _parent->displayedReply()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName();
}

int Location::fullWidth() const {
	return st::locationSize.width();
}

int Location::fullHeight() const {
	return st::locationSize.height();
}

} // namespace HistoryView
