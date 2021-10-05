/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/chat_style.h"

namespace HistoryView {

struct TextState;

class ViewButton {
public:
	ViewButton(not_null<PeerData*> peer, Fn<void()> updateCallback);
	~ViewButton();

	[[nodiscard]] int height() const;

	void draw(
		Painter &p,
		const QRect &r,
		const Ui::ChatPaintContext &context);

	[[nodiscard]] const ClickHandlerPtr &link() const;
	bool checkLink(const ClickHandlerPtr &other, bool pressed);

	[[nodiscard]] QRect countSponsoredRect(const QRect &r) const;

	[[nodiscard]] bool getState(
		QPoint point,
		const QRect &g,
		not_null<TextState*> outResult) const;

private:
	void resized() const;

	struct Inner;
	const std::unique_ptr<Inner> _inner;
};

} // namespace HistoryView
