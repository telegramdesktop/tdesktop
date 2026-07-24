/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_polls.h"

#include "api/api_common.h"
#include "api/api_statistics_data_deserialize.h"
#include "api/api_text_entities.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/random.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_statistics_chart.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h" // ShouldSendSilent
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_polls.h"
#include "ui/toast/toast.h"
#include "window/window_session_controller.h"

namespace {

constexpr auto kVoteRestrictionToastDuration = 5 * crl::time(1000);

const auto kSubscribersOnlyVoteErrorPatterns = std::array{
	u"POLL_SUBSCRIBERS_ONLY"_q,
	u"POLL_MEMBER_RESTRICTED"_q,
	u"VOTE_SUBSCRIBERS_ONLY"_q,
	u"SUBSCRIBERS_ONLY"_q,
	u"SUBSCRIBER_REQUIRED"_q,
	u"SUBSCRIBER_ONLY"_q,
};

const auto kSubscribersJoinedTooRecentlyVoteErrorPatterns = std::array{
	u"POLL_SUBSCRIBERS_TOO_RECENT"_q,
	u"VOTE_SUBSCRIBERS_TOO_RECENT"_q,
	u"SUBSCRIBERS_TOO_RECENT"_q,
	u"SUBSCRIBER_TOO_RECENT"_q,
	u"JOINED_TOO_RECENTLY"_q,
	u"24_HOURS"_q,
};

const auto kCountriesVoteErrorPatterns = std::array{
	u"POLL_COUNTRIES_ISO2"_q,
	u"VOTE_COUNTRIES_ISO2"_q,
	u"COUNTRIES_ISO2"_q,
	u"COUNTRY_RESTRICTED"_q,
	u"COUNTRY_ISO2"_q,
};

template <size_t Size>
[[nodiscard]] bool MatchesErrorPattern(
		const QString &type,
		const std::array<QString, Size> &patterns) {
	for (const auto &pattern : patterns) {
		if (!pattern.isEmpty()
			&& type.contains(pattern, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] PollData::VoteRestriction ParseVoteRestrictionError(
		const QString &type) {
	if (MatchesErrorPattern(
			type,
			kSubscribersJoinedTooRecentlyVoteErrorPatterns)) {
		return PollData::VoteRestriction::SubscribersJoinedTooRecently;
	} else if (MatchesErrorPattern(
			type,
			kSubscribersOnlyVoteErrorPatterns)) {
		return PollData::VoteRestriction::SubscribersOnly;
	} else if (MatchesErrorPattern(
			type,
			kCountriesVoteErrorPatterns)) {
		return PollData::VoteRestriction::Countries;
	}
	return PollData::VoteRestriction::None;
}

void ShowVoteRestrictionToast(
		not_null<PeerData*> peer,
		not_null<const PollData*> poll,
		PollData::VoteRestriction restriction) {
	if (restriction == PollData::VoteRestriction::None) {
		return;
	}
	auto text = PollVoteRestrictionText(restriction, peer, poll);
	if (text.text.isEmpty()) {
		return;
	}
	if (const auto window = peer->session().tryResolveWindow(peer)) {
		window->showToast({
			.text = std::move(text),
			.iconLottie = u"ban"_q,
			.iconLottieSize = st::pollToastIconSize,
			.duration = kVoteRestrictionToastDuration,
		});
	}
}

#ifdef _DEBUG
[[nodiscard]] Data::StatisticalGraph GenerateMockupPollStats(
		const PollData &poll) {
	auto chart = Data::StatisticalChart();
	const auto colorKeys = std::array<QString, 10>{
		u"BLUE"_q,
		u"GREEN"_q,
		u"RED"_q,
		u"GOLDEN"_q,
		u"LIGHTBLUE"_q,
		u"LIGHTGREEN"_q,
		u"ORANGE"_q,
		u"INDIGO"_q,
		u"PURPLE"_q,
		u"CYAN"_q,
	};

	constexpr auto kPoints = 14;
	constexpr auto kOneDay = float64(24 * 60 * 60 * 1000);
	constexpr auto kStart = float64(1704067200000);
	chart.x.reserve(kPoints);
	for (auto i = 0; i != kPoints; ++i) {
		chart.x.push_back(kStart + i * kOneDay);
	}
	chart.timeStep = kOneDay;

	auto lineId = 0;
	chart.lines.reserve(poll.answers.size());
	for (const auto &answer : poll.answers) {
		auto line = Data::StatisticalChart::Line();
		line.id = ++lineId;
		line.idString = u"answer_%1"_q.arg(line.id);
		line.name = answer.text.text.trimmed();
		if (line.name.isEmpty()) {
			line.name = QString("#%1").arg(line.id);
		}
		line.colorKey = colorKeys[(line.id - 1) % int(colorKeys.size())];
		line.y.reserve(kPoints);

		auto seed = int64(13 * line.id + 17);
		for (const auto byte : answer.option) {
			seed += uchar(byte);
		}
		const auto base = std::max(int64(answer.votes), int64(1));
		for (auto i = 0; i != kPoints; ++i) {
			const auto wave = int64(
				((i + line.id) % 5) * ((i + 2 * line.id) % 4));
			const auto trend = int64((i * (line.id + 1)) / 3);
			const auto noise = int64((seed + i * 7 + line.id * 11) % 6);
			const auto value = std::max(
				base + wave + trend + noise - 2,
				int64(1));
			line.y.push_back(value);
			line.maxValue = std::max(line.maxValue, value);
			line.minValue = std::min(line.minValue, value);
		}
		chart.lines.push_back(std::move(line));
	}
	if (chart.lines.empty()) {
		auto line = Data::StatisticalChart::Line();
		line.id = 1;
		line.idString = u"votes"_q;
		line.name = tr::lng_notification_reactions_poll_votes(tr::now);
		line.colorKey = u"BLUE"_q;
		line.y.reserve(kPoints);

		const auto base = std::max(int64(poll.totalVoters), int64(1));
		for (auto i = 0; i != kPoints; ++i) {
			const auto value = std::max(
				base + i * 2 + ((i * 5) % 7),
				int64(1));
			line.y.push_back(value);
			line.maxValue = std::max(line.maxValue, value);
			line.minValue = std::min(line.minValue, value);
		}
		chart.lines.push_back(std::move(line));
	}

	chart.defaultZoomXIndex = {
		.min = std::max(0, kPoints - 8),
		.max = kPoints - 1,
	};
	chart.measure();
	if (chart.maxValue == chart.minValue) {
		if (chart.minValue) {
			chart.minValue = 0;
		} else {
			chart.maxValue = 1;
		}
	}
	return {
		.chart = std::move(chart),
	};
}
#endif

} // namespace

namespace Api {

Polls::Polls(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void Polls::create(
		const PollData &data,
		const TextWithEntities &text,
		SendAction action,
		Fn<void()> done,
		Fn<void(bool fileReferenceExpired)> fail) {
	StripEphemeralReply(_session, action.replyTo);
	_session->api().sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;
	const auto topicRootId = action.replyTo.messageId
		? action.replyTo.topicRootId
		: 0;
	const auto monoforumPeerId = action.replyTo.monoforumPeerId;
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (action.replyTo) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
	}
	const auto clearCloudDraft = action.clearDraft;
	if (clearCloudDraft) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_clear_draft;
		history->clearLocalDraft(topicRootId, monoforumPeerId);
		history->clearCloudDraft(topicRootId, monoforumPeerId);
		history->startSavingCloudDraft(topicRootId, monoforumPeerId);
	}
	const auto silentPost = ShouldSendSilent(peer, action.options);
	const auto starsPaid = std::min(
		peer->starsPerMessageChecked(),
		action.options.starsApproved);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	if (action.options.scheduled) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
		if (action.options.scheduleRepeatPeriod) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_repeat_period;
		}
	}
	if (action.options.shortcutId) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
	}
	if (action.options.effectId) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_effect;
	}
	if (action.options.suggest) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_suggested_post;
	}
	if (starsPaid) {
		action.options.starsApproved -= starsPaid;
		sendFlags |= MTPmessages_SendMedia::Flag::f_allow_paid_stars;
	}
	const auto sendAs = action.options.sendAs;
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	auto sentEntities = Api::EntitiesToMTP(
		_session,
		text.entities,
		Api::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
	}
	auto &histories = history->owner().histories();
	const auto randomId = base::RandomValue<uint64>();
	histories.sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(sendFlags),
			peer->input(),
			Data::Histories::ReplyToPlaceholder(),
			PollDataToInputMedia(&data),
			MTP_string(text.text),
			MTP_long(randomId),
			MTPReplyMarkup(),
			sentEntities,
			MTP_int(action.options.scheduled),
			MTP_int(action.options.scheduleRepeatPeriod),
			(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(_session, action.options.shortcutId),
			MTP_long(action.options.effectId),
			MTP_long(starsPaid),
			SuggestToMTP(action.options.suggest)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
		if (clearCloudDraft) {
			history->finishSavingCloudDraft(
				topicRootId,
				monoforumPeerId,
				UnixtimeFromMsgId(response.outerMsgId));
		}
		_session->changes().historyUpdated(
			history,
			(action.options.scheduled
				? Data::HistoryUpdate::Flag::ScheduledSent
				: Data::HistoryUpdate::Flag::MessageSent));
		done();
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		if (clearCloudDraft) {
			history->finishSavingCloudDraft(
				topicRootId,
				monoforumPeerId,
				UnixtimeFromMsgId(response.outerMsgId));
		}
		const auto expired = (error.code() == 400)
			&& error.type().startsWith(u"FILE_REFERENCE_"_q);
		fail(expired);
	});
}

void Polls::sendVotes(
		FullMsgId itemId,
		const std::vector<QByteArray> &options) {
	if (_pollVotesRequestIds.contains(itemId)) {
		return;
	}
	const auto item = _session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!item) {
		return;
	}
	const auto peer = item->history()->peer;

	const auto showSending = poll && !options.empty();
	const auto hideSending = [=] {
		if (showSending) {
			if (const auto item = _session->data().message(itemId)) {
				poll->sendingVotes.clear();
				_session->data().requestItemRepaint(item);
			}
		}
	};
	if (showSending) {
		poll->sendingVotes = options;
		_session->data().requestItemRepaint(item);
	} else if (poll && options.empty() && poll->voted()) {
		for (auto &answer : poll->answers) {
			answer.chosen = false;
		}
		++poll->version;
		_session->data().notifyPollUpdateDelayed(poll);
	}

	auto prepared = QVector<MTPbytes>();
	prepared.reserve(options.size());
	ranges::transform(
		options,
		ranges::back_inserter(prepared),
		[](const QByteArray &option) { return MTP_bytes(option); });
	const auto requestId = _api.request(MTPmessages_SendVote(
		peer->input(),
		MTP_int(item->id),
		MTP_vector<MTPbytes>(prepared)
	)).done([=](const MTPUpdates &result) {
		_pollVotesRequestIds.erase(itemId);
		hideSending();
		if (poll) {
			if (poll->voteRestriction() != PollData::VoteRestriction::None) {
				poll->setVoteRestriction(PollData::VoteRestriction::None);
				_session->data().notifyPollUpdateDelayed(poll);
			}
		}
		_session->updates().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		_pollVotesRequestIds.erase(itemId);
		hideSending();
		if (poll) {
			const auto restriction = ParseVoteRestrictionError(error.type());
			if (restriction != PollData::VoteRestriction::None) {
				poll->setVoteRestriction(restriction);
				_session->data().notifyPollUpdateDelayed(poll);
				if (const auto item = _session->data().message(itemId)) {
					_session->data().requestItemResize(item);
				}
				ShowVoteRestrictionToast(peer, poll, restriction);
			}
		}
	}).send();
	_pollVotesRequestIds.emplace(itemId, requestId);
}

void Polls::addAnswer(
		FullMsgId itemId,
		const TextWithEntities &text,
		const PollMedia &media,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	if (_pollAddAnswerRequestIds.contains(itemId)) {
		return;
	}
	const auto item = _session->data().message(itemId);
	if (!item) {
		return;
	}
	const auto sentEntities = Api::EntitiesToMTP(
		_session,
		text.entities,
		Api::ConvertOption::SkipLocal);
	using Flag = MTPDinputPollAnswer::Flag;
	const auto flags = media
		? Flag::f_media
		: Flag();
	const auto answer = MTP_inputPollAnswer(
		MTP_flags(flags),
		MTP_textWithEntities(
			MTP_string(text.text),
			sentEntities),
		media ? PollMediaToMTP(media) : MTPInputMedia());
	const auto requestId = _api.request(MTPmessages_AddPollAnswer(
		item->history()->peer->input(),
		MTP_int(item->id),
		answer
	)).done([=](const MTPUpdates &result) {
		_pollAddAnswerRequestIds.erase(itemId);
		_session->updates().applyUpdates(result);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		_pollAddAnswerRequestIds.erase(itemId);
		if (fail) {
			fail(error.type());
		}
	}).send();
	_pollAddAnswerRequestIds.emplace(itemId, requestId);
}

void Polls::deleteAnswer(FullMsgId itemId, const QByteArray &option) {
	if (_pollVotesRequestIds.contains(itemId)) {
		return;
	}
	const auto item = _session->data().message(itemId);
	if (!item) {
		return;
	}
	const auto requestId = _api.request(MTPmessages_DeletePollAnswer(
		item->history()->peer->input(),
		MTP_int(item->id),
		MTP_bytes(option)
	)).done([=](const MTPUpdates &result) {
		_pollVotesRequestIds.erase(itemId);
		_session->updates().applyUpdates(result);
	}).fail([=] {
		_pollVotesRequestIds.erase(itemId);
	}).send();
	_pollVotesRequestIds.emplace(itemId, requestId);
}

void Polls::close(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	if (_pollCloseRequestIds.contains(itemId)) {
		return;
	}
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!poll) {
		return;
	}
	const auto requestId = _api.request(MTPmessages_EditMessage(
		MTP_flags(MTPmessages_EditMessage::Flag::f_media),
		item->history()->peer->input(),
		MTP_int(item->id),
		MTPstring(),
		PollDataToInputMedia(poll, true),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTP_int(0), // schedule_date
		MTP_int(0), // schedule_repeat_period
		MTPint(), // quick_reply_shortcut_id
		MTPInputRichMessage()
	)).done([=](const MTPUpdates &result) {
		_pollCloseRequestIds.erase(itemId);
		_session->updates().applyUpdates(result);
	}).fail([=] {
		_pollCloseRequestIds.erase(itemId);
	}).send();
	_pollCloseRequestIds.emplace(itemId, requestId);
}

void Polls::reloadResults(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	if (!item->isRegular() || _pollReloadRequestIds.contains(itemId)) {
		return;
	}
	const auto media = item->media();
	const auto poll = media ? media->poll() : nullptr;
	const auto pollHash = poll ? poll->hash : uint64(0);
	const auto requestId = _api.request(MTPmessages_GetPollResults(
		item->history()->peer->input(),
		MTP_int(item->id),
		MTP_long(pollHash)
	)).done([=](const MTPUpdates &result) {
		_pollReloadRequestIds.erase(itemId);
		_session->updates().applyUpdates(result);
	}).fail([=] {
		_pollReloadRequestIds.erase(itemId);
	}).send();
	_pollReloadRequestIds.emplace(itemId, requestId);
}

void Polls::requestStats(
		FullMsgId itemId,
		Fn<void(Data::StatisticalGraph)> done,
		Fn<void(QString)> fail) {
	const auto item = _session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!item || !item->isRegular() || !poll) {
		if (fail) {
			fail(QString());
		}
		return;
	}
#ifdef _DEBUG
	if (base::IsCtrlPressed()) {
		auto callback = std::move(done);
		if (callback) {
			constexpr auto kMockupStatsDelay = 2 * crl::time(1000);
			auto graph = GenerateMockupPollStats(*poll);
			base::call_delayed(kMockupStatsDelay, _session, [=]() mutable {
				callback(std::move(graph));
			});
		}
		return;
	}
#endif
	const auto requestGraph = [=](const QString &token) {
		_api.request(MTPstats_LoadAsyncGraph(
			MTP_flags(MTPstats_LoadAsyncGraph::Flag(0)),
			MTP_string(token),
			MTP_long(0)
		)).done([=](const MTPStatsGraph &result) {
			if (done) {
				done(Api::StatisticalGraphFromTL(result));
			}
		}).fail([=](const MTP::Error &error) {
			if (fail) {
				fail(error.type());
			}
		}).send();
	};
	_api.request(MTPstats_GetPollStats(
		MTP_flags(MTPstats_GetPollStats::Flags(0)),
		item->history()->peer->input(),
		MTP_int(item->id)
	)).done([=](const MTPstats_PollStats &result) {
		auto graph = Api::StatisticalGraphFromTL(result.data().vvotes_graph());
		if (graph.chart || !graph.error.isEmpty() || graph.zoomToken.isEmpty()) {
			if (done) {
				done(std::move(graph));
			}
		} else {
			requestGraph(graph.zoomToken);
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

} // namespace Api
