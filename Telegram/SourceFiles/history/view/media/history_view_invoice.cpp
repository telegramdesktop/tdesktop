/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_invoice.h"

#include "lang/lang_keys.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/item_text_options.h"
#include "ui/chat/chat_style.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "data/data_media_types.h"
#include "styles/style_chat.h"

namespace {

bool IsOrdinaryUnpaidCreditsInvoice(const Data::Invoice &invoice) {
	return invoice.currency == Ui::kCreditsCurrency
		&& invoice.receiptMsgId == 0
		&& !invoice.isTest
		&& !invoice.isPaidMedia;
}

TextSelection ClampSelection(TextSelection selection, uint16 length) {
	return (selection == FullSelection)
		? selection
		: TextSelection{
			qMin(selection.from, length),
			qMin(selection.to, length),
		};
}

} // namespace

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
	_statusVisible = !IsOrdinaryUnpaidCreditsInvoice(*invoice);
	if (invoice->photo) {
		const auto spoiler = false;
		_attach = std::make_unique<Photo>(
			_parent,
			_parent->data(),
			invoice->photo,
			spoiler);
	} else {
		_attach = nullptr;
	}
	if (_statusVisible) {
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
			int(statusText.text.size()) });
		statusText.text += ' ' + labelText().toUpper();
		_status.setMarkedText(
			st::defaultTextStyle,
			statusText,
			Ui::ItemTextOptions(_parent->data()));
	}

	_receiptMsgId = invoice->receiptMsgId;

	// init strings
	if (!invoice->description.empty()) {
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			invoice->description,
			Ui::WebpageTextDescriptionOptions());
	}
	if (!invoice->title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			invoice->title,
			Ui::WebpageTextTitleOptions());
	}
	_titleTextLength = _title.length();
	_descriptionTextLength = _description.length();
}

void Invoice::assignBottomInfoSkipBlock() {
	_title.removeSkipBlock();
	_description.removeSkipBlock();
	_status.removeSkipBlock();
	if (_attach) {
		return;
	}
	const auto width = _parent->skipBlockWidth();
	const auto height = _parent->skipBlockHeight();
	if (_statusVisible) {
		_status.updateSkipBlock(width, height);
	} else if (_descriptionTextLength) {
		_description.updateSkipBlock(width, height);
	} else if (_titleTextLength) {
		_title.updateSkipBlock(width, height);
	}
}

QSize Invoice::countOptimalSize() {
	assignBottomInfoSkipBlock();
	auto lineHeight = UnitedLineHeight();

	// init dimensions
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	auto titleMinHeight = _titleTextLength ? lineHeight : 0;
	// enable any count of lines in game description / message
	auto descMaxLines = 4096;
	auto descriptionMinHeight = _descriptionTextLength
		? qMin(_description.minHeight(), descMaxLines * lineHeight)
		: 0;

	if (_titleTextLength) {
		accumulate_max(maxWidth, _title.maxWidth());
		minHeight += titleMinHeight;
	}
	if (_descriptionTextLength) {
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
	} else if (_statusVisible) {
		accumulate_max(maxWidth, _status.maxWidth());
		minHeight += st::mediaInBubbleSkip + _status.minHeight();
	} else if (!_titleTextLength && !_descriptionTextLength) {
		minHeight += _parent->skipBlockHeight();
	}
	auto padding = inBubblePadding();
	maxWidth += padding.left() + padding.right();
	minHeight += padding.top() + padding.bottom();
	return { maxWidth, minHeight };
}

QSize Invoice::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto innerWidth = newWidth - st::msgPadding.left() - st::msgPadding.right();

	auto lineHeight = UnitedLineHeight();

	auto newHeight = 0;
	if (!_titleTextLength) {
		_titleHeight = 0;
	} else {
		if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
			_titleHeight = lineHeight;
		} else {
			_titleHeight = 2 * lineHeight;
		}
		newHeight += _titleHeight;
	}

	if (!_descriptionTextLength) {
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
	} else if (_statusVisible) {
		newHeight += st::mediaInBubbleSkip + _status.countHeight(innerWidth);
	} else if (!_titleTextLength && !_descriptionTextLength) {
		newHeight += _parent->skipBlockHeight();
	}
	auto padding = inBubblePadding();
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection Invoice::toDescriptionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _titleTextLength);
}

TextSelection Invoice::fromDescriptionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _titleTextLength);
}

void Invoice::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

void Invoice::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	auto &semibold = stm->msgServiceFg;

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	paintw -= padding.left() + padding.right();

	auto lineHeight = UnitedLineHeight();
	if (_titleHeight) {
		p.setPen(semibold);
		p.setTextPalette(stm->semiboldPalette);

		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, paintw, width(), _titleHeight / lineHeight, style::al_left, 0, -1, endskip, false, context.selection);
		tshift += _titleHeight;

		p.setTextPalette(stm->textPalette);
	}
	if (_descriptionHeight) {
		p.setPen(stm->historyTextFg);
		_parent->prepareCustomEmojiPaint(p, context, _description);
		_description.draw(p, {
			.position = { padding.left(), tshift },
			.outerWidth = width(),
			.availableWidth = paintw,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.selection = toDescriptionSelection(context.selection),
			.useFullWidth = true,
		});
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		p.translate(attachLeft, attachTop);
		_attach->draw(p, context.translated(
			-attachLeft,
			-attachTop
		).withSelection(context.selected()
			? FullSelection
			: TextSelection()));
		if (_statusVisible) {
			auto pixwidth = _attach->width();

			auto available = _status.maxWidth();
			auto statusW = available + 2 * st::msgDateImgPadding.x();
			auto statusH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
			auto statusX = st::msgDateImgDelta;
			auto statusY = st::msgDateImgDelta;

			Ui::FillRoundRect(p, style::rtlrect(statusX, statusY, statusW, statusH, pixwidth), sti->msgDateImgBg, sti->msgDateImgBgCorners);

			p.setFont(st::msgDateFont);
			p.setPen(st->msgDateImgFg());
			_status.drawLeftElided(p, statusX + st::msgDateImgPadding.x(), statusY + st::msgDateImgPadding.y(), available, pixwidth);
		}

		p.translate(-attachLeft, -attachTop);
	} else if (_statusVisible) {
		p.setPen(stm->historyTextFg);
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

	auto lineHeight = UnitedLineHeight();
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
			result.symbol = qMin(result.symbol, _titleTextLength);
		} else if (point.y() >= tshift + _titleHeight) {
			symbolAdd += _titleTextLength;
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
			result.symbol = qMin(result.symbol, _descriptionTextLength);
		} else if (point.y() >= tshift + _descriptionHeight) {
			symbolAdd += _descriptionTextLength;
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
	if (!_descriptionHeight || selection.to <= _titleTextLength) {
		return ClampSelection(
			_title.adjustSelection(selection, type),
			_titleTextLength);
	}
	auto descriptionSelection = ClampSelection(
		_description.adjustSelection(
			toDescriptionSelection(selection),
			type),
		_descriptionTextLength);
	if (selection.from >= _titleTextLength) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = ClampSelection(
		_title.adjustSelection(selection, type),
		_titleTextLength);
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

bool Invoice::hasHeavyPart() const {
	return _attach ? _attach->hasHeavyPart() : false;
}

void Invoice::unloadHeavyPart() {
	if (_attach) {
		_attach->unloadHeavyPart();
	}
	_description.unloadPersistentAnimation();
}

TextForMimeData Invoice::selectedText(TextSelection selection) const {
	const auto titleSelection = (selection == FullSelection)
		? TextSelection{ 0, _titleTextLength }
		: ClampSelection(selection, _titleTextLength);
	const auto descriptionSelection = (selection == FullSelection)
		? TextSelection{ 0, _descriptionTextLength }
		: ClampSelection(
			toDescriptionSelection(selection),
			_descriptionTextLength);
	auto titleResult = _title.toTextForMimeData(titleSelection);
	auto descriptionResult = _description.toTextForMimeData(
		descriptionSelection);
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
