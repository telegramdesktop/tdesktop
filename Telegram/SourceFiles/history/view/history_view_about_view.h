/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/admin_log/history_admin_log_item.h"

namespace Data {
struct ChatIntro;
} // namespace Data

namespace HistoryView {

class AboutView final : public ClickHandlerHost {
public:
	AboutView(
		not_null<History*> history,
		not_null<ElementDelegate*> delegate);
	~AboutView();

	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] Element *view() const;
	[[nodiscard]] HistoryItem *item() const;
	[[nodiscard]] bool aboveHistory() const;

	bool refresh();

	void make(Data::ChatIntro data, bool preview = false);

	[[nodiscard]] auto sendIntroSticker() const
		-> rpl::producer<not_null<DocumentData*>>;
	[[nodiscard]] rpl::producer<> refreshRequests() const;
	[[nodiscard]] rpl::lifetime &lifetime();

	int top = 0;
	int height = 0;

private:
	[[nodiscard]] AdminLog::OwnedItem makeAboutVerifyCodes();
	[[nodiscard]] AdminLog::OwnedItem makeAboutBot(not_null<BotInfo*> info);
	[[nodiscard]] AdminLog::OwnedItem makeAboutSimple(
		TextWithEntities textWithEntities,
		DocumentData *document = nullptr,
		PhotoData *photo = nullptr);
	[[nodiscard]] AdminLog::OwnedItem makePremiumRequired();
	[[nodiscard]] AdminLog::OwnedItem makeStarsPerMessage(int stars);
	[[nodiscard]] AdminLog::OwnedItem makeNewPeerInfo(
		not_null<UserData*> user);
	[[nodiscard]] AdminLog::OwnedItem makeBlocked();
	[[nodiscard]] AdminLog::OwnedItem makeNewBotThread();
	void makeIntro(not_null<UserData*> user);
	void setItem(AdminLog::OwnedItem item, DocumentData *sticker);
	void setHelloChosen(not_null<DocumentData*> sticker);
	void toggleStickerRegistered(bool registered);

	void loadCommonGroups();

	const not_null<History*> _history;
	const not_null<ElementDelegate*> _delegate;
	AdminLog::OwnedItem _item;

	DocumentData *_helloChosen = nullptr;
	DocumentData *_sticker = nullptr;
	int _version = 0;

	rpl::event_stream<not_null<DocumentData*>> _sendIntroSticker;

	bool _commonGroupsStale = false;
	bool _commonGroupsRequested = false;
	std::vector<not_null<PeerData*>> _commonGroups;
	rpl::event_stream<> _refreshRequests;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView
