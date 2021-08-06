/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;
class UserData;

namespace Bot {

struct SendCommandRequest {
	not_null<PeerData*> peer;
	QString command;
	FullMsgId context;
	int replyTo = 0;
};

[[nodiscard]] QString WrapCommandInChat(
	not_null<PeerData*> peer,
	const QString &command,
	const FullMsgId &context);
[[nodiscard]] QString WrapCommandInChat(
	not_null<PeerData*> peer,
	const QString &command,
	not_null<UserData*> bot);

} // namespace Bot
