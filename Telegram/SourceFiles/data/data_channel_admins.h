/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Data {

class ChannelAdminChanges {
public:
	ChannelAdminChanges(not_null<ChannelData*> channel);

	void add(UserId userId, const QString &rank);
	void remove(UserId userId);

	~ChannelAdminChanges();

private:
	not_null<ChannelData*> _channel;
	base::flat_map<UserId, QString> &_admins;
	base::flat_set<UserId> _changes;

};

} // namespace Data
