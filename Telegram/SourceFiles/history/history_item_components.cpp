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
#include "history/history_item_components.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "history/history_service_layout.h"
#include "history/history_message.h"
#include "history/history_media.h"
#include "history/history_media_types.h"
#include "media/media_audio.h"
#include "media/player/media_player_instance.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"

void HistoryMessageVia::create(UserId userId) {
	bot = App::user(peerFromUser(userId));
	maxWidth = st::msgServiceNameFont->width(
		lng_inline_bot_via(lt_inline_bot, '@' + bot->username));
	link = std::make_shared<LambdaClickHandler>([bot = this->bot] {
		App::insertBotCommand('@' + bot->username);
	});
}

void HistoryMessageVia::resize(int32 availw) const {
	if (availw < 0) {
		text = QString();
		width = 0;
	} else {
		text = lng_inline_bot_via(lt_inline_bot, '@' + bot->username);
		if (availw < maxWidth) {
			text = st::msgServiceNameFont->elided(text, availw);
			width = st::msgServiceNameFont->width(text);
		} else if (width < maxWidth) {
			width = maxWidth;
		}
	}
}

void HistoryMessageSigned::refresh(const QString &date) {
	auto time = qsl(", ") + date;
	auto name = author;
	auto timew = st::msgDateFont->width(time);
	auto namew = st::msgDateFont->width(name);
	if (timew + namew > st::maxSignatureSize) {
		name = st::msgDateFont->elided(author, st::maxSignatureSize - timew);
	}
	signature.setText(st::msgDateTextStyle, name + time, _textNameOptions);
}

int HistoryMessageSigned::maxWidth() const {
	return signature.maxWidth();
}

void HistoryMessageEdited::refresh(const QString &date, bool displayed) {
	const auto prefix = displayed ? (lang(lng_edited) + ' ') : QString();
	text.setText(st::msgDateTextStyle, prefix + date, _textNameOptions);
}

int HistoryMessageEdited::maxWidth() const {
	return text.maxWidth();
}

void HistoryMessageForwarded::create(const HistoryMessageVia *via) const {
	auto phrase = QString();
	auto fromChannel = (originalSender->isChannel() && !originalSender->isMegagroup());
	if (!originalAuthor.isEmpty()) {
		phrase = lng_forwarded_signed(
			lt_channel,
			App::peerName(originalSender),
			lt_user,
			originalAuthor);
	} else {
		phrase = App::peerName(originalSender);
	}
	if (via) {
		if (fromChannel) {
			phrase = lng_forwarded_channel_via(
				lt_channel,
				textcmdLink(1, phrase),
				lt_inline_bot,
				textcmdLink(2, '@' + via->bot->username));
		} else {
			phrase = lng_forwarded_via(
				lt_user,
				textcmdLink(1, phrase),
				lt_inline_bot,
				textcmdLink(2, '@' + via->bot->username));
		}
	} else {
		if (fromChannel) {
			phrase = lng_forwarded_channel(
				lt_channel,
				textcmdLink(1, phrase));
		} else {
			phrase = lng_forwarded(
				lt_user,
				textcmdLink(1, phrase));
		}
	}
	TextParseOptions opts = {
		TextParseRichText,
		0,
		0,
		Qt::LayoutDirectionAuto
	};
	text.setText(st::fwdTextStyle, phrase, opts);
	text.setLink(1, fromChannel
		? goToMessageClickHandler(originalSender, originalId)
		: originalSender->openLink());
	if (via) {
		text.setLink(2, via->link);
	}
}

bool HistoryMessageReply::updateData(HistoryMessage *holder, bool force) {
	if (!force) {
		if (replyToMsg || !replyToMsgId) {
			return true;
		}
	}
	if (!replyToMsg) {
		replyToMsg = App::histItemById(holder->channelId(), replyToMsgId);
		if (replyToMsg) {
			if (replyToMsg->isEmpty()) {
				// Really it is deleted.
				replyToMsg = nullptr;
				force = true;
			} else {
				App::historyRegDependency(holder, replyToMsg);
			}
		}
	}

	if (replyToMsg) {
		replyToText.setText(st::messageTextStyle, TextUtilities::Clean(replyToMsg->inReplyText()), _textDlgOptions);

		updateName();

		replyToLnk = goToMessageClickHandler(replyToMsg);
		if (!replyToMsg->Has<HistoryMessageForwarded>()) {
			if (auto bot = replyToMsg->viaBot()) {
				replyToVia = std::make_unique<HistoryMessageVia>();
				replyToVia->create(peerToUser(bot->id));
			}
		}
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		holder->setPendingInitDimensions();
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryMessageReply::clearData(HistoryMessage *holder) {
	replyToVia = nullptr;
	if (replyToMsg) {
		App::historyUnregDependency(holder, replyToMsg);
		replyToMsg = nullptr;
	}
	replyToMsgId = 0;
}

bool HistoryMessageReply::isNameUpdated() const {
	if (replyToMsg && replyToMsg->author()->nameVersion > replyToVersion) {
		updateName();
		return true;
	}
	return false;
}

void HistoryMessageReply::updateName() const {
	if (replyToMsg) {
		QString name = (replyToVia && replyToMsg->author()->isUser())
			? replyToMsg->author()->asUser()->firstName
			: App::peerName(replyToMsg->author());
		replyToName.setText(st::fwdTextStyle, name, _textNameOptions);
		replyToVersion = replyToMsg->author()->nameVersion;
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (replyToVia) {
			w += st::msgServiceFont->spacew + replyToVia->maxWidth;
		}

		maxReplyWidth = previewSkip + qMax(w, qMin(replyToText.maxWidth(), int32(st::maxSignatureSize)));
	} else {
		maxReplyWidth = st::msgDateFont->width(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message));
	}
	maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + maxReplyWidth + st::msgReplyPadding.right();
}

void HistoryMessageReply::resize(int width) const {
	if (replyToVia) {
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		replyToVia->resize(width - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryMessageReply::itemRemoved(HistoryMessage *holder, HistoryItem *removed) {
	if (replyToMsg == removed) {
		clearData(holder);
		holder->setPendingInitDimensions();
	}
}

void HistoryMessageReply::paint(Painter &p, const HistoryItem *holder, int x, int y, int w, PaintFlags flags) const {
	bool selected = (flags & PaintFlag::Selected), outbg = holder->hasOutLayout();

	style::color bar = st::msgImgReplyBarColor;
	if (flags & PaintFlag::InBubble) {
		bar = (flags & PaintFlag::Selected) ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	}
	QRect rbar(rtlrect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), w + 2 * x));
	p.fillRect(rbar, bar);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			auto hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			if (hasPreview && w < st::msgReplyBarSkip + st::msgReplyBarSize.height()) {
				hasPreview = false;
			}
			auto previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				ImagePtr replyPreview = replyToMsg->getMedia()->replyPreview();
				if (!replyPreview->isNull()) {
					auto to = rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x);
					auto previewWidth = replyPreview->width() / cIntRetinaFactor();
					auto previewHeight = replyPreview->height() / cIntRetinaFactor();
					auto preview = replyPreview->pixSingle(previewWidth, previewHeight, to.width(), to.height(), ImageRoundRadius::Small, RectPart::AllCorners, selected ? &st::msgStickerOverlay : nullptr);
					p.drawPixmap(to.x(), to.y(), preview);
				}
			}
			if (w > st::msgReplyBarSkip + previewSkip) {
				if (flags & PaintFlag::InBubble) {
					p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				} else {
					p.setPen(st::msgImgReplyBarColor);
				}
				replyToName.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				if (replyToVia && w > st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew, y + st::msgReplyPadding.top() + st::msgServiceFont->ascent, replyToVia->text);
				}

				auto replyToAsMsg = replyToMsg->toHistoryMessage();
				if (!(flags & PaintFlag::InBubble)) {
				} else if ((replyToAsMsg && replyToAsMsg->emptyText()) || replyToMsg->serviceMsg()) {
					p.setPen(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
				} else {
					p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
				}
				replyToText.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
			}
		} else {
			p.setFont(st::msgDateFont);
			auto &date = outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg);
			p.setPen((flags & PaintFlag::InBubble) ? date : st::msgDateImgFg);
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message), w - st::msgReplyBarSkip));
		}
	}
}

ReplyMarkupClickHandler::ReplyMarkupClickHandler(
	int row,
	int column,
	FullMsgId context)
: _itemId(context)
, _row(row)
, _column(column) {
}

// Copy to clipboard support.
void ReplyMarkupClickHandler::copyToClipboard() const {
	if (auto button = getButton()) {
		if (button->type == HistoryMessageMarkupButton::Type::Url) {
			auto url = QString::fromUtf8(button->data);
			if (!url.isEmpty()) {
				QApplication::clipboard()->setText(url);
			}
		}
	}
}

QString ReplyMarkupClickHandler::copyToClipboardContextItemText() const {
	if (auto button = getButton()) {
		if (button->type == HistoryMessageMarkupButton::Type::Url) {
			return lang(lng_context_copy_link);
		}
	}
	return QString();
}

// Finds the corresponding button in the items markup struct.
// If the button is not found it returns nullptr.
// Note: it is possible that we will point to the different button
// than the one was used when constructing the handler, but not a big deal.
const HistoryMessageMarkupButton *ReplyMarkupClickHandler::getButton() const {
	if (auto item = App::histItemById(_itemId)) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (_row < markup->rows.size()) {
				auto &row = markup->rows[_row];
				if (_column < row.size()) {
					return &row[_column];
				}
			}
		}
	}
	return nullptr;
}

void ReplyMarkupClickHandler::onClickImpl() const {
	if (const auto item = App::histItemById(_itemId)) {
		App::activateBotCommand(item, _row, _column);
	}
}

// Returns the full text of the corresponding button.
QString ReplyMarkupClickHandler::buttonText() const {
	if (const auto button = getButton()) {
		return button->text;
	}
	return QString();
}

ReplyKeyboard::Button::Button() = default;
ReplyKeyboard::Button::Button(Button &&other) = default;
ReplyKeyboard::Button &ReplyKeyboard::Button::operator=(
	Button &&other) = default;
ReplyKeyboard::Button::~Button() = default;

ReplyKeyboard::ReplyKeyboard(
	not_null<const HistoryItem*> item,
	std::unique_ptr<Style> &&s)
: _item(item)
, _a_selected(animation(this, &ReplyKeyboard::step_selected))
, _st(std::move(s)) {
	if (const auto markup = _item->Get<HistoryMessageReplyMarkup>()) {
		const auto context = _item->fullId();
		const auto rowCount = int(markup->rows.size());
		_rows.reserve(rowCount);
		for (auto i = 0; i != rowCount; ++i) {
			const auto &row = markup->rows.at(i);
			const auto rowSize = int(row.size());
			auto newRow = std::vector<Button>();
			newRow.reserve(rowSize);
			for (auto j = 0; j != rowSize; ++j) {
				auto button = Button();
				const auto text = row[j].text;
				button.type = row.at(j).type;
				button.link = std::make_shared<ReplyMarkupClickHandler>(
					i,
					j,
					context);
				button.text.setText(
					_st->textStyle(),
					TextUtilities::SingleLine(text),
					_textPlainOptions);
				button.characters = text.isEmpty() ? 1 : text.size();
				newRow.push_back(std::move(button));
			}
			_rows.push_back(std::move(newRow));
		}
	}
}

void ReplyKeyboard::updateMessageId() {
	const auto msgId = _item->fullId();
	for (const auto &row : _rows) {
		for (const auto &button : row) {
			button.link->setMessageId(msgId);
		}
	}

}

void ReplyKeyboard::resize(int width, int height) {
	_width = width;

	auto markup = _item->Get<HistoryMessageReplyMarkup>();
	auto y = 0.;
	auto buttonHeight = _rows.empty()
		? float64(_st->buttonHeight())
		: (float64(height + _st->buttonSkip()) / _rows.size());
	for (auto &row : _rows) {
		int s = row.size();

		int widthForButtons = _width - ((s - 1) * _st->buttonSkip());
		int widthForText = widthForButtons;
		int widthOfText = 0;
		int maxMinButtonWidth = 0;
		for_const (auto &button, row) {
			widthOfText += qMax(button.text.maxWidth(), 1);
			int minButtonWidth = _st->minButtonWidth(button.type);
			widthForText -= minButtonWidth;
			accumulate_max(maxMinButtonWidth, minButtonWidth);
		}
		bool exact = (widthForText == widthOfText);
		bool enough = (widthForButtons - s * maxMinButtonWidth) >= widthOfText;

		float64 x = 0;
		for (Button &button : row) {
			int buttonw = qMax(button.text.maxWidth(), 1);
			float64 textw = buttonw, minw = _st->minButtonWidth(button.type);
			float64 w = textw;
			if (exact) {
				w += minw;
			} else if (enough) {
				w = (widthForButtons / float64(s));
				textw = w - minw;
			} else {
				textw = (widthForText / float64(s));
				w = minw + textw;
				accumulate_max(w, 2 * float64(_st->buttonPadding()));
			}

			int rectx = static_cast<int>(std::floor(x));
			int rectw = static_cast<int>(std::floor(x + w)) - rectx;
			button.rect = QRect(rectx, qRound(y), rectw, qRound(buttonHeight - _st->buttonSkip()));
			if (rtl()) button.rect.setX(_width - button.rect.x() - button.rect.width());
			x += w + _st->buttonSkip();

			button.link->setFullDisplayed(textw >= buttonw);
		}
		y += buttonHeight;
	}
}

bool ReplyKeyboard::isEnoughSpace(int width, const style::BotKeyboardButton &st) const {
	for_const (auto &row, _rows) {
		int s = row.size();
		int widthLeft = width - ((s - 1) * st.margin + s * 2 * st.padding);
		for_const (auto &button, row) {
			widthLeft -= qMax(button.text.maxWidth(), 1);
			if (widthLeft < 0) {
				if (row.size() > 3) {
					return false;
				} else {
					break;
				}
			}
		}
	}
	return true;
}

void ReplyKeyboard::setStyle(std::unique_ptr<Style> &&st) {
	_st = std::move(st);
}

int ReplyKeyboard::naturalWidth() const {
	auto result = 0;
	for (const auto &row : _rows) {
		auto maxMinButtonWidth = 0;
		for (const auto &button : row) {
			accumulate_max(
				maxMinButtonWidth,
				_st->minButtonWidth(button.type));
		}
		auto rowMaxButtonWidth = 0;
		for (const auto &button : row) {
			accumulate_max(
				rowMaxButtonWidth,
				qMax(button.text.maxWidth(), 1) + maxMinButtonWidth);
		}

		const auto rowSize = int(row.size());
		accumulate_max(
			result,
			rowSize * rowMaxButtonWidth + (rowSize - 1) * _st->buttonSkip());
	}
	return result;
}

int ReplyKeyboard::naturalHeight() const {
	return (_rows.size() - 1) * _st->buttonSkip() + _rows.size() * _st->buttonHeight();
}

void ReplyKeyboard::paint(Painter &p, int outerWidth, const QRect &clip, TimeMs ms) const {
	Assert(_st != nullptr);
	Assert(_width > 0);

	_st->startPaint(p);
	for_const (auto &row, _rows) {
		for_const (auto &button, row) {
			QRect rect(button.rect);
			if (rect.y() >= clip.y() + clip.height()) return;
			if (rect.y() + rect.height() < clip.y()) continue;

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			_st->paintButton(p, outerWidth, button, ms);
		}
	}
}

ClickHandlerPtr ReplyKeyboard::getState(QPoint point) const {
	Assert(_width > 0);

	for_const (auto &row, _rows) {
		for_const (auto &button, row) {
			QRect rect(button.rect);

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			if (rect.contains(point)) {
				_savedCoords = point;
				return button.link;
			}
		}
	}
	return ClickHandlerPtr();
}

void ReplyKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	_savedActive = active ? p : ClickHandlerPtr();
	auto coords = findButtonCoordsByClickHandler(p);
	if (coords.i >= 0 && _savedPressed != p) {
		startAnimation(coords.i, coords.j, active ? 1 : -1);
	}
}

ReplyKeyboard::ButtonCoords ReplyKeyboard::findButtonCoordsByClickHandler(const ClickHandlerPtr &p) {
	for (int i = 0, rows = _rows.size(); i != rows; ++i) {
		auto &row = _rows[i];
		for (int j = 0, cols = row.size(); j != cols; ++j) {
			if (row[j].link == p) {
				return { i, j };
			}
		}
	}
	return { -1, -1 };
}

void ReplyKeyboard::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (!p) return;

	_savedPressed = pressed ? p : ClickHandlerPtr();
	auto coords = findButtonCoordsByClickHandler(p);
	if (coords.i >= 0) {
		auto &button = _rows[coords.i][coords.j];
		if (pressed) {
			if (!button.ripple) {
				auto mask = Ui::RippleAnimation::roundRectMask(
					button.rect.size(),
					_st->buttonRadius());
				button.ripple = std::make_unique<Ui::RippleAnimation>(
					_st->_st->ripple,
					std::move(mask),
					[this] { _st->repaint(_item); });
			}
			button.ripple->add(_savedCoords - button.rect.topLeft());
		} else {
			if (button.ripple) {
				button.ripple->lastStop();
			}
			if (_savedActive != p) {
				startAnimation(coords.i, coords.j, -1);
			}
		}
	}
}

void ReplyKeyboard::startAnimation(int i, int j, int direction) {
	auto notStarted = _animations.empty();

	int indexForAnimation = (i * MatrixRowShift + j + 1) * direction;

	_animations.remove(-indexForAnimation);
	if (!_animations.contains(indexForAnimation)) {
		_animations.emplace(indexForAnimation, getms());
	}

	if (notStarted && !_a_selected.animating()) {
		_a_selected.start();
	}
}

void ReplyKeyboard::step_selected(TimeMs ms, bool timer) {
	for (auto i = _animations.begin(); i != _animations.end();) {
		const auto index = std::abs(i->first) - 1;
		const auto row = (index / MatrixRowShift);
		const auto col = index % MatrixRowShift;
		const auto dt = float64(ms - i->second) / st::botKbDuration;
		if (dt >= 1) {
			_rows[row][col].howMuchOver = (i->first > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_rows[row][col].howMuchOver = (i->first > 0) ? dt : (1 - dt);
			++i;
		}
	}
	if (timer) _st->repaint(_item);
	if (_animations.empty()) {
		_a_selected.stop();
	}
}

void ReplyKeyboard::clearSelection() {
	for (const auto [relativeIndex, time] : _animations) {
		const auto index = std::abs(relativeIndex) - 1;
		const auto row = (index / MatrixRowShift);
		const auto col = index % MatrixRowShift;
		_rows[row][col].howMuchOver = 0;
	}
	_animations.clear();
	_a_selected.stop();
}

int ReplyKeyboard::Style::buttonSkip() const {
	return _st->margin;
}

int ReplyKeyboard::Style::buttonPadding() const {
	return _st->padding;
}

int ReplyKeyboard::Style::buttonHeight() const {
	return _st->height;
}

void ReplyKeyboard::Style::paintButton(
		Painter &p,
		int outerWidth,
		const ReplyKeyboard::Button &button,
		TimeMs ms) const {
	const QRect &rect = button.rect;
	paintButtonBg(p, rect, button.howMuchOver);
	if (button.ripple) {
		button.ripple->paint(p, rect.x(), rect.y(), outerWidth, ms);
		if (button.ripple->empty()) {
			button.ripple.reset();
		}
	}
	paintButtonIcon(p, rect, outerWidth, button.type);
	if (button.type == HistoryMessageMarkupButton::Type::Callback
		|| button.type == HistoryMessageMarkupButton::Type::Game) {
		if (auto data = button.link->getButton()) {
			if (data->requestId) {
				paintButtonLoading(p, rect);
			}
		}
	}

	int tx = rect.x(), tw = rect.width();
	if (tw >= st::botKbStyle.font->elidew + _st->padding * 2) {
		tx += _st->padding;
		tw -= _st->padding * 2;
	} else if (tw > st::botKbStyle.font->elidew) {
		tx += (tw - st::botKbStyle.font->elidew) / 2;
		tw = st::botKbStyle.font->elidew;
	}
	button.text.drawElided(p, tx, rect.y() + _st->textTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
}

void HistoryMessageReplyMarkup::createFromButtonRows(const QVector<MTPKeyboardButtonRow> &v) {
	if (v.isEmpty()) {
		rows.clear();
		return;
	}

	rows.reserve(v.size());
	for_const (auto &row, v) {
		switch (row.type()) {
		case mtpc_keyboardButtonRow: {
			auto &r = row.c_keyboardButtonRow();
			auto &b = r.vbuttons.v;
			if (!b.isEmpty()) {
				auto buttonRow = std::vector<Button>();
				buttonRow.reserve(b.size());
				for_const (auto &button, b) {
					switch (button.type()) {
					case mtpc_keyboardButton: {
						buttonRow.push_back({ Button::Type::Default, qs(button.c_keyboardButton().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonCallback: {
						auto &buttonData = button.c_keyboardButtonCallback();
						buttonRow.push_back({ Button::Type::Callback, qs(buttonData.vtext), qba(buttonData.vdata), 0 });
					} break;
					case mtpc_keyboardButtonRequestGeoLocation: {
						buttonRow.push_back({ Button::Type::RequestLocation, qs(button.c_keyboardButtonRequestGeoLocation().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonRequestPhone: {
						buttonRow.push_back({ Button::Type::RequestPhone, qs(button.c_keyboardButtonRequestPhone().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonUrl: {
						auto &buttonData = button.c_keyboardButtonUrl();
						buttonRow.push_back({ Button::Type::Url, qs(buttonData.vtext), qba(buttonData.vurl), 0 });
					} break;
					case mtpc_keyboardButtonSwitchInline: {
						auto &buttonData = button.c_keyboardButtonSwitchInline();
						auto buttonType = buttonData.is_same_peer() ? Button::Type::SwitchInlineSame : Button::Type::SwitchInline;
						buttonRow.push_back({ buttonType, qs(buttonData.vtext), qba(buttonData.vquery), 0 });
						if (buttonType == Button::Type::SwitchInline) {
							// Optimization flag.
							// Fast check on all new messages if there is a switch button to auto-click it.
							flags |= MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button;
						}
					} break;
					case mtpc_keyboardButtonGame: {
						auto &buttonData = button.c_keyboardButtonGame();
						buttonRow.push_back({ Button::Type::Game, qs(buttonData.vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonBuy: {
						auto &buttonData = button.c_keyboardButtonBuy();
						buttonRow.push_back({ Button::Type::Buy, qs(buttonData.vtext), QByteArray(), 0 });
					}
					}
				}
				if (!buttonRow.empty()) {
					rows.push_back(std::move(buttonRow));
				}
			}
		} break;
		}
	}
}

void HistoryMessageReplyMarkup::create(const MTPReplyMarkup &markup) {
	flags = 0;
	rows.clear();
	inlineKeyboard = nullptr;

	switch (markup.type()) {
	case mtpc_replyKeyboardMarkup: {
		auto &d = markup.c_replyKeyboardMarkup();
		flags = d.vflags.v;

		createFromButtonRows(d.vrows.v);
	} break;

	case mtpc_replyInlineMarkup: {
		auto &d = markup.c_replyInlineMarkup();
		flags = MTPDreplyKeyboardMarkup::Flags(0) | MTPDreplyKeyboardMarkup_ClientFlag::f_inline;

		createFromButtonRows(d.vrows.v);
	} break;

	case mtpc_replyKeyboardHide: {
		auto &d = markup.c_replyKeyboardHide();
		flags = mtpCastFlags(d.vflags) | MTPDreplyKeyboardMarkup_ClientFlag::f_zero;
	} break;

	case mtpc_replyKeyboardForceReply: {
		auto &d = markup.c_replyKeyboardForceReply();
		flags = mtpCastFlags(d.vflags) | MTPDreplyKeyboardMarkup_ClientFlag::f_force_reply;
	} break;
	}
}

void HistoryMessageReplyMarkup::create(
		const HistoryMessageReplyMarkup &markup) {
	flags = markup.flags;
	inlineKeyboard = nullptr;

	rows.clear();
	for (const auto &row : markup.rows) {
		auto buttonRow = std::vector<Button>();
		buttonRow.reserve(row.size());
		for (const auto &button : row) {
			buttonRow.push_back({ button.type, button.text, button.data, 0 });
		}
		if (!buttonRow.empty()) {
			rows.push_back(std::move(buttonRow));
		}
	}
}

void HistoryMessageUnreadBar::init(int count) {
	if (_freezed) return;
	_text = lng_unread_bar(lt_count, count);
	_width = st::semiboldFont->width(_text);
}

int HistoryMessageUnreadBar::height() {
	return st::historyUnreadBarHeight + st::historyUnreadBarMargin;
}

int HistoryMessageUnreadBar::marginTop() {
	return st::lineWidth + st::historyUnreadBarMargin;
}

void HistoryMessageUnreadBar::paint(Painter &p, int y, int w) const {
	p.fillRect(0, y + marginTop(), w, height() - marginTop() - st::lineWidth, st::historyUnreadBarBg);
	p.fillRect(0, y + height() - st::lineWidth, w, st::lineWidth, st::historyUnreadBarBorder);
	p.setFont(st::historyUnreadBarFont);
	p.setPen(st::historyUnreadBarFg);

	int left = st::msgServiceMargin.left();
	int maxwidth = w;
	if (Adaptive::ChatWide()) {
		maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	w = maxwidth;

	p.drawText((w - _width) / 2, y + marginTop() + (st::historyUnreadBarHeight - 2 * st::lineWidth - st::historyUnreadBarFont->height) / 2 + st::historyUnreadBarFont->ascent, _text);
}

void HistoryMessageDate::init(const QDateTime &date) {
	_text = langDayOfMonthFull(date.date());
	_width = st::msgServiceFont->width(_text);
}

int HistoryMessageDate::height() const {
	return st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom() + st::msgServiceMargin.bottom();
}

void HistoryMessageDate::paint(Painter &p, int y, int w) const {
	HistoryLayout::ServiceMessagePainter::paintDate(p, _text, _width, y, w);
}

HistoryMessageLogEntryOriginal::HistoryMessageLogEntryOriginal() = default;

HistoryMessageLogEntryOriginal::HistoryMessageLogEntryOriginal(HistoryMessageLogEntryOriginal &&other) : _page(std::move(other._page)) {
}

HistoryMessageLogEntryOriginal &HistoryMessageLogEntryOriginal::operator=(HistoryMessageLogEntryOriginal &&other) {
	_page = std::move(other._page);
	return *this;
}

HistoryMessageLogEntryOriginal::~HistoryMessageLogEntryOriginal() = default;

HistoryDocumentCaptioned::HistoryDocumentCaptioned()
: _caption(st::msgFileMinWidth - st::msgPadding.left() - st::msgPadding.right()) {
}

HistoryDocumentVoicePlayback::HistoryDocumentVoicePlayback(const HistoryDocument *that)
: a_progress(0., 0.)
, _a_progress(animation(const_cast<HistoryDocument*>(that), &HistoryDocument::step_voiceProgress)) {
}

void HistoryDocumentVoice::ensurePlayback(const HistoryDocument *that) const {
	if (!_playback) {
		_playback = std::make_unique<HistoryDocumentVoicePlayback>(that);
	}
}

void HistoryDocumentVoice::checkPlaybackFinished() const {
	if (_playback && !_playback->_a_progress.animating()) {
		_playback.reset();
	}
}

void HistoryDocumentVoice::startSeeking() {
	_seeking = true;
	_seekingCurrent = _seekingStart;
	Media::Player::instance()->startSeeking(AudioMsgId::Type::Voice);
}

void HistoryDocumentVoice::stopSeeking() {
	_seeking = false;
	Media::Player::instance()->stopSeeking(AudioMsgId::Type::Voice);
}
