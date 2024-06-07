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

	bool refresh();

	void make(Data::ChatIntro data, bool preview = false);

	int top = 0;
	int height = 0;

private:
	[[nodiscard]] AdminLog::OwnedItem makeAboutBot(not_null<BotInfo*> info);
	[[nodiscard]] AdminLog::OwnedItem makePremiumRequired();
	void makeIntro(not_null<UserData*> user);
	void setItem(AdminLog::OwnedItem item, DocumentData *sticker);
	void setHelloChosen(not_null<DocumentData*> sticker);
	void toggleStickerRegistered(bool registered);

	const not_null<History*> _history;
	const not_null<ElementDelegate*> _delegate;
	AdminLog::OwnedItem _item;
	DocumentData *_helloChosen = nullptr;
	DocumentData *_sticker = nullptr;
	int _version = 0;

};

} // namespace HistoryView
