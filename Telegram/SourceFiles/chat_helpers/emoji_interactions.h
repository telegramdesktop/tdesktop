/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

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
	not_null<HistoryItem*> item;
	std::shared_ptr<Data::DocumentMedia> media;
};

class EmojiInteractions final {
public:
	explicit EmojiInteractions(not_null<Main::Session*> session);
	~EmojiInteractions();

	using PlayRequest = EmojiInteractionPlayRequest;

	void start(not_null<const HistoryView::Element*> view);
	[[nodiscard]] rpl::producer<PlayRequest> playRequests() const {
		return _playRequests.events();
	}

private:
	struct Animation {
		EmojiPtr emoji;
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> media;
		crl::time scheduledAt = 0;
		crl::time startedAt = 0;
		int index = 0;
	};
	struct CheckResult {
		crl::time nextCheckAt = 0;
		bool waitingForDownload = false;
	};
	[[nodiscard]] static CheckResult Combine(CheckResult a, CheckResult b);

	void check(crl::time now = 0);
	[[nodiscard]] CheckResult checkAnimations(crl::time now);
	[[nodiscard]] CheckResult checkAccumulated(crl::time now);
	void sendAccumulated(
		crl::time now,
		not_null<HistoryItem*> item,
		std::vector<Animation> &animations);
	void setWaitingForDownload(bool waiting);

	const not_null<Main::Session*> _session;

	base::flat_map<
		not_null<HistoryItem*>,
		std::vector<Animation>> _animations;
	base::Timer _checkTimer;
	rpl::event_stream<PlayRequest> _playRequests;

	bool _waitingForDownload = false;
	rpl::lifetime _downloadCheckLifetime;

	rpl::lifetime _lifetime;

};

} // namespace ChatHelpers
