/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
struct EmojiInteractionPlayRequest;
} // namespace ChatHelpers

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {

class Element;

class EmojiInteractions final {
public:
	explicit EmojiInteractions(not_null<Main::Session*> session);
	~EmojiInteractions();

	void play(
		ChatHelpers::EmojiInteractionPlayRequest request,
		not_null<Element*> view);
	void visibleAreaUpdated(int visibleTop, int visibleBottom);

	void paint(QPainter &p);
	[[nodiscard]] rpl::producer<QRect> updateRequests() const;

private:
	struct Play {
		not_null<Element*> view;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		QPoint shift;
		bool finished = false;
	};

	[[nodiscard]] QRect computeRect(not_null<Element*> view) const;

	const not_null<Main::Session*> _session;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	QSize _emojiSize;

	std::vector<Play> _plays;
	rpl::event_stream<QRect> _updateRequests;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
