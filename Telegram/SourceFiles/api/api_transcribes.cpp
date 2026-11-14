/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_transcribes.h"

#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "spellcheck/spellcheck_types.h"
#include "storage/storage_account.h"

namespace Api {
namespace {

constexpr auto kTrialCooldownPref = "transcription_trial_cooldown_until"_cs;

} // namespace

Transcribes::Transcribes(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance())
, _trialCooldownUntil(_session->local()
	.readPref<QString>(kTrialCooldownPref).toInt())
, _trialCooldownTimer([=] { refreshTrialCooldown(); }) {
	_session->appConfig().value() | rpl::on_next([=] {
		refreshTrialCooldown();
	}, _lifetime);
}

bool Transcribes::isRated(not_null<HistoryItem*> item) const {
	const auto fullId = item->fullId();
	for (const auto &[transcribeId, id] : _ids) {
		if (id == fullId) {
			return _session->settings().isTranscriptionRated(transcribeId);
		}
	}
	return false;
}

void Transcribes::rate(not_null<HistoryItem*> item, bool isGood) {
	const auto fullId = item->fullId();
	for (const auto &[transcribeId, id] : _ids) {
		if (id == fullId) {
			_api.request(MTPmessages_RateTranscribedAudio(
				item->history()->peer->input(),
				MTP_int(item->id),
				MTP_long(transcribeId),
				MTP_bool(isGood))).send();
			_session->settings().markTranscriptionAsRated(transcribeId);
			_session->saveSettings();
			return;
		}
	}
}

bool Transcribes::freeFor(not_null<HistoryItem*> item) const {
	if (const auto channel = item->history()->peer->asMegagroup()) {
		const auto owner = &channel->owner();
		return channel->levelHint() >= owner->groupFreeTranscribeLevel();
	}
	return false;
}

bool Transcribes::trialsSupport() const {
	return trialCooldownUntil() > base::unixtime::now()
		|| _session->appConfig().get<int>(
			u"transcribe_audio_trial_weekly_number"_q,
			0) > 0;
}

TimeId Transcribes::trialCooldownUntil() const {
	return std::max(
		_trialCooldownUntil,
		_session->appConfig().get<int>(
			u"transcribe_audio_trial_cooldown_until"_q,
			0));
}

TimeId Transcribes::trialCooldownUntil(not_null<HistoryItem*> item) const {
	const auto until = trialCooldownUntil();
	return (until > base::unixtime::now()
		&& !_session->premium()
		&& !freeFor(item)
		&& !_map.contains(item->fullId()))
		? until
		: 0;
}

rpl::producer<> Transcribes::trialCooldownChanges() const {
	return _trialCooldownChanges.events();
}

void Transcribes::setTrialCooldownUntil(TimeId until) {
	if (until > _trialCooldownUntil) {
		_trialCooldownUntil = until;
		_session->local().writePref<QString>(
			kTrialCooldownPref,
			QString::number(until));
		refreshTrialCooldown();
	}
}

void Transcribes::refreshTrialCooldown() {
	const auto remaining = int64(trialCooldownUntil()) - base::unixtime::now();
	if (remaining > 0) {
		_trialCooldownTimer.callOnce(std::min(
			crl::time(remaining * 1000),
			crl::time(std::numeric_limits<int>::max())));
	} else {
		_trialCooldownTimer.cancel();
	}
	_trialCooldownChanges.fire({});
}

crl::time Transcribes::trialsMaxLengthMs() const {
	return 1000 * _session->appConfig().get<int>(
		u"transcribe_audio_trial_duration_max"_q,
		300);
}

void Transcribes::toggle(
		not_null<HistoryItem*> item,
		Fn<void(int, TimeId)> showTrial) {
	const auto id = item->fullId();
	const auto i = _map.find(id);
	if (i == _map.end()) {
		if (const auto until = trialCooldownUntil(item)) {
			showTrial(0, until);
			return;
		}
		load(item, std::move(showTrial));
		_session->data().requestItemResize(item);
	} else if (!i->second.requestId) {
		i->second.shown = !i->second.shown;
		if (i->second.roundview) {
			_session->data().requestItemViewRefresh(item);
		}
		_session->data().requestItemResize(item);
	}
}

void Transcribes::toggleSummary(not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	auto i = _summaries.find(id);
	if (i == _summaries.end()) {
		summarize(item);
	} else if (!i->second.loading) {
		auto &entry = i->second;
		if (entry.result.empty()) {
			summarize(item);
		} else {
			entry.shown = entry.premiumRequired ? false : !entry.shown;
			_session->data().requestItemResize(item);
			if (entry.shown) {
				_session->data().requestItemShowHighlight(item);
			}
		}
	}
}

const Transcribes::Entry &Transcribes::entry(
		not_null<HistoryItem*> item) const {
	static auto empty = Entry();
	const auto i = _map.find(item->fullId());
	return (i != _map.end()) ? i->second : empty;
}

const SummaryEntry &Transcribes::summary(
		not_null<const HistoryItem*> item) const {
	static const auto empty = SummaryEntry();
	const auto i = _summaries.find(item->fullId());
	return (i != _summaries.end()) ? i->second : empty;
}

void Transcribes::apply(const MTPDupdateTranscribedAudio &update) {
	const auto id = update.vtranscription_id().v;
	const auto i = _ids.find(id);
	if (i == _ids.end()) {
		return;
	}
	const auto j = _map.find(i->second);
	if (j == _map.end()) {
		return;
	}
	const auto text = qs(update.vtext());
	j->second.result = text;
	j->second.pending = update.is_pending();
	if (const auto item = _session->data().message(i->second)) {
		if (j->second.roundview) {
			_session->data().requestItemViewRefresh(item);
		}
		_session->data().requestItemResize(item);
	}
}

void Transcribes::load(
		not_null<HistoryItem*> item,
		Fn<void(int, TimeId)> showTrial) {
	if (!item->isHistoryEntry() || item->isLocal()) {
		return;
	}
	const auto toggleRound = [](not_null<HistoryItem*> item, Entry &entry) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				if (document->isVideoMessage()) {
					entry.roundview = true;
					document->owner().requestItemViewRefresh(item);
				}
			}
		}
	};
	const auto id = item->fullId();
	const auto usesTrial = !_session->premium() && !freeFor(item);
	const auto requestId = _api.request(MTPmessages_TranscribeAudio(
		item->history()->peer->input(),
		MTP_int(item->id)
	)).done([=](const MTPmessages_TranscribedAudio &result) {
		const auto &data = result.data();

		auto &entry = _map[id];
		entry.requestId = 0;
		entry.pending = data.is_pending();
		entry.result = qs(data.vtext());
		_ids.emplace(data.vtranscription_id().v, id);
		if (const auto item = _session->data().message(id)) {
			toggleRound(item, entry);
			_session->data().requestItemResize(item);
		}
		if (const auto count = data.vtrial_remains_num()) {
			const auto left = count->v;
			const auto until = data.vtrial_remains_until_date()->v;
			const auto now = base::unixtime::now();
			if (until > now
				&& (until > _trialsRefreshAt
					|| (until == _trialsRefreshAt && left < _trialsCount))) {
				_trialsCount = left;
				_trialsRefreshAt = until;
				if (left == 0) {
					setTrialCooldownUntil(until);
					showTrial(0, trialCooldownUntil());
				} else if (trialCooldownUntil() <= now) {
					showTrial(left, until);
				}
			}
		}
	}).fail([=](const MTP::Error &error) {
		const auto floodWaitPrefix = u"FLOOD_WAIT_"_q;
		if (usesTrial && error.type().startsWith(floodWaitPrefix)) {
			const auto seconds = error.type().mid(floodWaitPrefix.size()).toInt();
			if (seconds > 0) {
				const auto until = TimeId(std::min(
					int64(base::unixtime::now()) + seconds,
					int64(std::numeric_limits<TimeId>::max())));
				_map.remove(id);
				if (const auto item = _session->data().message(id)) {
					_session->data().requestItemResize(item);
				}
				setTrialCooldownUntil(until);
				showTrial(0, trialCooldownUntil());
				return;
			}
		}
		auto &entry = _map[id];
		entry.requestId = 0;
		entry.pending = false;
		entry.failed = true;
		if (error.type() == u"MSG_VOICE_TOO_LONG"_q) {
			entry.toolong = true;
		}
		if (const auto item = _session->data().message(id)) {
			toggleRound(item, entry);
			_session->data().requestItemResize(item);
		}
	}).handleFloodErrors().send();
	auto &entry = _map.emplace(id).first->second;
	entry.requestId = requestId;
	entry.shown = true;
	entry.failed = false;
	entry.pending = false;
}

void Transcribes::summarize(not_null<HistoryItem*> item) {
	if (!item->isHistoryEntry() || item->isLocal()) {
		return;
	}

	const auto id = item->fullId();
	const auto translatedTo = item->history()->translatedTo();
	const auto langCode = translatedTo
		? translatedTo.twoLetterCode()
		: QString();
	const auto requestId = _api.request(MTPmessages_SummarizeText(
		langCode.isEmpty()
			? MTP_flags(0)
			: MTP_flags(MTPmessages_summarizeText::Flag::f_to_lang),
		item->history()->peer->input(),
		MTP_int(item->id),
		langCode.isEmpty() ? MTPstring() : MTP_string(langCode),
		MTPstring() // tone
	)).done([=](const MTPTextWithEntities &result) {
		const auto &data = result.data();
		auto &entry = _summaries[id];
		entry.requestId = 0;
		entry.loading = false;
		entry.premiumRequired = false;
		entry.languageId = translatedTo;
		entry.result = TextWithEntities(
			qs(data.vtext()),
			Api::EntitiesFromMTP(_session, data.ventities().v));
		if (const auto item = _session->data().message(id)) {
			_session->data().requestItemTextRefresh(item);
			_session->data().requestItemShowHighlight(item);
		}
	}).fail([=](const MTP::Error &error) {
		auto &entry = _summaries[id];
		if (error.type() == u"SUMMARY_FLOOD_PREMIUM"_q) {
			entry.premiumRequired = true;
		}
		entry.requestId = 0;
		entry.shown = false;
		entry.loading = false;
		if (const auto item = _session->data().message(id)) {
			_session->data().requestItemTextRefresh(item);
		}
	}).send();

	auto &entry = _summaries.emplace(id).first->second;
	entry.requestId = requestId;
	entry.shown = true;
	entry.loading = true;

	item->setHasSummaryEntry();
	_session->data().requestItemResize(item);
}

void Transcribes::checkSummaryToTranslate(FullMsgId id) {
	const auto i = _summaries.find(id);
	if (i == _summaries.end() || i->second.result.empty()) {
		return;
	}
	const auto item = _session->data().message(id);
	if (!item) {
		return;
	}
	const auto translatedTo = item->history()->translatedTo();
	if (i->second.languageId != translatedTo) {
		i->second.result = tr::lng_contacts_loading(tr::now, tr::italic);
		summarize(item);
	}
}

} // namespace Api
