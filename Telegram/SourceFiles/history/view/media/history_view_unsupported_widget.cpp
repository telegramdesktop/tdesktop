/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_unsupported_widget.h"

#include "core/update_checker.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"

#include "styles/style_chat.h"

namespace HistoryView {

UnsupportedWidget::UnsupportedWidget(not_null<Element*> parent)
: Media(parent)
, _link(std::make_shared<LambdaClickHandler>([] {
	Core::UpdateApplication();
})) {
	_card.setTexts(
		tr::lng_unsupported_message_title(tr::now),
		tr::lng_unsupported_message_text(tr::now),
		tr::lng_unsupported_message_update(tr::now));
}

UnsupportedWidget::~UnsupportedWidget() = default;

QSize UnsupportedWidget::countOptimalSize() {
	const auto height = _card.resizeGetHeight(st::unsupportedNoticeMaxWidth);
	return {
		st::msgPadding.left() + _card.width() + st::msgPadding.right(),
		topSkip() + height + bottomSkip(),
	};
}

QSize UnsupportedWidget::countCurrentSize(int newWidth) {
	const auto available = std::max(
		newWidth - st::msgPadding.left() - st::msgPadding.right(),
		0);
	const auto height = _card.resizeGetHeight(available);
	return { newWidth, topSkip() + height + bottomSkip() };
}

void UnsupportedWidget::draw(Painter &p, const PaintContext &context) const {
	_card.paintInBubble(p, context, cardRect(), _ripple.get());
}

TextState UnsupportedWidget::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);
	const auto button = _card.buttonRect().translated(cardRect().topLeft());
	if (button.contains(point)) {
		result.link = _link;
		_lastPoint = point - button.topLeft();
	}
	return result;
}

bool UnsupportedWidget::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return true;
}

bool UnsupportedWidget::dragItemByHandler(const ClickHandlerPtr &p) const {
	return true;
}

void UnsupportedWidget::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (handler == _link) {
		_card.buttonPressedChanged(
			_ripple,
			pressed,
			_lastPoint,
			[=] { repaint(); });
	}
}

bool UnsupportedWidget::hideMessageText() const {
	return false;
}

bool UnsupportedWidget::needsBubble() const {
	return true;
}

bool UnsupportedWidget::customInfoLayout() const {
	return false;
}

int UnsupportedWidget::topSkip() const {
	return isBubbleTop() ? st::msgPadding.left() : 0;
}

int UnsupportedWidget::bottomSkip() const {
	if (!isBubbleBottom()) {
		return 0;
	}
	const auto info = _parent->hidesBottomInfo()
		? 0
		: (st::mediaInBubbleSkip + _parent->skipBlockHeight());
	return st::msgPadding.bottom() + info;
}

QRect UnsupportedWidget::cardRect() const {
	return QRect(
		st::msgPadding.left(),
		topSkip(),
		_card.width(),
		_card.height());
}

} // namespace HistoryView
