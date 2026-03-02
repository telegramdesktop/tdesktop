/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_search_calendar.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_search_controller.h" // PrepareSearchFilter
#include "data/data_session.h"
#include "history/history_item.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/dynamic_thumbnails.h"

namespace Api {

SearchCalendarController::SearchCalendarController(
	not_null<Main::Session*> session,
	PeerId peerId,
	Storage::SharedMediaType type)
: _session(session)
, _peerId(peerId)
, _type(type)
, _api(&session->mtp()) {
}

void SearchCalendarController::monthThumbnails(
		TimeId date,
		Fn<void(std::vector<DayThumbnail>)> onFinish) {
	const auto parsed = base::unixtime::parse(date).date();
	const auto key = MonthKey{
		.peerId = _peerId,
		.year = parsed.year(),
		.month = parsed.month(),
	};

	if (const auto it = _months.find(key); it != _months.end()) {
		if (!it->second.cache.empty()) {
			onFinish(it->second.cache);
			return;
		}
	}

	_months[key].callbacks.push_back(std::move(onFinish));

	if (!_months[key].requestId) {
		performMonthRequest(key);
	}
}

void SearchCalendarController::performMonthRequest(const MonthKey &key) {
	const auto peer = _session->data().peer(key.peerId);
	const auto filter = PrepareSearchFilter(_type);

	const auto parsed = QDate(key.year, key.month, 1);
	const auto endDate = base::unixtime::serialize(QDateTime(
		parsed.addMonths(1).addDays(-1),
		QTime(23, 59, 59)));

	auto &state = _months[key].state;

	_months[key].requestId = _api.request(
		MTPmessages_GetSearchResultsCalendar(
			MTP_flags(0),
			peer->input(),
			MTPInputPeer(),
			filter,
			MTP_int(state.offsetId),
			MTP_int(state.offsetDate ? state.offsetDate : endDate)
	)).done([=](const MTPmessages_SearchResultsCalendar &result) {
		_months[key].requestId = 0;
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		_session->data().processMessages(
			data.vmessages(),
			NewMessageType::Existing);

		auto messageIds = std::vector<FullMsgId>();
		messageIds.reserve(data.vmessages().v.size());
		for (const auto &message : data.vmessages().v) {
			messageIds.push_back(
				FullMsgId(key.peerId, IdFromMessage(message)));
		}

		auto &monthState = _months[key].state;
		const auto prevOffsetId = monthState.offsetId;
		const auto prevOffsetDate = monthState.offsetDate;
		monthState.offsetId = data.vmin_msg_id().v;
		monthState.offsetDate = data.vmin_date().v;

		const auto noMoreData = (prevOffsetId == monthState.offsetId
			&& prevOffsetDate == monthState.offsetDate
			&& prevOffsetId != 0);

		processMonthMessages(
			key,
			messageIds,
			data.vmin_date().v,
			data.vmin_msg_id().v,
			noMoreData);
	}).fail([=] {
		auto &data = _months[key];
		data.requestId = 0;
		data.cache = {};
		for (const auto &callback : data.callbacks) {
			callback({});
		}
		data.callbacks.clear();
	}).send();
}

void SearchCalendarController::processMonthMessages(
		const MonthKey &key,
		const std::vector<FullMsgId> &messages,
		TimeId minDate,
		MsgId minMsgId,
		bool noMoreData) {
	auto result = std::vector<DayThumbnail>();
	auto seenDays = base::flat_set<TimeId>();

	const auto targetMonth = QDate(key.year, key.month, 1);
	const auto targetStart = base::unixtime::serialize(
		QDateTime(targetMonth, QTime()));
	const auto targetEnd = base::unixtime::serialize(QDateTime(
		targetMonth.addMonths(1).addDays(-1),
		QTime(23, 59, 59)));

	for (const auto &fullId : messages) {
		const auto item = _session->data().message(fullId);
		if (!item) {
			continue;
		}

		const auto date = item->date();
		if (date < targetStart || date > targetEnd) {
			continue;
		}

		const auto parsed = base::unixtime::parse(date).date();
		const auto dayStart = base::unixtime::serialize(
			QDateTime(parsed, QTime()));

		if (seenDays.contains(dayStart)) {
			continue;
		}

		const auto media = item->media();
		if (!media) {
			continue;
		}

		auto image = std::shared_ptr<Ui::DynamicImage>();

		if (const auto photo = media->photo()) {
			image = Ui::MakePhotoThumbnail(photo, item->fullId());
		} else if (const auto document = media->document()) {
			if (document->isVideoFile()) {
				image = Ui::MakeDocumentThumbnail(document, item->fullId());
			}
		}

		if (image) {
			seenDays.insert(dayStart);
			result.push_back(DayThumbnail{
				.date = dayStart,
				.image = std::move(image),
				.msgId = fullId.msg,
			});
		}
	}

	if (result.empty()
		&& minDate < targetStart
		&& !_months[key].requestId
		&& !noMoreData) {
		performMonthRequest(key);
	} else {
		auto &data = _months[key];
		data.cache = result;
		for (const auto &callback : data.callbacks) {
			callback(result);
		}
		data.callbacks.clear();
	}
}

std::optional<MsgId> SearchCalendarController::resolveMsgIdByDate(
		TimeId date) const {
	const auto parsed = base::unixtime::parse(date).date();
	const auto key = MonthKey{
		.peerId = _peerId,
		.year = parsed.year(),
		.month = parsed.month(),
	};

	const auto it = _months.find(key);
	if (it == _months.end() || it->second.cache.empty()) {
		return std::nullopt;
	}

	const auto dayStart = base::unixtime::serialize(
		QDateTime(parsed, QTime()));

	for (const auto &thumb : it->second.cache) {
		if (thumb.date == dayStart) {
			return thumb.msgId;
		}
	}

	return std::nullopt;
}

} // namespace Api