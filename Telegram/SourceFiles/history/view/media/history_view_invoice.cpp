/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_invoice.h"

#include "lang/lang_keys.h"
#include "layout.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/item_text_options.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "data/data_media_types.h"
#include "styles/style_chat.h"

namespace HistoryView {

Invoice::Invoice(
	not_null<Element*> parent,
	not_null<Data::Invoice*> invoice)
: Media(parent)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _status(st::msgMinWidth) {
	fillFromData(invoice);
}

void Invoice::fillFromData(not_null<Data::Invoice*> invoice) {
	if (invoice->photo) {
		_attach = std::make_unique<Photo>(
			_parent,
			_parent->data(),
			invoice->photo);
	} else {
		_attach = nullptr;
	}
	auto labelText = [&] {
		if (invoice->receiptMsgId) {
			if (invoice->isTest) {
				return tr::lng_payments_receipt_label_test(tr::now);
			}
			return tr::lng_payments_receipt_label(tr::now);
		} else if (invoice->isTest) {
			return tr::lng_payments_invoice_label_test(tr::now);
		}
		return tr::lng_payments_invoice_label(tr::now);
	};
	auto statusText = TextWithEntities {
		Ui::FillAmountAndCurrency(invoice->amount, invoice->currency),
		EntitiesInText()
	};
	statusText.entities.push_back({
		EntityType::Bold,
		0,
		statusText.text.size() });
	statusText.text += ' ' + labelText().toUpper();
	_status.setMarkedText(
		st::defaultTextStyle,
		statusText,
		Ui::ItemTextOptions(_parent->data()));

	_receiptMsgId = invoice->receiptMsgId;

	// init strings
	if (!invoice->description.isEmpty()) {
		auto marked = TextWithEntities { invoice->description };
		auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
		TextUtilities::ParseEntities(marked, parseFlags);
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			marked,
			Ui::WebpageTextDescriptionOptions());
	}
	if (!invoice->title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			invoice->title,
			Ui::WebpageTextTitleOptions());
	}
}

QSize Invoice::countOptimalSize() {
	auto lineHeight = unitedLineHeight();

	if (_attach) {
		if (_status.hasSkipBlock()) {
			_status.removeSkipBlock();
		}
	} else {
		_status.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	// init dimensions
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	auto titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	// enable any count of lines in game description / message
	auto descMaxLines = 4096;
	auto descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);

	if (!_title.isEmpty()) {
		accumulate_max(maxWidth, _title.maxWidth());
		minHeight += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(maxWidth, _description.maxWidth());
		minHeight += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = _title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) minHeight += st::mediaInBubbleSkip;

		_attach->initDimensions();
		auto bubble = _attach->bubbleMargins();
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - bubble.top() - bubble.bottom();
	} else {
		accumulate_max(maxWidth, _status.maxWidth());
		minHeight += st::mediaInBubbleSkip + _status.minHeight();
	}
	auto padding = inBubblePadding();
	maxWidth += padding.left() + padding.right();
	minHeight += padding.top() + padding.bottom();
	return { maxWidth, minHeight };
}

QSize Invoice::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto innerWidth = newWidth - st::msgPadding.left() - st::msgPadding.right();

	auto lineHeight = unitedLineHeight();

	auto newHeight = 0;
	if (_title.isEmpty()) {
		_titleHeight = 0;
	} else {
		if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
			_titleHeight = lineHeight;
		} else {
			_titleHeight = 2 * lineHeight;
		}
		newHeight += _titleHeight;
	}

	if (_description.isEmpty()) {
		_descriptionHeight = 0;
	} else {
		_descriptionHeight = _description.countHeight(innerWidth);
		newHeight += _descriptionHeight;
	}

	if (_attach) {
		auto attachAtTop = !_title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) newHeight += st::mediaInBubbleSkip;

		QMargins bubble(_attach->bubbleMargins());

		_attach->resizeGetHeight(innerWidth + bubble.left() + bubble.right());
		newHeight += _attach->height() - bubble.top() - bubble.bottom();
		if (isBubbleBottom() && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > innerWidth + bubble.left() + bubble.right()) {
			newHeight += bottomInfoPadding();
		}
	} else {
		newHeight += st::mediaInBubbleSkip + _status.countHeight(innerWidth);
	}
	auto padding = inBubblePadding();
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection Invoice::toDescriptionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _title);
}

TextSelection Invoice::fromDescriptionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _title);
}

void Invoice::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

void Invoice::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	paintw -= padding.left() + padding.right();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	auto lineHeight = unitedLineHeight();
	if (_titleHeight) {
		p.setPen(semibold);
		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outSemiboldPalette : st::inSemiboldPalette));

		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, paintw, width(), _titleHeight / lineHeight, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleHeight;

		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));
	}
	if (_descriptionHeight) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		_description.drawLeft(p, padding.left(), tshift, paintw, width(), style::al_left, 0, -1, toDescriptionSelection(selection));
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };

		p.translate(attachLeft, attachTop);
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->width();

		auto available = _status.maxWidth();
		auto statusW = available + 2 * st::msgDateImgPadding.x();
		auto statusH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = st::msgDateImgDelta;
		auto statusY = st::msgDateImgDelta;

		Ui::FillRoundRect(p, style::rtlrect(statusX, statusY, statusW, statusH, pixwidth), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? Ui::DateSelectedCorners : Ui::DateCorners);

		p.setFont(st::msgDateFont);
		p.setPen(st::msgDateImgFg);
		_status.drawLeftElided(p, statusX + st::msgDateImgPadding.x(), statusY + st::msgDateImgPadding.y(), available, pixwidth);

		p.translate(-attachLeft, -attachTop);
	} else {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		_status.drawLeft(p, padding.left(), tshift + st::mediaInBubbleSkip, paintw, width());
	}
}

TextState Invoice::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintw = width();

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}
	paintw -= padding.left() + padding.right();

	auto lineHeight = unitedLineHeight();
	auto symbolAdd = 0;
	if (_titleHeight) {
		if (point.y() >= tshift && point.y() < tshift + _titleHeight) {
			Ui::Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleHeight / lineHeight;
			result = TextState(_parent, _title.getStateElidedLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				titleRequest));
		} else if (point.y() >= tshift + _titleHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleHeight;
	}
	if (_descriptionHeight) {
		if (point.y() >= tshift && point.y() < tshift + _descriptionHeight) {
			result = TextState(_parent, _description.getStateLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				request.forText()));
		} else if (point.y() >= tshift + _descriptionHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		if (QRect(attachLeft, tshift, _attach->width(), height() - tshift - bshift).contains(point)) {
			result = _attach->textState(point - QPoint(attachLeft, attachTop), request);
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection Invoice::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionHeight || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void Invoice::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void Invoice::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

TextForMimeData Invoice::selectedText(TextSelection selection) const {
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

QMargins Invoice::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.top() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.bottom() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

int Invoice::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;
	return result;
}

} // namespace HistoryView
