/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ApiWrap;
class PeerData;
class ChannelData;

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Api {

class UnreadThings final {
public:
	using DialogsEntry = Dialogs::Entry;

	explicit UnreadThings(not_null<ApiWrap*> api);

	[[nodiscard]] bool trackMentions(DialogsEntry *entry) const;
	[[nodiscard]] bool trackReactions(DialogsEntry *entry) const;

	void preloadEnough(DialogsEntry *entry);

	void mediaAndMentionsRead(
		const base::flat_set<MsgId> &readIds,
		ChannelData *channel = nullptr);

	void cancelRequests(not_null<DialogsEntry*> entry);

private:
	void preloadEnoughMentions(not_null<DialogsEntry*> entry);
	void preloadEnoughReactions(not_null<DialogsEntry*> entry);

	void requestMentions(not_null<DialogsEntry*> entry, int loaded);
	void requestReactions(not_null<DialogsEntry*> entry, int loaded);

	const not_null<ApiWrap*> _api;

	base::flat_map<not_null<DialogsEntry*>, mtpRequestId> _mentionsRequests;
	base::flat_map<not_null<DialogsEntry*>, mtpRequestId> _reactionsRequests;

};

} // namespace Api
