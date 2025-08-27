/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class HistoryItem;
struct HistoryMessageFactcheck;

namespace HistoryView {
class Message;
class WebPage;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Data {

class Factchecks final {
public:
	explicit Factchecks(not_null<Main::Session*> session);

	void requestFor(not_null<HistoryItem*> item);
	[[nodiscard]] std::unique_ptr<HistoryView::WebPage> makeMedia(
		not_null<HistoryView::Message*> view,
		not_null<HistoryMessageFactcheck*> factcheck);

	[[nodiscard]] bool canEdit(not_null<HistoryItem*> item) const;
	[[nodiscard]] int lengthLimit() const;

	void save(
		FullMsgId itemId,
		TextWithEntities text,
		Fn<void(QString)> done);
	void save(
		FullMsgId itemId,
		const TextWithEntities &was,
		TextWithEntities text,
		std::shared_ptr<Ui::Show> show);

private:
	[[nodiscard]] bool canEdit() const;

	void subscribeIfNotYet();
	void request();

	const not_null<Main::Session*> _session;

	base::Timer _requestTimer;
	base::flat_set<not_null<HistoryItem*>> _pending;
	std::vector<HistoryItem*> _requested;
	mtpRequestId _requestId = 0;
	bool _subscribed = false;

	rpl::lifetime _lifetime;

};

} // namespace Data
