/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/text/text_entity.h"

namespace Main {
class Session;
} // namespace Main

namespace MTP {
class Error;
} // namespace MTP

namespace Data {

struct AiComposeToneExample {
	TextWithEntities from;
	TextWithEntities to;
};

struct AiComposeTone {
	uint64 id = 0;
	uint64 accessHash = 0;
	QString slug;
	QString title;
	DocumentId emojiId = 0;
	QString prompt;
	int installsCount = 0;
	UserId authorId = 0;
	bool creator = false;
	bool isDefault = false;
	QString defaultType;
	std::optional<AiComposeToneExample> firstExample;
};

class AiComposeTones final {
public:
	explicit AiComposeTones(not_null<Main::Session*> session);

	void refresh();
	[[nodiscard]] const std::vector<AiComposeTone> &list() const;
	[[nodiscard]] rpl::producer<> updated() const;

	void create(
		const QString &title,
		const QString &prompt,
		DocumentId emojiId,
		bool displayAuthor,
		Fn<void(AiComposeTone)> done,
		Fn<void(const MTP::Error &)> fail = nullptr);
	void update(
		const AiComposeTone &tone,
		std::optional<QString> title,
		std::optional<QString> prompt,
		std::optional<DocumentId> emojiId,
		std::optional<bool> displayAuthor,
		Fn<void(AiComposeTone)> done,
		Fn<void(const MTP::Error &)> fail = nullptr);
	void save(
		const AiComposeTone &tone,
		bool unsave,
		Fn<void()> done = nullptr,
		Fn<void(const MTP::Error &)> fail = nullptr);
	void remove(
		const AiComposeTone &tone,
		Fn<void()> done = nullptr);
	void resolve(
		const QString &slug,
		Fn<void(AiComposeTone)> done,
		Fn<void(const MTP::Error &)> fail = nullptr);
	void getToneExample(
		const AiComposeTone &tone,
		int num,
		Fn<void(AiComposeToneExample)> done,
		Fn<void(const MTP::Error &)> fail = nullptr);

	void applyUpdate();

	[[nodiscard]] MTPInputAiComposeTone toneToMTP(
		const AiComposeTone &tone) const;

private:
	void parseTones(const QVector<MTPAiComposeTone> &list);
	[[nodiscard]] AiComposeTone parseTone(
		const MTPAiComposeTone &tone) const;
	void finishRefresh();
	void promoteCustomTone(AiComposeTone tone);
	void removeCustomTone(uint64 id);
	void rememberRecentCustomTone(uint64 id);
	void forgetRecentCustomTone(uint64 id);
	void reapplyRecentCustomToneOrder();
	void refreshWithHash(uint64 hash);

	enum class PendingRefresh {
		None,
		Incremental,
		Full,
	};

	const not_null<Main::Session*> _session;
	uint64 _hash = 0;
	mtpRequestId _refreshRequestId = 0;
	std::vector<AiComposeTone> _list;
	std::vector<uint64> _recentCustomToneIds;
	rpl::event_stream<> _updates;
	base::Timer _refreshTimer;
	PendingRefresh _pendingRefresh = PendingRefresh::None;

};

} // namespace Data
