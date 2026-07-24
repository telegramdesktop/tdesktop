/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_ephemeral_plate.h"

#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "history/view/history_view_element.h"
#include "ui/text/text_utilities.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxLines = 3;
constexpr auto kFitSlack = 6;

} // namespace

void RefreshEphemeralPlate(
		not_null<const Element*> view,
		Ui::Text::String &text) {
	const auto badge = view->Get<EphemeralBadge>();
	if (!badge || badge->text.isEmpty()) {
		text.clear();
		return;
	} else if (!text.isEmpty()) {
		return;
	}
	if (const auto receiver = badge->receiver) {
		const auto username = receiver->username();
		text.setMarkedText(
			st::historyEphemeralPlateStyle,
			tr::lng_ephemeral_visible_to(
				tr::now,
				lt_user,
				Ui::Text::Link(
					(username.isEmpty()
						? receiver->name()
						: ('@' + username)),
					1),
				tr::marked),
			kMarkupTextOptions);
		text.setLink(1, receiver->openLink());
	} else {
		text.setText(
			st::historyEphemeralPlateStyle,
			badge->text.toString(),
			kPlainTextOptions);
	}
}

int EphemeralPlateMaxWidth(const Ui::Text::String &text) {
	if (text.isEmpty()) {
		return 0;
	}
	return st::msgReplyPadding.left()
		+ st::historyEphemeralIconIn.width()
		+ st::historyEphemeralIconSkip
		+ text.maxWidth()
		+ kFitSlack
		+ st::msgReplyPadding.right();
}

QSize EphemeralPlateSize(const Ui::Text::String &text, int available) {
	if (text.isEmpty()) {
		return QSize();
	}
	const auto &icon = st::historyEphemeralIconIn;
	const auto iconWidth = icon.width() + st::historyEphemeralIconSkip;
	const auto maxBox = EphemeralPlateMaxWidth(text);
	const auto width = (available < 0)
		? maxBox
		: std::min(available, maxBox);
	const auto textw = width
		- st::msgReplyPadding.left()
		- iconWidth
		- st::msgReplyPadding.right();
	const auto lineHeight = st::historyEphemeralPlateStyle.font->height;
	const auto textHeight = (text.maxWidth() <= textw)
		? lineHeight
		: std::min(text.countHeight(textw), kMaxLines * lineHeight);
	return {
		width,
		st::msgReplyPadding.top() + textHeight + st::msgReplyPadding.bottom(),
	};
}

void PaintEphemeralPlate(
		Painter &p,
		const PaintContext &context,
		const Ui::Text::String &text,
		int x,
		int y,
		int width,
		int outerWidth) {
	if (text.isEmpty()) {
		return;
	}
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto height = EphemeralPlateSize(text, width).height();
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(sti->msgServiceBg);
		const auto radius = height / 2;
		p.drawRoundedRect(QRect(x, y, width, height), radius, radius);
	}
	const auto &icon = st::historyEphemeralIconIn;
	const auto lineHeight = st::historyEphemeralPlateStyle.font->height;
	const auto iconTop = y
		+ st::msgReplyPadding.top()
		+ (lineHeight - icon.height()) / 2;
	icon.paint(
		p,
		x + st::msgReplyPadding.left() + st::historyEphemeralIconLeftSkip,
		iconTop,
		outerWidth,
		st->msgServiceFg()->c);
	p.setPen(st->msgServiceFg());
	p.setTextPalette(st->serviceTextPalette());
	const auto textx = x
		+ st::msgReplyPadding.left()
		+ icon.width()
		+ st::historyEphemeralIconSkip;
	const auto textw = width
		- st::msgReplyPadding.left()
		- icon.width()
		- st::historyEphemeralIconSkip
		- st::msgReplyPadding.right();
	text.drawElided(
		p,
		textx,
		y + st::msgReplyPadding.top(),
		textw,
		kMaxLines);
	p.restoreTextPalette();
}

bool EphemeralPlateState(
		not_null<const Element*> view,
		const Ui::Text::String &text,
		QPoint point,
		int x,
		int y,
		int width,
		int height,
		StateRequest request,
		TextState &state) {
	if (text.isEmpty() || !QRect(x, y, width, height).contains(point)) {
		return false;
	}
	const auto &icon = st::historyEphemeralIconIn;
	const auto textx = x
		+ st::msgReplyPadding.left()
		+ icon.width()
		+ st::historyEphemeralIconSkip;
	const auto texty = y + st::msgReplyPadding.top();
	const auto textw = width
		- st::msgReplyPadding.left()
		- icon.width()
		- st::historyEphemeralIconSkip
		- st::msgReplyPadding.right();
	const auto lookup = text.getState(
		point - QPoint(textx, texty),
		textw,
		request.forText());
	state = TextState(view, lookup.link);
	return true;
}

} // namespace HistoryView
