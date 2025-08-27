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

namespace Api {
struct SponsoredSearchResult;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Data {

class MediaPreload;

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
	QString title;
	QString link;
	QString buttonText;
	PhotoId photoId = PhotoId(0);
	PhotoId mediaPhotoId = PhotoId(0);
	DocumentId mediaDocumentId = DocumentId(0);
	uint64 backgroundEmojiId = 0;
	uint8 colorIndex : 6 = 0;
	bool isLinkInternal = false;
	bool isRecommended = false;
	bool canReport = false;
};

struct SponsoredMessage {
	QByteArray randomId;
	SponsoredFrom from;
	TextWithEntities textWithEntities;
	not_null<History*> history;
	QString link;
	TextWithEntities sponsorInfo;
	TextWithEntities additionalInfo;
	crl::time durationMin = 0;
	crl::time durationMax = 0;
};

struct SponsoredMessageDetails {
	std::vector<TextWithEntities> info;
	QString link;
	QString buttonText;
	PhotoId photoId = PhotoId(0);
	PhotoId mediaPhotoId = PhotoId(0);
	DocumentId mediaDocumentId = DocumentId(0);
	uint64 backgroundEmojiId = 0;
	uint8 colorIndex : 6 = 0;
	bool isLinkInternal = false;
	bool canReport = false;
};

struct SponsoredReportAction {
	Fn<void(
		Data::SponsoredReportResult::Id,
		Fn<void(Data::SponsoredReportResult)>)> callback;
};

struct SponsoredForVideoState {
	int itemIndex = 0;
	crl::time leftTillShow = 0;

	[[nodiscard]] bool initial() const {
		return !itemIndex && !leftTillShow;
	}
};

struct SponsoredForVideo {
	std::vector<SponsoredMessage> list;
	crl::time startDelay = 0;
	crl::time betweenDelay = 0;

	SponsoredForVideoState state;
};

class SponsoredMessages final {
public:
	enum class AppendResult {
		None,
		Appended,
		MediaLoading,
	};
	enum class State {
		None,
		AppendToEnd,
		InjectToMiddle,
		AppendToTopBar,
	};
	using Details = SponsoredMessageDetails;
	using RandomId = QByteArray;
	explicit SponsoredMessages(not_null<Main::Session*> session);
	~SponsoredMessages();

	[[nodiscard]] bool canHaveFor(not_null<History*> history) const;
	[[nodiscard]] bool canHaveFor(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool isTopBarFor(not_null<History*> history) const;
	void request(not_null<History*> history, Fn<void()> done);
	void requestForVideo(
		not_null<HistoryItem*> item,
		Fn<void(SponsoredForVideo)> done);
	void updateForVideo(
		FullMsgId itemId,
		SponsoredForVideoState state);
	void clearItems(not_null<History*> history);
	[[nodiscard]] Details lookupDetails(const FullMsgId &fullId) const;
	[[nodiscard]] Details lookupDetails(const SponsoredMessage &data) const;
	[[nodiscard]] Details lookupDetails(
		const Api::SponsoredSearchResult &data) const;
	void clicked(const FullMsgId &fullId, bool isMedia, bool isFullscreen);
	void clicked(
		const QByteArray &randomId,
		bool isMedia,
		bool isFullscreen);
	[[nodiscard]] FullMsgId fillTopBar(
		not_null<History*> history,
		not_null<Ui::RpWidget*> widget);
	[[nodiscard]] rpl::producer<> itemRemoved(const FullMsgId &);

	[[nodiscard]] AppendResult append(not_null<History*> history);
	void inject(
		not_null<History*> history,
		MsgId injectAfterMsgId,
		int betweenHeight,
		int fallbackWidth);

	void view(const FullMsgId &fullId);
	void view(const QByteArray &randomId);

	[[nodiscard]] State state(not_null<History*> history) const;

	[[nodiscard]] SponsoredReportAction createReportCallback(
		const FullMsgId &fullId);
	[[nodiscard]] SponsoredReportAction createReportCallback(
		const QByteArray &randomId,
		Fn<void()> erase);

	void clear();

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct Entry {
		OwnedItem item;
		FullMsgId itemFullId;
		SponsoredMessage sponsored;
		std::unique_ptr<MediaPreload> preload;
		std::unique_ptr<rpl::lifetime> optionalDestructionNotifier;
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
	struct ListForVideo {
		std::vector<Entry> entries;
		crl::time received = 0;
		crl::time startDelay = 0;
		crl::time betweenDelay = 0;
		SponsoredForVideoState state;
	};
	struct Request {
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};
	struct RequestForVideo {
		std::vector<Fn<void(SponsoredForVideo)>> callbacks;
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};

	void parse(
		not_null<History*> history,
		const MTPmessages_sponsoredMessages &list);
	void parseForVideo(
		not_null<PeerData*> peer,
		const MTPmessages_sponsoredMessages &list);
	void append(
		Fn<not_null<std::vector<Entry>*>()> entries,
		not_null<History*> history,
		const MTPSponsoredMessage &message);
	[[nodiscard]] SponsoredForVideo prepareForVideo(
		not_null<PeerData*> peer);
	void clearOldRequests();

	const Entry *find(const FullMsgId &fullId) const;

	const not_null<Main::Session*> _session;

	base::Timer _clearTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	base::flat_map<RandomId, Request> _viewRequests;

	base::flat_map<not_null<PeerData*>, ListForVideo> _dataForVideo;
	base::flat_map<not_null<PeerData*>, RequestForVideo> _requestsForVideo;

	rpl::event_stream<FullMsgId> _itemRemoved;

	rpl::lifetime _lifetime;

};

} // namespace Data
