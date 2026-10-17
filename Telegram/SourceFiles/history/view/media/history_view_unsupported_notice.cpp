/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_unsupported_notice.h"

#include "core/update_checker.h"
#include "history/view/history_view_cursor_state.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"

#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] ClickHandlerPtr MakeUpdateTelegramHandler() {
	return std::make_shared<LambdaClickHandler>([] {
		Core::UpdateApplication();
	});
}

} // namespace

UnsupportedNotice::UnsupportedNotice(not_null<Element*> parent)
: Media(parent)
, _link(MakeUpdateTelegramHandler()) {
	_card.setTexts(
		tr::lng_unsupported_message_title(tr::now),
		tr::lng_unsupported_message_text(tr::now),
		tr::lng_unsupported_message_update(tr::now));
}

QSize UnsupportedNotice::countOptimalSize() {
	const auto &margins = st::msgServiceMargin;
	const auto height = _card.resizeGetHeight(st::unsupportedNoticeMaxWidth);
	return {
		margins.left() + _card.width() + margins.right(),
		margins.top() + height + margins.bottom(),
	};
}

QSize UnsupportedNotice::countCurrentSize(int newWidth) {
	const auto &margins = st::msgServiceMargin;
	accumulate_min(newWidth, maxWidth());
	const auto available = std::max(
		newWidth - margins.left() - margins.right(),
		0);
	const auto height = _card.resizeGetHeight(available);
	return {
		margins.left() + _card.width() + margins.right(),
		margins.top() + height + margins.bottom(),
	};
}

void UnsupportedNotice::draw(Painter &p, const PaintContext &context) const {
	const auto &margins = st::msgServiceMargin;
	const auto card = QRect(
		margins.left(),
		margins.top(),
		_card.width(),
		_card.height());
	_card.paint(p, context, card, _ripple.get());
}

TextState UnsupportedNotice::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);
	const auto button = buttonRect();
	if (button.contains(point)) {
		result.link = _link;
		_lastPoint = point - button.topLeft();
	}
	return result;
}

bool UnsupportedNotice::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return true;
}

bool UnsupportedNotice::dragItemByHandler(const ClickHandlerPtr &p) const {
	return true;
}

void UnsupportedNotice::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (handler != _link) {
		return;
	} else if (pressed) {
		if (!_ripple) {
			const auto size = _card.buttonSize();
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(size, size.height() / 2),
				[=] { repaint(); });
		}
		_ripple->add(_lastPoint);
	} else if (_ripple) {
		_ripple->lastStop();
	}
}

bool UnsupportedNotice::needsBubble() const {
	return false;
}

bool UnsupportedNotice::customInfoLayout() const {
	return true;
}

QRect UnsupportedNotice::buttonRect() const {
	return _card.buttonRect().translated(
		st::msgServiceMargin.left(),
		st::msgServiceMargin.top());
}

} // namespace HistoryView
