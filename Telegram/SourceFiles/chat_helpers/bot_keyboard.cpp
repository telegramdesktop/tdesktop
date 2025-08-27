/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/bot_keyboard.h"

#include "api/api_bot.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"

namespace {

class Style : public ReplyKeyboard::Style {
public:
	Style(
		not_null<BotKeyboard*> parent,
		const style::BotKeyboardButton &st);

	Images::CornersMaskRef buttonRounding(
		Ui::BubbleRounding outer,
		RectParts sides) const override;

	void startPaint(QPainter &p, const Ui::ChatStyle *st) const override;
	const style::TextStyle &textStyle() const override;
	void repaint(not_null<const HistoryItem*> item) const override;

protected:
	void paintButtonBg(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		Ui::BubbleRounding rounding,
		float64 howMuchOver) const override;
	void paintButtonIcon(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const override;
	void paintButtonLoading(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		Ui::BubbleRounding rounding) const override;
	int minButtonWidth(HistoryMessageMarkupButton::Type type) const override;

private:
	not_null<BotKeyboard*> _parent;

};

Style::Style(
	not_null<BotKeyboard*> parent,
	const style::BotKeyboardButton &st)
: ReplyKeyboard::Style(st), _parent(parent) {
}

void Style::startPaint(QPainter &p, const Ui::ChatStyle *st) const {
	p.setPen(st::botKbColor);
	p.setFont(st::botKbStyle.font);
}

const style::TextStyle &Style::textStyle() const {
	return st::botKbStyle;
}

void Style::repaint(not_null<const HistoryItem*> item) const {
	_parent->update();
}

Images::CornersMaskRef Style::buttonRounding(
		Ui::BubbleRounding outer,
		RectParts sides) const {
	using namespace Images;
	return CornersMaskRef(CornersMask(ImageRoundRadius::Small));
}

void Style::paintButtonBg(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		Ui::BubbleRounding rounding,
		float64 howMuchOver) const {
	Ui::FillRoundRect(p, rect, st::botKbBg, Ui::BotKeyboardCorners);
}

void Style::paintButtonIcon(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const {
	// Buttons with icons should not appear here.
}

void Style::paintButtonLoading(
		QPainter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		Ui::BubbleRounding rounding) const {
	// Buttons with loading progress should not appear here.
}

int Style::minButtonWidth(HistoryMessageMarkupButton::Type type) const {
	int result = 2 * buttonPadding();
	return result;
}

} // namespace

BotKeyboard::BotKeyboard(
	not_null<Window::SessionController*> controller,
	QWidget *parent)
: RpWidget(parent)
, _controller(controller)
, _st(&st::botKbButton) {
	setGeometry(0, 0, _st->margin, st::botKbScroll.deltat);
	_height = st::botKbScroll.deltat;
	setMouseTracking(true);
}

void BotKeyboard::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	p.fillRect(clip, st::historyComposeAreaBg);

	if (_impl) {
		int x = rtl() ? st::botKbScroll.width : _st->margin;
		p.translate(x, st::botKbScroll.deltat);
		_impl->paint(
			p,
			nullptr,
			Ui::BubbleRounding(),
			width(),
			clip.translated(-x, -st::botKbScroll.deltat));
	}
}

void BotKeyboard::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	ClickHandler::pressed();
}

void BotKeyboard::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void BotKeyboard::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		ActivateClickHandler(window(), activated, {
			e->button(),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(_controller),
			})
		});
	}
}

void BotKeyboard::enterEventHook(QEnterEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void BotKeyboard::leaveEventHook(QEvent *e) {
	clearSelection();
}

bool BotKeyboard::moderateKeyActivate(
		int key,
		Fn<ClickContext(FullMsgId)> context) {
	const auto &data = _controller->session().data();

	const auto botCommand = [](int key) {
		if (key == Qt::Key_Q || key == Qt::Key_6) {
			return u"/translate"_q;
		} else if (key == Qt::Key_W || key == Qt::Key_5) {
			return u"/eng"_q;
		} else if (key == Qt::Key_3) {
			return u"/pattern"_q;
		} else if (key == Qt::Key_4) {
			return u"/abuse"_q;
		} else if (key == Qt::Key_0 || key == Qt::Key_E || key == Qt::Key_9) {
			return u"/undo"_q;
		} else if (key == Qt::Key_Plus
				|| key == Qt::Key_QuoteLeft
				|| key == Qt::Key_7) {
			return u"/next"_q;
		} else if (key == Qt::Key_Period
				|| key == Qt::Key_S
				|| key == Qt::Key_8) {
			return u"/stats"_q;
		}
		return QString();
	};

	if (const auto item = data.message(_wasForMsgId)) {
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (key >= Qt::Key_1 && key <= Qt::Key_2) {
				const auto index = int(key - Qt::Key_1);
				if (!markup->data.rows.empty()
					&& index >= 0
					&& index < int(markup->data.rows.front().size())) {
					Api::ActivateBotCommand(
						context(
							_wasForMsgId).other.value<ClickHandlerContext>(),
						0,
						index);
					return true;
				}
			} else if (const auto user = item->history()->peer->asUser()) {
				if (user->isBot() && item->from() == user) {
					const auto command = botCommand(key);
					if (!command.isEmpty()) {
						_sendCommandRequests.fire({
							.peer = user,
							.command = command,
							.context = item->fullId(),
						});
					}
					return true;
				}
			}
		}
	}
	return false;
}

void BotKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!_impl) return;
	_impl->clickHandlerActiveChanged(p, active);
}

void BotKeyboard::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (!_impl) return;
	_impl->clickHandlerPressedChanged(p, pressed, Ui::BubbleRounding());
}

bool BotKeyboard::updateMarkup(HistoryItem *to, bool force) {
	if (!to || !to->definesReplyKeyboard()) {
		if (_wasForMsgId.msg) {
			_maximizeSize = _singleUse = _forceReply = _persistent = false;
			_wasForMsgId = FullMsgId();
			_placeholder = QString();
			_impl = nullptr;
			return true;
		}
		return false;
	}

	const auto peerId = to->history()->peer->id;
	if (_wasForMsgId == FullMsgId(peerId, to->id) && !force) {
		return false;
	}

	_wasForMsgId = FullMsgId(peerId, to->id);

	auto markupFlags = to->replyKeyboardFlags();
	_forceReply = markupFlags & ReplyMarkupFlag::ForceReply;
	_maximizeSize = !(markupFlags & ReplyMarkupFlag::Resize);
	_singleUse = _forceReply || (markupFlags & ReplyMarkupFlag::SingleUse);
	_persistent = (markupFlags & ReplyMarkupFlag::Persistent);

	if (const auto markup = to->Get<HistoryMessageReplyMarkup>()) {
		_placeholder = markup->data.placeholder;
	} else {
		_placeholder = QString();
	}

	_impl = nullptr;
	if (auto markup = to->Get<HistoryMessageReplyMarkup>()) {
		if (!markup->data.rows.empty()) {
			_impl = std::make_unique<ReplyKeyboard>(
				to,
				std::make_unique<Style>(this, *_st));
		}
	}

	resizeToWidth(width(), _maxOuterHeight);

	return true;
}

bool BotKeyboard::hasMarkup() const {
	return _impl != nullptr;
}

bool BotKeyboard::forceReply() const {
	return _forceReply;
}

int BotKeyboard::resizeGetHeight(int newWidth) {
	updateStyle(newWidth);
	_height = st::botKbScroll.deltat + st::botKbScroll.deltab + (_impl ? _impl->naturalHeight() : 0);
	if (_maximizeSize) {
		accumulate_max(_height, _maxOuterHeight);
	}
	if (_impl) {
		int implWidth = newWidth - _st->margin - st::botKbScroll.width;
		int implHeight = _height - (st::botKbScroll.deltat + st::botKbScroll.deltab);
		_impl->resize(implWidth, implHeight);
	}
	return _height;
}

bool BotKeyboard::maximizeSize() const {
	return _maximizeSize;
}

bool BotKeyboard::singleUse() const {
	return _singleUse;
}

bool BotKeyboard::persistent() const {
	return _persistent;
}

void BotKeyboard::updateStyle(int newWidth) {
	if (!_impl) return;

	int implWidth = newWidth - st::botKbButton.margin - st::botKbScroll.width;
	_st = _impl->isEnoughSpace(implWidth, st::botKbButton) ? &st::botKbButton : &st::botKbTinyButton;

	_impl->setStyle(std::make_unique<Style>(this, *_st));
}

void BotKeyboard::clearSelection() {
	if (_impl) {
		if (ClickHandler::setActive(ClickHandlerPtr(), this)) {
			Ui::Tooltip::Hide();
			setCursor(style::cur_default);
		}
	}
}

QPoint BotKeyboard::tooltipPos() const {
	return _lastMousePos;
}

bool BotKeyboard::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

QString BotKeyboard::tooltipText() const {
	if (ClickHandlerPtr lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

void BotKeyboard::updateSelected() {
	Ui::Tooltip::Show(1000, this);

	if (!_impl) return;

	auto p = mapFromGlobal(_lastMousePos);
	auto x = rtl() ? st::botKbScroll.width : _st->margin;

	auto link = _impl->getLink(p - QPoint(x, _st->margin));
	if (ClickHandler::setActive(link, this)) {
		Ui::Tooltip::Hide();
		setCursor(link ? style::cur_pointer : style::cur_default);
	}
}

auto BotKeyboard::sendCommandRequests() const
-> rpl::producer<Bot::SendCommandRequest> {
	return _sendCommandRequests.events();
}

BotKeyboard::~BotKeyboard() = default;
