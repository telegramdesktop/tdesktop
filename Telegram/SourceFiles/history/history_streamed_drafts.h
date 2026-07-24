/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"

#include <memory>

class History;
class MTPDmessage;
class MTPDsendMessageRichMessageDraftAction;
class MTPDsendMessageTextDraftAction;

namespace Iv {
struct RichPage;
} // namespace Iv

class HistoryStreamedDrafts final : public base::has_weak_ptr {
public:
	explicit HistoryStreamedDrafts(not_null<History*> history);
	~HistoryStreamedDrafts();

	[[nodiscard]] rpl::producer<> destroyRequests() const;

	void apply(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		const MTPDsendMessageTextDraftAction &data);
	void apply(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		const MTPDsendMessageRichMessageDraftAction &data);

	[[nodiscard]] bool hasFor(not_null<HistoryItem*> item) const;
	void applyItemRemoved(not_null<HistoryItem*> item);
	HistoryItem *adoptIncoming(const MTPDmessage &data);

private:
	enum class DraftKind {
		Text,
		Rich,
	};

	struct DraftContent {
		TextWithEntities text;
		std::shared_ptr<const Iv::RichPage> richPage;
		QString matchText;
		DraftKind kind = DraftKind::Text;
	};

	struct Draft {
		not_null<HistoryItem*> message;
		MsgId rootId = 0;
		PeerId fromId = 0;
		crl::time updated = 0;
		DraftKind kind = DraftKind::Text;
		QString matchText;
	};

	bool update(uint64 randomId, DraftContent &&content);
	void applyPrepared(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		uint64 randomId,
		DraftContent &&content);
	[[nodiscard]] DraftContent prepareContent(
		const MTPDsendMessageTextDraftAction &data);
	[[nodiscard]] DraftContent prepareContent(
		const MTPDsendMessageRichMessageDraftAction &data);
	void clearByRandomId(uint64 randomId);

	void check();
	void scheduleDestroy();

	[[nodiscard]] TextWithEntities loadingEmoji();

	const not_null<History*> _history;
	base::flat_map<uint64, Draft> _drafts;

	base::Timer _checkTimer;

	rpl::event_stream<> _destroyRequests;

	TextWithEntities _loadingEmoji;

};
