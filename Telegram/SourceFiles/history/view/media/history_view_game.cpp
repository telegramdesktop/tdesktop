/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_game.h"

#include "lang/lang_keys.h"
#include "history/history_item_components.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/item_text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/cached_round_corners.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "styles/style_chat.h"

namespace HistoryView {

Game::Game(
	not_null<Element*> parent,
	not_null<GameData*> data,
	const TextWithEntities &consumed)
: Media(parent)
, _st(st::historyPagePreview)
, _data(data)
, _title(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _description(st::msgMinWidth - _st.padding.left() - _st.padding.right()) {
	if (!consumed.text.isEmpty()) {
		const auto context = Core::TextContext({
			.session = &history()->session(),
			.repaint = [=] { _parent->customEmojiRepaint(); },
		});
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			consumed,
			Ui::ItemTextOptions(parent->data()),
			context);
	}
	history()->owner().registerGameView(_data, _parent);
}

QSize Game::countOptimalSize() {
	auto lineHeight = UnitedLineHeight();

	const auto item = _parent->data();
	if (!_openl && item->isRegular()) {
		const auto row = 0;
		const auto column = 0;
		_openl = std::make_shared<ReplyMarkupClickHandler>(
			&item->history()->owner(),
			row,
			column,
			item->fullId());
	}

	auto title = TextUtilities::SingleLine(_data->title);

	// init attach
	if (!_attach) {
		_attach = CreateAttach(
			_parent,
			_data->document,
			_data->document ? nullptr : _data->photo);
	}

	// init strings
	if (_description.isEmpty() && !_data->description.isEmpty()) {
		auto text = _data->description;
		if (!text.isEmpty()) {
			auto marked = TextWithEntities { text };
			auto parseFlags = TextParseLinks | TextParseMultiline;
			TextUtilities::ParseEntities(marked, parseFlags);
			_description.setMarkedText(
				st::webPageDescriptionStyle,
				marked,
				Ui::WebpageTextDescriptionOptions());
			if (!_attach) {
				_description.updateSkipBlock(
					_parent->skipBlockWidth(),
					_parent->skipBlockHeight());
			}
		}
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			title,
			Ui::WebpageTextTitleOptions());
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
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) minHeight += st::mediaInBubbleSkip;

		_attach->initDimensions();
		QMargins bubble(_attach->bubbleMargins());
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	auto padding = inBubblePadding() + innerMargin();
	maxWidth += padding.left() + padding.right();
	minHeight += padding.top() + padding.bottom();

	if (!_gameTagWidth) {
		_gameTagWidth = st::msgDateFont->width(tr::lng_game_tag(tr::now).toUpper());
	}
	return { maxWidth, minHeight };
}

void Game::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_openl) {
		_openl->setMessageId(realParent->fullId());
	}
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

QSize Game::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	const auto padding = inBubblePadding() + innerMargin();
	auto innerWidth = newWidth - padding.left() - padding.right();

	// enable any count of lines in game description / message
	auto linesMax = 4096;
	auto lineHeight = UnitedLineHeight();
	auto newHeight = 0;
	if (_title.isEmpty()) {
		_titleLines = 0;
	} else {
		if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
			_titleLines = 1;
		} else {
			_titleLines = 2;
		}
		newHeight += _titleLines * lineHeight;
	}

	if (_description.isEmpty()) {
		_descriptionLines = 0;
	} else {
		auto descriptionHeight = _description.countHeight(innerWidth);
		if (descriptionHeight < (linesMax - _titleLines) * st::webPageDescriptionFont->height) {
			_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
		} else {
			_descriptionLines = (linesMax - _titleLines);
		}
		newHeight += _descriptionLines * lineHeight;
	}

	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) newHeight += st::mediaInBubbleSkip;

		QMargins bubble(_attach->bubbleMargins());

		_attach->resizeGetHeight(innerWidth + bubble.left() + bubble.right());
		newHeight += _attach->height() - bubble.top() - bubble.bottom();
	}
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection Game::toDescriptionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _title);
}

TextSelection Game::fromDescriptionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _title);
}

void Game::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = QRect(0, 0, width(), height());
	auto outer = full.marginsRemoved(inBubblePadding());
	auto inner = outer.marginsRemoved(innerMargin());
	auto tshift = inner.top();
	auto paintw = inner.width();

	const auto colorIndex = parent()->contentColorIndex();
	const auto selected = context.selected();
	const auto cache = context.outbg
		? stm->replyCache[st->colorPatternIndex(colorIndex)].get()
		: st->coloredReplyCache(selected, colorIndex).get();
	Ui::Text::ValidateQuotePaintCache(*cache, _st);
	Ui::Text::FillQuotePaint(p, outer, *cache, _st);

	if (_ripple) {
		_ripple->paint(p, outer.x(), outer.y(), width(), &cache->bg);
		if (_ripple->empty()) {
			_ripple = nullptr;
		}
	}

	auto lineHeight = UnitedLineHeight();
	if (_titleLines) {
		p.setPen(cache->icon);
		p.setTextPalette(context.outbg
			? stm->semiboldPalette
			: st->coloredTextPalette(selected, colorIndex));

		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(
			p,
			inner.left(),
			tshift,
			paintw,
			width(),
			_titleLines,
			style::al_left,
			0,
			-1,
			endskip,
			false,
			context.selection);
		tshift += _titleLines * lineHeight;

		p.setTextPalette(stm->textPalette);
	}
	if (_descriptionLines) {
		p.setPen(stm->historyTextFg);
		auto endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_parent->prepareCustomEmojiPaint(p, context, _description);
		_description.draw(p, {
			.position = { inner.left(), tshift },
			.outerWidth = width(),
			.availableWidth = paintw,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.selection = toDescriptionSelection(context.selection),
			.elisionHeight = _descriptionLines * lineHeight,
			.elisionRemoveFromEnd = endskip,
			.useFullWidth = true,
		});
		tshift += _descriptionLines * lineHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = inner.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		p.translate(attachLeft, attachTop);
		_attach->draw(p, context.translated(
			-attachLeft,
			-attachTop
		).withSelection(context.selected()
			? FullSelection
			: TextSelection()));
		auto pixwidth = _attach->width();
		auto pixheight = _attach->height();

		auto gameW = _gameTagWidth + 2 * st::msgDateImgPadding.x();
		auto gameH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		auto gameX = pixwidth - st::msgDateImgDelta - gameW;
		auto gameY = pixheight - st::msgDateImgDelta - gameH;

		Ui::FillRoundRect(p, style::rtlrect(gameX, gameY, gameW, gameH, pixwidth), sti->msgDateImgBg, sti->msgDateImgBgCorners);

		p.setFont(st::msgDateFont);
		p.setPen(st->msgDateImgFg());
		p.drawTextLeft(gameX + st::msgDateImgPadding.x(), gameY + st::msgDateImgPadding.y(), pixwidth, tr::lng_game_tag(tr::now).toUpper());

		p.translate(-attachLeft, -attachTop);
	}
}

TextState Game::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = QRect(0, 0, width(), height());
	auto outer = full.marginsRemoved(inBubblePadding());
	auto inner = outer.marginsRemoved(innerMargin());
	auto tshift = inner.top();
	auto paintw = inner.width();

	auto symbolAdd = 0;
	auto lineHeight = UnitedLineHeight();
	if (_titleLines) {
		if (point.y() >= tshift && point.y() < tshift + _titleLines * lineHeight) {
			Ui::Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = TextState(_parent, _title.getStateElidedLeft(
				point - QPoint(inner.left(), tshift),
				paintw,
				width(),
				titleRequest));
		} else if (point.y() >= tshift + _titleLines * lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		if (point.y() >= tshift && point.y() < tshift + _descriptionLines * lineHeight) {
			Ui::Text::StateRequestElided descriptionRequest = request.forText();
			descriptionRequest.lines = _descriptionLines;
			result = TextState(_parent, _description.getStateElidedLeft(
				point - QPoint(inner.left(), tshift),
				paintw,
				width(),
				descriptionRequest));
		} else if (point.y() >= tshift + _descriptionLines * lineHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionLines * lineHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = inner.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		if (QRect(attachLeft, tshift, _attach->width(), inner.top() + inner.height() - tshift).contains(point)) {
			if (_attach->isReadyForOpen()) {
				if (_parent->data()->isHistoryEntry()) {
					result.link = _openl;
				}
			} else {
				result = _attach->textState(point - QPoint(attachLeft, attachTop), request);
			}
		}
	}
	if (_parent->data()->isHistoryEntry()) {
		if (!result.link && outer.contains(point)) {
			result.link = _openl;
		}
	}
	_lastPoint = point - outer.topLeft();

	result.symbol += symbolAdd;
	return result;
}

TextSelection Game::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionLines || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void Game::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void Game::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (p == _openl) {
		if (pressed) {
			if (!_ripple) {
				const auto full = QRect(0, 0, width(), height());
				const auto outer = full.marginsRemoved(inBubblePadding());
				_ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					Ui::RippleAnimation::RoundRectMask(
						outer.size(),
						_st.radius),
					[=] { repaint(); });
			}
			_ripple->add(_lastPoint);
		} else if (_ripple) {
			_ripple->lastStop();
		}
	}
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

bool Game::toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const {
	return _attach && _attach->toggleSelectionByHandlerClick(p);
}

bool Game::allowTextSelectionByHandler(const ClickHandlerPtr &p) const {
	return (p == _openl);
}

bool Game::dragItemByHandler(const ClickHandlerPtr &p) const {
	return _attach && _attach->dragItemByHandler(p);
}

TextForMimeData Game::selectedText(TextSelection selection) const {
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

void Game::playAnimation(bool autoplay) {
	if (_attach) {
		if (autoplay) {
			_attach->autoplayAnimation();
		} else {
			_attach->playAnimation();
		}
	}
}

QMargins Game::inBubblePadding() const {
	return {
		st::msgPadding.left(),
		isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip,
		st::msgPadding.right(),
		(isBubbleBottom()
			? (st::msgPadding.left() + bottomInfoPadding())
			: st::mediaInBubbleSkip),
	};
}

QMargins Game::innerMargin() const {
	return _st.padding;
}

int Game::bottomInfoPadding() const {
	if (!isBubbleBottom()) {
		return 0;
	}

	auto result = st::msgDateFont->height;

	// we use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

void Game::parentTextUpdated() {
	if (const auto media = _parent->data()->media()) {
		const auto consumed = media->consumedMessageText();
		if (!consumed.text.isEmpty()) {
			const auto context = Core::TextContext({
				.session = &history()->session(),
				.repaint = [=] { _parent->customEmojiRepaint(); },
			});
			_description.setMarkedText(
				st::webPageDescriptionStyle,
				consumed,
				Ui::ItemTextOptions(_parent->data()),
				context);
		} else {
			_description = Ui::Text::String(st::msgMinWidth
				- _st.padding.left()
				- _st.padding.right());
		}
		history()->owner().requestViewResize(_parent);
	}
}

bool Game::hasHeavyPart() const {
	return _attach ? _attach->hasHeavyPart() : false;
}

void Game::unloadHeavyPart() {
	if (_attach) {
		_attach->unloadHeavyPart();
	}
	_description.unloadPersistentAnimation();
}

Game::~Game() {
	history()->owner().unregisterGameView(_data, _parent);
}

} // namespace HistoryView
