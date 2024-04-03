/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "history/history_item.h"
#include "ui/image/image_location.h"
#include "window/window_session_controller_link_info.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

struct SponsoredReportResult final {
	using Id = QByteArray;
	struct Option final {
		Id id = 0;
		QString text;
	};
	using Options = std::vector<Option>;
	enum class FinalStep {
		Hidden,
		Reported,
		Premium,
		Silence,
	};
	Options options;
	QString title;
	QString error;
	FinalStep result;
};

struct SponsoredFrom {
	PeerData *peer = nullptr;
	QString title;
	bool isBroadcast = false;
	bool isMegagroup = false;
	bool isChannel = false;
	bool isPublic = false;
	std::optional<Window::PeerByLinkInfo> botLinkInfo;
	bool isExactPost = false;
	bool isRecommended = false;
	QString externalLink;
	PhotoId webpageOrBotPhotoId = PhotoId(0);
	bool isForceUserpicDisplay = false;
	QString buttonText;
	bool canReport = false;
};

struct SponsoredMessage {
	QByteArray randomId;
	SponsoredFrom from;
	TextWithEntities textWithEntities;
	History *history = nullptr;
	MsgId msgId;
	QString chatInviteHash;
	QString externalLink;
	TextWithEntities sponsorInfo;
	TextWithEntities additionalInfo;
};

class SponsoredMessages final {
public:
	enum class State {
		None,
		AppendToEnd,
		InjectToMiddle,
	};
	struct Details {
		std::optional<QString> hash;
		PeerData *peer = nullptr;
		MsgId msgId;
		std::vector<TextWithEntities> info;
		QString externalLink;
		bool isForceUserpicDisplay = false;
		QString buttonText;
		std::optional<Window::PeerByLinkInfo> botLinkInfo;
		bool canReport = false;
	};
	using RandomId = QByteArray;
	explicit SponsoredMessages(not_null<Session*> owner);
	SponsoredMessages(const SponsoredMessages &other) = delete;
	SponsoredMessages &operator=(const SponsoredMessages &other) = delete;
	~SponsoredMessages();

	[[nodiscard]] bool canHaveFor(not_null<History*> history) const;
	void request(not_null<History*> history, Fn<void()> done);
	void clearItems(not_null<History*> history);
	[[nodiscard]] Details lookupDetails(const FullMsgId &fullId) const;
	void clicked(const FullMsgId &fullId);

	[[nodiscard]] bool append(not_null<History*> history);
	void inject(
		not_null<History*> history,
		MsgId injectAfterMsgId,
		int betweenHeight,
		int fallbackWidth);

	void view(const FullMsgId &fullId);

	[[nodiscard]] State state(not_null<History*> history) const;

	[[nodiscard]] auto createReportCallback(const FullMsgId &fullId)
	-> Fn<void(SponsoredReportResult::Id, Fn<void(SponsoredReportResult)>)>;

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct Entry {
		OwnedItem item;
		FullMsgId itemFullId;
		SponsoredMessage sponsored;
	};
	struct List {
		std::vector<Entry> entries;
		// Data between history displays.
		size_t injectedCount = 0;
		bool showedAll = false;
		//
		crl::time received = 0;
		int postsBetween = 0;
		State state = State::None;
	};
	struct Request {
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};

	void parse(
		not_null<History*> history,
		const MTPmessages_sponsoredMessages &list);
	void append(
		not_null<History*> history,
		List &list,
		const MTPSponsoredMessage &message);
	void clearOldRequests();

	const Entry *find(const FullMsgId &fullId) const;

	const not_null<Main::Session*> _session;

	base::Timer _clearTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	base::flat_map<RandomId, Request> _viewRequests;

	rpl::lifetime _lifetime;

};

} // namespace Data
