/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class DocumentMedia;
} // namespace Data

namespace ChatHelpers {
struct EmojiInteractionPlayRequest;
} // namespace ChatHelpers

namespace Lottie {
class SinglePlayer;
class FrameProvider;
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
	void playPremiumEffect(not_null<const Element*> view);
	void visibleAreaUpdated(int visibleTop, int visibleBottom);

	void paint(QPainter &p);
	[[nodiscard]] rpl::producer<QRect> updateRequests() const;
	[[nodiscard]] rpl::producer<QString> playStarted() const;

private:
	struct Play {
		not_null<const Element*> view;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		QPoint shift;
		int frame = 0;
		int framesCount = 0;
		int frameRate = 0;
		bool premium = false;
		bool started = false;
		bool finished = false;
	};
	struct Delayed {
		QString emoticon;
		not_null<const Element*> view;
		std::shared_ptr<Data::DocumentMedia> media;
		crl::time shouldHaveStartedAt = 0;
		bool incoming = false;
	};

	[[nodiscard]] QRect computeRect(
		not_null<const Element*> view,
		bool premium) const;

	void play(
		QString emoticon,
		not_null<const Element*> view,
		std::shared_ptr<Data::DocumentMedia> media,
		bool incoming);
	void play(
		QString emoticon,
		not_null<const Element*> view,
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool incoming,
		bool premium);
	void checkDelayed();

	[[nodiscard]] QSize sizeFor(bool premium) const;
	[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> preparePlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool premium);

	const not_null<Main::Session*> _session;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	QSize _emojiSize;
	QSize _premiumSize;

	std::vector<Play> _plays;
	std::vector<Delayed> _delayed;
	rpl::event_stream<QRect> _updateRequests;
	rpl::event_stream<QString> _playStarted;
	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<Lottie::FrameProvider>> _sharedProviders;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
