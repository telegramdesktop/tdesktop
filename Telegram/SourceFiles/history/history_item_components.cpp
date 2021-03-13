/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_components.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/text/text_options.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/media/history_view_document.h"
#include "core/click_handler_types.h"
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "facades.h"
#include "base/qt_adapters.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"

#include <QtGui/QGuiApplication>

namespace {

const auto kPsaForwardedPrefix = "cloud_lng_forwarded_psa_";

} // namespace

void HistoryMessageVia::create(
		not_null<Data::Session*> owner,
		UserId userId) {
	bot = owner->user(userId);
	maxWidth = st::msgServiceNameFont->width(
		tr::lng_inline_bot_via(tr::now, lt_inline_bot, '@' + bot->username));
	link = std::make_shared<LambdaClickHandler>([bot = this->bot](
			ClickContext context) {
		if (QGuiApplication::keyboardModifiers() == Qt::ControlModifier) {
			if (const auto window = App::wnd()) {
				if (const auto controller = window->sessionController()) {
					controller->showPeerInfo(bot);
					return;
				}
			}
		}
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto delegate = my.elementDelegate ? my.elementDelegate() : nullptr) {
			delegate->elementHandleViaClick(bot);
		} else {
			App::insertBotCommand('@' + bot->username);
		}
	});
}

void HistoryMessageVia::resize(int32 availw) const {
	if (availw < 0) {
		text = QString();
		width = 0;
	} else {
		text = tr::lng_inline_bot_via(tr::now, lt_inline_bot, '@' + bot->username);
		if (availw < maxWidth) {
			text = st::msgServiceNameFont->elided(text, availw);
			width = st::msgServiceNameFont->width(text);
		} else if (width < maxWidth) {
			width = maxWidth;
		}
	}
}

void HistoryMessageSigned::refresh(const QString &date) {
	Expects(!isAnonymousRank);

	auto name = author;
	const auto time = qsl(", ") + date;
	const auto timew = st::msgDateFont->width(time);
	const auto namew = st::msgDateFont->width(name);
	isElided = (timew + namew > st::maxSignatureSize);
	if (isElided) {
		name = st::msgDateFont->elided(author, st::maxSignatureSize - timew);
	}
	signature.setText(
		st::msgDateTextStyle,
		name + time,
		Ui::NameTextOptions());
}

int HistoryMessageSigned::maxWidth() const {
	return signature.maxWidth();
}

void HistoryMessageEdited::refresh(const QString &date, bool displayed) {
	const auto prefix = displayed
		? (tr::lng_edited(tr::now) + ' ')
		: QString();
	text.setText(st::msgDateTextStyle, prefix + date, Ui::NameTextOptions());
}

int HistoryMessageEdited::maxWidth() const {
	return text.maxWidth();
}

HiddenSenderInfo::HiddenSenderInfo(const QString &name, bool external)
: name(name)
, colorPeerId(Data::FakePeerIdForJustName(name))
, userpic(
	Data::PeerUserpicColor(colorPeerId),
	(external
		? Ui::EmptyUserpic::ExternalName()
		: name)) {
	nameText.setText(st::msgNameStyle, name, Ui::NameTextOptions());
	const auto parts = name.trimmed().split(' ', base::QStringSkipEmptyParts);
	firstName = parts[0];
	for (const auto &part : parts.mid(1)) {
		if (!lastName.isEmpty()) {
			lastName.append(' ');
		}
		lastName.append(part);
	}
}

void HistoryMessageForwarded::create(const HistoryMessageVia *via) const {
	auto phrase = QString();
	const auto fromChannel = originalSender
		&& originalSender->isChannel()
		&& !originalSender->isMegagroup();
	const auto name = originalSender
		? originalSender->name
		: hiddenSenderInfo->name;
	if (!originalAuthor.isEmpty()) {
		phrase = tr::lng_forwarded_signed(
			tr::now,
			lt_channel,
			name,
			lt_user,
			originalAuthor);
	} else {
		phrase = name;
	}
	if (via && psaType.isEmpty()) {
		if (fromChannel) {
			phrase = tr::lng_forwarded_channel_via(
				tr::now,
				lt_channel,
				textcmdLink(1, phrase),
				lt_inline_bot,
				textcmdLink(2, '@' + via->bot->username));
		} else {
			phrase = tr::lng_forwarded_via(
				tr::now,
				lt_user,
				textcmdLink(1, phrase),
				lt_inline_bot,
				textcmdLink(2, '@' + via->bot->username));
		}
	} else {
		if (fromChannel || !psaType.isEmpty()) {
			auto custom = psaType.isEmpty()
				? QString()
				: Lang::GetNonDefaultValue(
					kPsaForwardedPrefix + psaType.toUtf8());
			phrase = !custom.isEmpty()
				? custom.replace("{channel}", textcmdLink(1, phrase))
				: (psaType.isEmpty()
					? tr::lng_forwarded_channel
					: tr::lng_forwarded_psa_default)(
						tr::now,
						lt_channel,
						textcmdLink(1, phrase));
		} else {
			phrase = tr::lng_forwarded(
				tr::now,
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
	static const auto hidden = std::make_shared<LambdaClickHandler>([] {
		Ui::Toast::Show(tr::lng_forwarded_hidden(tr::now));
	});

	text.setLink(1, fromChannel
		? goToMessageClickHandler(originalSender, originalId)
		: originalSender
		? originalSender->openLink()
		: hidden);
	if (via) {
		text.setLink(2, via->link);
	}
}

bool HistoryMessageReply::updateData(
		not_null<HistoryMessage*> holder,
		bool force) {
	const auto guard = gsl::finally([&] { refreshReplyToDocument(); });
	if (!force) {
		if (replyToMsg || !replyToMsgId) {
			return true;
		}
	}
	if (!replyToMsg) {
		replyToMsg = holder->history()->owner().message(
			(replyToPeerId
				? peerToChannel(replyToPeerId)
				: holder->channelId()),
			replyToMsgId);
		if (replyToMsg) {
			if (replyToMsg->isEmpty()) {
				// Really it is deleted.
				replyToMsg = nullptr;
				force = true;
			} else {
				holder->history()->owner().registerDependentMessage(
					holder,
					replyToMsg);
			}
		}
	}

	if (replyToMsg) {
		replyToText.setText(
			st::messageTextStyle,
			replyToMsg->inReplyText(),
			Ui::DialogTextOptions());

		updateName();

		setReplyToLinkFrom(holder);
		if (!replyToMsg->Has<HistoryMessageForwarded>()) {
			if (auto bot = replyToMsg->viaBot()) {
				replyToVia = std::make_unique<HistoryMessageVia>();
				replyToVia->create(
					&holder->history()->owner(),
					peerToUser(bot->id));
			}
		}
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		holder->history()->owner().requestItemResize(holder);
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryMessageReply::setReplyToLinkFrom(
		not_null<HistoryMessage*> holder) {
	replyToLnk = replyToMsg
		? goToMessageClickHandler(replyToMsg, holder->fullId())
		: nullptr;
}

void HistoryMessageReply::clearData(not_null<HistoryMessage*> holder) {
	replyToVia = nullptr;
	if (replyToMsg) {
		holder->history()->owner().unregisterDependentMessage(
			holder,
			replyToMsg);
		replyToMsg = nullptr;
	}
	replyToMsgId = 0;
	refreshReplyToDocument();
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
		const auto from = [&] {
			if (const auto from = replyToMsg->displayFrom()) {
				return from;
			}
			return replyToMsg->author().get();
		}();
		const auto name = (replyToVia && from->isUser())
			? from->asUser()->firstName
			: from->name;
		replyToName.setText(st::fwdTextStyle, name, Ui::NameTextOptions());
		replyToVersion = replyToMsg->author()->nameVersion;
		bool hasPreview = replyToMsg->media() ? replyToMsg->media()->hasReplyPreview() : false;
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (replyToVia) {
			w += st::msgServiceFont->spacew + replyToVia->maxWidth;
		}

		maxReplyWidth = previewSkip + qMax(w, qMin(replyToText.maxWidth(), int32(st::maxSignatureSize)));
	} else {
		maxReplyWidth = st::msgDateFont->width(replyToMsgId ? tr::lng_profile_loading(tr::now) : tr::lng_deleted_message(tr::now));
	}
	maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + maxReplyWidth + st::msgReplyPadding.right();
}

void HistoryMessageReply::resize(int width) const {
	if (replyToVia) {
		bool hasPreview = replyToMsg->media() ? replyToMsg->media()->hasReplyPreview() : false;
		int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		replyToVia->resize(width - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryMessageReply::itemRemoved(
		HistoryMessage *holder,
		HistoryItem *removed) {
	if (replyToMsg == removed) {
		clearData(holder);
		holder->history()->owner().requestItemResize(holder);
	}
}

void HistoryMessageReply::paint(
		Painter &p,
		not_null<const HistoryView::Element*> holder,
		int x,
		int y,
		int w,
		PaintFlags flags) const {
	bool selected = (flags & PaintFlag::Selected), outbg = holder->hasOutLayout();

	style::color bar = st::msgImgReplyBarColor;
	if (flags & PaintFlag::InBubble) {
		bar = (flags & PaintFlag::Selected) ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	}
	QRect rbar(style::rtlrect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), w + 2 * x));
	p.fillRect(rbar, bar);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			auto hasPreview = replyToMsg->media() ? replyToMsg->media()->hasReplyPreview() : false;
			if (hasPreview && w < st::msgReplyBarSkip + st::msgReplyBarSize.height()) {
				hasPreview = false;
			}
			auto previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				if (const auto image = replyToMsg->media()->replyPreview()) {
					auto to = style::rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x);
					auto previewWidth = image->width() / cIntRetinaFactor();
					auto previewHeight = image->height() / cIntRetinaFactor();
					auto preview = image->pixSingle(previewWidth, previewHeight, to.width(), to.height(), ImageRoundRadius::Small, RectPart::AllCorners, selected ? &st::msgStickerOverlay : nullptr);
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

				if (flags & PaintFlag::InBubble) {
					p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
					p.setTextPalette(outbg ? (selected ? st::outReplyTextPaletteSelected : st::outReplyTextPalette) : (selected ? st::inReplyTextPaletteSelected : st::inReplyTextPalette));
				} else {
					p.setTextPalette(st::imgReplyTextPalette);
				}
				replyToText.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));
			}
		} else {
			p.setFont(st::msgDateFont);
			auto &date = outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg);
			p.setPen((flags & PaintFlag::InBubble) ? date : st::msgDateImgFg);
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(replyToMsgId ? tr::lng_profile_loading(tr::now) : tr::lng_deleted_message(tr::now), w - st::msgReplyBarSkip));
		}
	}
}

void HistoryMessageReply::refreshReplyToDocument() {
	replyToDocumentId = 0;
	if (const auto media = replyToMsg ? replyToMsg->media() : nullptr) {
		if (const auto document = media->document()) {
			replyToDocumentId = document->id;
		}
	}
}

ReplyMarkupClickHandler::ReplyMarkupClickHandler(
	not_null<Data::Session*> owner,
	int row,
	int column,
	FullMsgId context)
: _owner(owner)
, _itemId(context)
, _row(row)
, _column(column) {
}

// Copy to clipboard support.
QString ReplyMarkupClickHandler::copyToClipboardText() const {
	const auto button = getUrlButton();
	return button ? QString::fromUtf8(button->data) : QString();
}

QString ReplyMarkupClickHandler::copyToClipboardContextItemText() const {
	const auto button = getUrlButton();
	return button ? tr::lng_context_copy_link(tr::now) : QString();
}

// Finds the corresponding button in the items markup struct.
// If the button is not found it returns nullptr.
// Note: it is possible that we will point to the different button
// than the one was used when constructing the handler, but not a big deal.
const HistoryMessageMarkupButton *ReplyMarkupClickHandler::getButton() const {
	return HistoryMessageMarkupButton::Get(_owner, _itemId, _row, _column);
}

auto ReplyMarkupClickHandler::getUrlButton() const
-> const HistoryMessageMarkupButton* {
	if (const auto button = getButton()) {
		using Type = HistoryMessageMarkupButton::Type;
		if (button->type == Type::Url || button->type == Type::Auth) {
			return button;
		}
	}
	return nullptr;
}

void ReplyMarkupClickHandler::onClickImpl() const {
	if (const auto item = _owner->message(_itemId)) {
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

QString ReplyMarkupClickHandler::tooltip() const {
	const auto button = getUrlButton();
	const auto url = button ? QString::fromUtf8(button->data) : QString();
	const auto text = _fullDisplayed ? QString() : buttonText();
	if (!url.isEmpty() && !text.isEmpty()) {
		return QString("%1\n\n%2").arg(text, url);
	} else if (url.isEmpty() != text.isEmpty()) {
		return text + url;
	} else {
		return QString();
	}
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
, _selectedAnimation([=](crl::time now) {
	return selectedAnimationCallback(now);
})
, _st(std::move(s)) {
	if (const auto markup = _item->Get<HistoryMessageReplyMarkup>()) {
		const auto owner = &_item->history()->owner();
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
					owner,
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

void ReplyKeyboard::paint(Painter &p, int outerWidth, const QRect &clip) const {
	Assert(_st != nullptr);
	Assert(_width > 0);

	_st->startPaint(p);
	for (const auto &row : _rows) {
		for (const auto &button : row) {
			const auto rect = button.rect;
			if (rect.y() >= clip.y() + clip.height()) return;
			if (rect.y() + rect.height() < clip.y()) continue;

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			_st->paintButton(p, outerWidth, button);
		}
	}
}

ClickHandlerPtr ReplyKeyboard::getLink(QPoint point) const {
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

void ReplyKeyboard::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (!handler) return;

	_savedPressed = pressed ? handler : ClickHandlerPtr();
	auto coords = findButtonCoordsByClickHandler(handler);
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
			if (_savedActive != handler) {
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
		_animations.emplace(indexForAnimation, crl::now());
	}

	if (notStarted && !_selectedAnimation.animating()) {
		_selectedAnimation.start();
	}
}

bool ReplyKeyboard::selectedAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::botKbDuration;
	}
	for (auto i = _animations.begin(); i != _animations.end();) {
		const auto index = std::abs(i->first) - 1;
		const auto row = (index / MatrixRowShift);
		const auto col = index % MatrixRowShift;
		const auto dt = float64(now - i->second) / st::botKbDuration;
		if (dt >= 1) {
			_rows[row][col].howMuchOver = (i->first > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_rows[row][col].howMuchOver = (i->first > 0) ? dt : (1 - dt);
			++i;
		}
	}
	_st->repaint(_item);
	return !_animations.empty();
}

void ReplyKeyboard::clearSelection() {
	for (const auto &[relativeIndex, time] : _animations) {
		const auto index = std::abs(relativeIndex) - 1;
		const auto row = (index / MatrixRowShift);
		const auto col = index % MatrixRowShift;
		_rows[row][col].howMuchOver = 0;
	}
	_animations.clear();
	_selectedAnimation.stop();
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
		const ReplyKeyboard::Button &button) const {
	const QRect &rect = button.rect;
	paintButtonBg(p, rect, button.howMuchOver);
	if (button.ripple) {
		button.ripple->paint(p, rect.x(), rect.y(), outerWidth);
		if (button.ripple->empty()) {
			button.ripple.reset();
		}
	}
	paintButtonIcon(p, rect, outerWidth, button.type);
	if (button.type == HistoryMessageMarkupButton::Type::CallbackWithPassword
		|| button.type == HistoryMessageMarkupButton::Type::Callback
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

HistoryMessageMarkupButton::HistoryMessageMarkupButton(
	Type type,
	const QString &text,
	const QByteArray &data,
	const QString &forwardText,
	int32 buttonId)
: type(type)
, text(text)
, forwardText(forwardText)
, data(data)
, buttonId(buttonId) {
}

HistoryMessageMarkupButton *HistoryMessageMarkupButton::Get(
		not_null<Data::Session*> owner,
		FullMsgId itemId,
		int row,
		int column) {
	if (const auto item = owner->message(itemId)) {
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (row < markup->rows.size()) {
				auto &buttons = markup->rows[row];
				if (column < buttons.size()) {
					return &buttons[column];
				}
			}
		}
	}
	return nullptr;
}

void HistoryMessageReplyMarkup::createFromButtonRows(
		const QVector<MTPKeyboardButtonRow> &list) {
	rows.clear();
	if (list.isEmpty()) {
		return;
	}

	rows.reserve(list.size());
	for (const auto &row : list) {
		row.match([&](const MTPDkeyboardButtonRow &data) {
			auto row = std::vector<Button>();
			row.reserve(data.vbuttons().v.size());
			for (const auto &button : data.vbuttons().v) {
				using Type = Button::Type;
				button.match([&](const MTPDkeyboardButton &data) {
					row.emplace_back(Type::Default, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonCallback &data) {
					row.emplace_back(
						(data.is_requires_password()
							? Type::CallbackWithPassword
							: Type::Callback),
						qs(data.vtext()),
						qba(data.vdata()));
				}, [&](const MTPDkeyboardButtonRequestGeoLocation &data) {
					row.emplace_back(Type::RequestLocation, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonRequestPhone &data) {
					row.emplace_back(Type::RequestPhone, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonUrl &data) {
					row.emplace_back(
						Type::Url,
						qs(data.vtext()),
						qba(data.vurl()));
				}, [&](const MTPDkeyboardButtonSwitchInline &data) {
					const auto type = data.is_same_peer()
						? Type::SwitchInlineSame
						: Type::SwitchInline;
					row.emplace_back(type, qs(data.vtext()), qba(data.vquery()));
					if (type == Type::SwitchInline) {
						// Optimization flag.
						// Fast check on all new messages if there is a switch button to auto-click it.
						flags |= MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button;
					}
				}, [&](const MTPDkeyboardButtonGame &data) {
					row.emplace_back(Type::Game, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonBuy &data) {
					row.emplace_back(Type::Buy, qs(data.vtext()));
				}, [&](const MTPDkeyboardButtonUrlAuth &data) {
					row.emplace_back(
						Type::Auth,
						qs(data.vtext()),
						qba(data.vurl()),
						qs(data.vfwd_text().value_or_empty()),
						data.vbutton_id().v);
				}, [&](const MTPDinputKeyboardButtonUrlAuth &data) {
					LOG(("API Error: inputKeyboardButtonUrlAuth received."));
					// Should not get those for the users.
				}, [&](const MTPDkeyboardButtonRequestPoll &data) {
					const auto quiz = [&] {
						if (!data.vquiz()) {
							return QByteArray();
						}
						return data.vquiz()->match([&](const MTPDboolTrue&) {
							return QByteArray(1, 1);
						}, [&](const MTPDboolFalse&) {
							return QByteArray(1, 0);
						});
					}();
					row.emplace_back(
						Type::RequestPoll,
						qs(data.vtext()),
						quiz);
				});
			}
			if (!row.empty()) {
				rows.push_back(std::move(row));
			}
		});
	}
}

void HistoryMessageReplyMarkup::create(const MTPReplyMarkup &markup) {
	flags = 0;
	rows.clear();
	inlineKeyboard = nullptr;

	switch (markup.type()) {
	case mtpc_replyKeyboardMarkup: {
		auto &d = markup.c_replyKeyboardMarkup();
		flags = d.vflags().v;

		createFromButtonRows(d.vrows().v);
	} break;

	case mtpc_replyInlineMarkup: {
		auto &d = markup.c_replyInlineMarkup();
		flags = MTPDreplyKeyboardMarkup::Flags(0) | MTPDreplyKeyboardMarkup_ClientFlag::f_inline;

		createFromButtonRows(d.vrows().v);
	} break;

	case mtpc_replyKeyboardHide: {
		auto &d = markup.c_replyKeyboardHide();
		flags = mtpCastFlags(d.vflags()) | MTPDreplyKeyboardMarkup_ClientFlag::f_zero;
	} break;

	case mtpc_replyKeyboardForceReply: {
		auto &d = markup.c_replyKeyboardForceReply();
		flags = mtpCastFlags(d.vflags()) | MTPDreplyKeyboardMarkup_ClientFlag::f_force_reply;
	} break;
	}
}

void HistoryMessageReplyMarkup::create(
		const HistoryMessageReplyMarkup &markup) {
	flags = markup.flags;
	inlineKeyboard = nullptr;

	rows.clear();
	rows.reserve(markup.rows.size());
	using Type = HistoryMessageMarkupButton::Type;
	for (const auto &existing : markup.rows) {
		auto row = std::vector<Button>();
		row.reserve(existing.size());
		for (const auto &button : existing) {
			const auto newType = (button.type != Type::SwitchInlineSame)
				? button.type
				: Type::SwitchInline;
			const auto text = button.forwardText.isEmpty()
				? button.text
				: button.forwardText;
			row.emplace_back(
				newType,
				text,
				button.data,
				QString(),
				button.buttonId);
		}
		if (!row.empty()) {
			rows.push_back(std::move(row));
		}
	}
}

HistoryMessageLogEntryOriginal::HistoryMessageLogEntryOriginal() = default;

HistoryMessageLogEntryOriginal::HistoryMessageLogEntryOriginal(
	HistoryMessageLogEntryOriginal &&other)
: page(std::move(other.page)) {
}

HistoryMessageLogEntryOriginal &HistoryMessageLogEntryOriginal::operator=(
		HistoryMessageLogEntryOriginal &&other) {
	page = std::move(other.page);
	return *this;
}

HistoryMessageLogEntryOriginal::~HistoryMessageLogEntryOriginal() = default;

HistoryDocumentCaptioned::HistoryDocumentCaptioned()
: _caption(st::msgFileMinWidth - st::msgPadding.left() - st::msgPadding.right()) {
}

HistoryDocumentVoicePlayback::HistoryDocumentVoicePlayback(
	const HistoryView::Document *that)
: progress(0., 0.)
, progressAnimation([=](crl::time now) {
	const auto nonconst = const_cast<HistoryView::Document*>(that);
	return nonconst->voiceProgressAnimationCallback(now);
}) {
}

void HistoryDocumentVoice::ensurePlayback(
		const HistoryView::Document *that) const {
	if (!_playback) {
		_playback = std::make_unique<HistoryDocumentVoicePlayback>(that);
	}
}

void HistoryDocumentVoice::checkPlaybackFinished() const {
	if (_playback && !_playback->progressAnimation.animating()) {
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
	Media::Player::instance()->cancelSeeking(AudioMsgId::Type::Voice);
}
