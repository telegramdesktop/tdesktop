/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/unread_badge.h"

#include "data/data_peer.h"
#include "dialogs/dialogs_layout.h"
#include "lang/lang_keys.h"
#include "styles/style_dialogs.h"

namespace Ui {

void UnreadBadge::setText(const QString &text, bool active) {
	_text = text;
	_active = active;
	const auto st = Dialogs::Layout::UnreadBadgeStyle();
	resize(
		std::max(st.font->width(text) + 2 * st.padding, st.size),
		st.size);
	update();
}

int UnreadBadge::textBaseline() const {
	const auto st = Dialogs::Layout::UnreadBadgeStyle();
	return ((st.size - st.font->height) / 2) + st.font->ascent;
}

void UnreadBadge::paintEvent(QPaintEvent *e) {
	if (_text.isEmpty()) {
		return;
	}

	Painter p(this);

	Dialogs::Layout::UnreadBadgeStyle unreadSt;
	unreadSt.muted = !_active;
	auto unreadRight = width();
	auto unreadTop = 0;
	Dialogs::Layout::paintUnreadCount(
		p,
		_text,
		unreadRight,
		unreadTop,
		unreadSt);
}

QSize ScamBadgeSize(bool fake) {
	const auto phrase = fake
		? tr::lng_fake_badge(tr::now)
		: tr::lng_scam_badge(tr::now);
	const auto phraseWidth = st::dialogsScamFont->width(phrase);
	const auto width = st::dialogsScamPadding.left()
		+ phraseWidth
		+ st::dialogsScamPadding.right();
	const auto height = st::dialogsScamPadding.top()
		+ st::dialogsScamFont->height
		+ st::dialogsScamPadding.bottom();
	return { width, height };
}

void DrawScamFakeBadge(
		Painter &p,
		QRect rect,
		int outerWidth,
		const style::color &color,
		const QString &phrase,
		int phraseWidth) {
	PainterHighQualityEnabler hq(p);
	auto pen = color->p;
	pen.setWidth(st::lineWidth);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(rect, st::dialogsScamRadius, st::dialogsScamRadius);
	p.setFont(st::dialogsScamFont);
	p.drawTextLeft(
		rect.x() + st::dialogsScamPadding.left(),
		rect.y() + st::dialogsScamPadding.top(),
		outerWidth,
		phrase,
		phraseWidth);
}

void DrawScamBadge(
		bool fake,
		Painter &p,
		QRect rect,
		int outerWidth,
		const style::color &color) {
	const auto phrase = fake
		? tr::lng_fake_badge(tr::now)
		: tr::lng_scam_badge(tr::now);
	DrawScamFakeBadge(
		p,
		rect,
		outerWidth,
		color,
		phrase,
		st::dialogsScamFont->width(phrase));
}

int DrawPeerBadgeGetWidth(
		not_null<PeerData*> peer,
		Painter &p,
		QRect rectForName,
		int nameWidth,
		int outerWidth,
		const PeerBadgeStyle &st) {
	if (peer->isVerified() && st.verified) {
		const auto iconw = st.verified->width();
		st.verified->paint(
			p,
			rectForName.x() + qMin(nameWidth, rectForName.width() - iconw),
			rectForName.y(),
			outerWidth);
		return iconw;
	} else if ((peer->isScam() || peer->isFake()) && st.scam) {
		const auto phrase = peer->isScam()
			? tr::lng_scam_badge(tr::now)
			: tr::lng_fake_badge(tr::now);
		const auto phraseWidth = st::dialogsScamFont->width(phrase);
		const auto width = st::dialogsScamPadding.left()
			+ phraseWidth
			+ st::dialogsScamPadding.right();
		const auto height = st::dialogsScamPadding.top()
			+ st::dialogsScamFont->height
			+ st::dialogsScamPadding.bottom();
		const auto rect = QRect(
			(rectForName.x()
				+ qMin(
					nameWidth + st::dialogsScamSkip,
					rectForName.width() - width)),
			rectForName.y() + (rectForName.height() - height) / 2,
			width,
			height);
		DrawScamFakeBadge(p, rect, outerWidth, *st.scam, phrase, phraseWidth);
		return st::dialogsScamSkip + width;
	}
	return 0;
}

} // namespace Ui
