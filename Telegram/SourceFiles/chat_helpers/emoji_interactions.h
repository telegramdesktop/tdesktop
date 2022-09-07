/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class PeerData;
class HistoryItem;
class DocumentData;

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace ChatHelpers {

struct EmojiInteractionPlayRequest {
	QString emoticon;
	not_null<HistoryItem*> item;
	std::shared_ptr<Data::DocumentMedia> media;
	crl::time shouldHaveStartedAt = 0;
	bool incoming = false;
};

struct EmojiInteractionsBunch {
	struct Single {
		int index = 0;
		double time = 0;
	};
	std::vector<Single> interactions;
};

struct EmojiInteractionSeen {
	not_null<PeerData*> peer;
	QString emoticon;
};

class EmojiInteractions final {
public:
	explicit EmojiInteractions(not_null<Main::Session*> session);
	~EmojiInteractions();

	using PlayRequest = EmojiInteractionPlayRequest;

	void startOutgoing(not_null<const HistoryView::Element*> view);
	void startIncoming(
		not_null<PeerData*> peer,
		MsgId messageId,
		const QString &emoticon,
		EmojiInteractionsBunch &&bunch);

	void seenOutgoing(not_null<PeerData*> peer, const QString &emoticon);
	[[nodiscard]] rpl::producer<EmojiInteractionSeen> seen() const {
		return _seen.events();
	}

	[[nodiscard]] rpl::producer<PlayRequest> playRequests() const {
		return _playRequests.events();
	}
	void playStarted(not_null<PeerData*> peer, QString emoji);

	[[nodiscard]] static EmojiInteractionsBunch Parse(const QByteArray &json);
	[[nodiscard]] static QByteArray ToJson(
		const EmojiInteractionsBunch &bunch);

private:
	struct Animation {
		QString emoticon;
		not_null<EmojiPtr> emoji;
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> media;
		crl::time scheduledAt = 0;
		crl::time startedAt = 0;
		bool incoming = false;
		int index = 0;
	};
	struct PlaySent {
		mtpRequestId lastRequestId = 0;
		crl::time lastDoneReceivedAt = 0;
	};
	struct CheckResult {
		crl::time nextCheckAt = 0;
		bool waitingForDownload = false;
	};
	[[nodiscard]] static CheckResult Combine(CheckResult a, CheckResult b);

	void check(crl::time now = 0);
	[[nodiscard]] CheckResult checkAnimations(crl::time now);
	[[nodiscard]] CheckResult checkAnimations(
		crl::time now,
		base::flat_map<not_null<HistoryItem*>, std::vector<Animation>> &map);
	[[nodiscard]] CheckResult checkAccumulated(crl::time now);
	void sendAccumulatedOutgoing(
		crl::time now,
		not_null<HistoryItem*> item,
		std::vector<Animation> &animations);
	void clearAccumulatedIncoming(
		crl::time now,
		std::vector<Animation> &animations);
	void setWaitingForDownload(bool waiting);

	void checkSeenRequests(crl::time now);
	void checkSentRequests(crl::time now);
	void checkEdition(
		not_null<HistoryItem*> item,
		base::flat_map<not_null<HistoryItem*>, std::vector<Animation>> &map);

	const not_null<Main::Session*> _session;

	base::flat_map<not_null<HistoryItem*>, std::vector<Animation>> _outgoing;
	base::flat_map<not_null<HistoryItem*>, std::vector<Animation>> _incoming;
	base::Timer _checkTimer;
	rpl::event_stream<PlayRequest> _playRequests;
	base::flat_map<
		not_null<PeerData*>,
		base::flat_map<QString, crl::time>> _playStarted;
	base::flat_map<
		not_null<PeerData*>,
		base::flat_map<not_null<EmojiPtr>, PlaySent>> _playsSent;
	rpl::event_stream<EmojiInteractionSeen> _seen;

	bool _waitingForDownload = false;
	rpl::lifetime _downloadCheckLifetime;

	rpl::lifetime _lifetime;

};

} // namespace ChatHelpers
