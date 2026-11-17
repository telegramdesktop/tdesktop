/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/sender.h"
#include "spellcheck/spellcheck_types.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

struct SummaryEntry {
	TextWithEntities result;
	LanguageId languageId;
	bool shown = false;
	bool loading = false;
	bool premiumRequired = false;
	mtpRequestId requestId = 0;
};

class Transcribes final {
public:
	explicit Transcribes(not_null<ApiWrap*> api);

	struct Entry {
		QString result;
		bool shown = false;
		bool failed = false;
		bool toolong = false;
		bool pending = false;
		bool roundview = false;
		mtpRequestId requestId = 0;
	};

	void toggle(
		not_null<HistoryItem*> item,
		Fn<void(int, TimeId)> showTrial);
	[[nodiscard]] const Entry &entry(not_null<HistoryItem*> item) const;

	void toggleSummary(not_null<HistoryItem*> item);
	[[nodiscard]] const SummaryEntry &summary(
		not_null<const HistoryItem*> item) const;
	void checkSummaryToTranslate(FullMsgId id);

	void apply(const MTPDupdateTranscribedAudio &update);

	[[nodiscard]] bool freeFor(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool isRated(not_null<HistoryItem*> item) const;
	void rate(not_null<HistoryItem*> item, bool isGood);

	[[nodiscard]] bool trialsSupport() const;
	[[nodiscard]] TimeId trialCooldownUntil(not_null<HistoryItem*> item) const;
	[[nodiscard]] rpl::producer<> trialCooldownChanges() const;
	[[nodiscard]] crl::time trialsMaxLengthMs() const;

private:
	void load(
		not_null<HistoryItem*> item,
		Fn<void(int, TimeId)> showTrial);
	void summarize(not_null<HistoryItem*> item);
	[[nodiscard]] TimeId trialCooldownUntil() const;
	void setTrialCooldownUntil(TimeId until);
	void refreshTrialCooldown();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	int _trialsCount = -1;
	TimeId _trialsRefreshAt = 0;
	TimeId _trialCooldownUntil = 0;
	base::Timer _trialCooldownTimer;
	rpl::event_stream<> _trialCooldownChanges;

	base::flat_map<FullMsgId, Entry> _map;
	base::flat_map<uint64, FullMsgId> _ids;

	base::flat_map<FullMsgId, SummaryEntry> _summaries;
	rpl::lifetime _lifetime;

};

} // namespace Api
