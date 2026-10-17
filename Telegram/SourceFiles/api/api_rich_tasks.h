/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flat_map.h"

class ApiWrap;
class HistoryItem;

namespace Iv {
struct RichPage;
} // namespace Iv

namespace Iv::Markdown {
struct PreparedEditListItemSource;
} // namespace Iv::Markdown

namespace Main {
class Session;
} // namespace Main

namespace Api {

class RichTasks final {
public:
	explicit RichTasks(not_null<ApiWrap*> api);

	[[nodiscard]] bool togglingAllowed(not_null<HistoryItem*> item) const;
	void toggle(
		not_null<HistoryItem*> item,
		const Iv::Markdown::PreparedEditListItemSource &source);

private:
	struct Accumulated {
		std::shared_ptr<const Iv::RichPage> original;
		crl::time scheduled = 0;
		mtpRequestId requestId = 0;
		bool dirty = false;
	};

	void sendAccumulated();
	void send(FullMsgId itemId, Accumulated &entry);
	void finishRequest(FullMsgId itemId, bool failed);

	const not_null<Main::Session*> _session;

	base::flat_map<FullMsgId, Accumulated> _entries;
	base::Timer _sendTimer;

};

} // namespace Api
