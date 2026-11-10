/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/sender.h"

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace twidget {
struct WidgetRequestOutcome;
} // namespace twidget

namespace Window {
class SessionController;
} // namespace Window

namespace Tgw {

class BotQueries final {
public:
	using Respond = Fn<void(const twidget::WidgetRequestOutcome&)>;

	explicit BotQueries(not_null<Main::Session*> session);
	~BotQueries();

	void send(
		not_null<PeerData*> peer,
		MsgId msgId,
		const MTPWidgetQueryType &type,
		const std::string &data,
		Respond respond);

private:
	struct Pending {
		Respond respond;
		crl::time deadline = 0;
	};

	void finish(
		mtpRequestId id,
		const twidget::WidgetRequestOutcome &outcome);
	void checkDeadlines();

	MTP::Sender _api;
	base::flat_map<mtpRequestId, Pending> _pending;
	base::Timer _deadlines;

};

class Bridge final {
public:
	using Respond = BotQueries::Respond;

	explicit Bridge(not_null<HistoryItem*> item);

	[[nodiscard]] QString openLink(const QString &url, bool telegram);
	[[nodiscard]] QString copyText(
		const QString &text,
		not_null<QWidget*> window);
	void request(const std::string &data, bool wallet, Respond respond);

private:
	[[nodiscard]] Window::SessionController *findController() const;

	const not_null<HistoryItem*> _item;
	BotQueries _queries;

};

[[nodiscard]] Window::SessionController *WindowController(
	not_null<QWidget*> window,
	not_null<Main::Session*> session);

} // namespace Tgw
