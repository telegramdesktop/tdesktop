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
namespace {

[[nodiscard]] TimeId DayStartOf(TimeId date) {
	return base::unixtime::serialize(
		QDateTime(base::unixtime::parse(date).date(), QTime()));
}

} // namespace

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
		if (it->second.loaded) {
			onFinish(thumbnails(it->second));
			return;
		}
	}

	auto &data = _months[key];
	data.callbacks.push_back(std::move(onFinish));

	if (!data.requestId && !data.deferred) {
		const auto newest = requestingNewest();
		if (newest && key < *newest) {
			data.deferred = true;
		} else {
			performMonthRequest(key);
		}
	}
}

std::optional<SearchCalendarController::MonthKey>
SearchCalendarController::requestingNewest() const {
	auto result = std::optional<MonthKey>();
	for (const auto &[key, data] : _months) {
		if (data.requestId && (!result || *result < key)) {
			result = key;
		}
	}
	return result;
}

void SearchCalendarController::sendDeferredRequests() {
	auto keys = std::vector<MonthKey>();
	for (const auto &[key, data] : _months) {
		if (data.deferred && !data.loaded && !data.requestId) {
			keys.push_back(key);
		}
	}
	for (const auto &key : keys) {
		auto &data = _months[key];
		if (data.loaded || data.requestId) {
			continue;
		}
		data.deferred = false;
		performMonthRequest(key);
	}
}

void SearchCalendarController::performMonthRequest(const MonthKey &key) {
	const auto peer = _session->data().peer(key.peerId);
	const auto filter = PrepareSearchFilter(_type);

	const auto month = QDate(key.year, key.month, 1);
	const auto endDate = base::unixtime::serialize(QDateTime(
		month.addMonths(1).addDays(-1),
		QTime(23, 59, 59)));

	auto &data = _months[key];
	const auto usedDate = data.state.offsetDate
		? data.state.offsetDate
		: endDate;

	data.requestId = _api.request(
		MTPmessages_GetSearchResultsCalendar(
			MTP_flags(0),
			peer->input(),
			MTPInputPeer(),
			filter,
			MTP_int(data.state.offsetId),
			MTP_int(usedDate)
	)).done([=](const MTPmessages_SearchResultsCalendar &result) {
		auto &data = _months[key];
		data.requestId = 0;
		const auto &fields = result.data();
		_session->data().processUsers(fields.vusers());
		_session->data().processChats(fields.vchats());
		_session->data().processMessages(
			fields.vmessages(),
			NewMessageType::Existing);

		auto messageIds = std::vector<FullMsgId>();
		messageIds.reserve(fields.vmessages().v.size());
		for (const auto &message : fields.vmessages().v) {
			messageIds.push_back(
				FullMsgId(key.peerId, IdFromMessage(message)));
		}

		auto periods = std::vector<CalendarPeriod>();
		periods.reserve(fields.vperiods().v.size());
		for (const auto &period : fields.vperiods().v) {
			const auto &periodFields = period.data();
			periods.push_back(CalendarPeriod{
				.date = periodFields.vdate().v,
				.minMsgId = periodFields.vmin_msg_id().v,
				.maxMsgId = periodFields.vmax_msg_id().v,
				.count = periodFields.vcount().v,
			});
		}

		const auto prevOffsetId = data.state.offsetId;
		const auto prevOffsetDate = data.state.offsetDate;
		data.state.offsetId = fields.vmin_msg_id().v;
		data.state.offsetDate = fields.vmin_date().v;

		const auto noMoreData = !data.state.offsetId
			|| (prevOffsetId == data.state.offsetId
				&& prevOffsetDate == data.state.offsetDate);

		processMonthData(
			key,
			periods,
			messageIds,
			fields.vmin_date().v,
			usedDate,
			noMoreData);
	}).fail([=] {
		auto &data = _months[key];
		data.requestId = 0;
		finishMonth(data);
		sendDeferredRequests();
	}).send();
}

auto SearchCalendarController::collectDayMedia(
	const std::vector<FullMsgId> &messages) const
-> base::flat_map<TimeId, MonthDay> {
	auto result = base::flat_map<TimeId, MonthDay>();
	for (const auto &fullId : messages) {
		const auto item = _session->data().message(fullId);
		if (!item) {
			continue;
		}
		const auto dayStart = DayStartOf(item->date());
		if (result.contains(dayStart)) {
			continue;
		}
		const auto media = item->media();
		if (!media) {
			continue;
		}
		auto day = MonthDay{ .origin = item->fullId() };
		if (const auto photo = media->photo()) {
			day.photo = photo;
		} else if (const auto document = media->document()) {
			if (document->isVideoFile()) {
				day.document = document;
			}
		}
		if (day.photo || day.document) {
			result.emplace(dayStart, day);
		}
	}
	return result;
}

void SearchCalendarController::fillMonth(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const base::flat_map<TimeId, MonthDay> &dayMedia) {
	// Periods are authoritative: they provide the newest message of each day.
	auto &data = _months[key];
	auto seenDays = base::flat_set<TimeId>();
	for (const auto &day : data.cache) {
		seenDays.emplace(day.date);
	}
	for (const auto &period : periods) {
		const auto parsed = base::unixtime::parse(period.date).date();
		if (!period.maxMsgId
			|| parsed.year() != key.year
			|| parsed.month() != key.month) {
			continue;
		}
		const auto dayStart = DayStartOf(period.date);
		if (seenDays.contains(dayStart)) {
			continue;
		}
		const auto i = dayMedia.find(dayStart);
		if (i == dayMedia.end()) {
			continue;
		}
		seenDays.emplace(dayStart);
		auto day = i->second;
		day.date = dayStart;
		day.msgId = period.maxMsgId;
		data.cache.push_back(day);
	}
}

void SearchCalendarController::fillCoveredMonths(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const base::flat_map<TimeId, MonthDay> &dayMedia,
		TimeId offsetDate,
		bool noMoreData) {
	auto oldest = TimeId();
	auto months = base::flat_set<QDate>();
	for (const auto &period : periods) {
		if (!oldest || period.date < oldest) {
			oldest = period.date;
		}
		const auto parsed = base::unixtime::parse(period.date).date();
		months.emplace(QDate(parsed.year(), parsed.month(), 1));
	}
	if (!oldest) {
		return;
	}
	const auto oldestDay = base::unixtime::parse(oldest).date();
	for (const auto &month : months) {
		if (month.year() == key.year && month.month() == key.month) {
			continue;
		} else if (!noMoreData && month <= oldestDay) {
			continue;
		} else if (base::unixtime::serialize(QDateTime(
				month.addMonths(1),
				QTime())) > offsetDate) {
			continue;
		}
		const auto covered = MonthKey{
			.peerId = key.peerId,
			.year = month.year(),
			.month = month.month(),
		};
		if (_months[covered].loaded || _months[covered].requestId) {
			continue;
		}
		fillMonth(covered, periods, dayMedia);
		finishMonth(_months[covered]);
	}
}

void SearchCalendarController::processMonthData(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const std::vector<FullMsgId> &messages,
		TimeId minDate,
		TimeId offsetDate,
		bool noMoreData) {
	const auto dayMedia = collectDayMedia(messages);
	fillMonth(key, periods, dayMedia);

	auto &data = _months[key];
	const auto month = QDate(key.year, key.month, 1);
	const auto covered = noMoreData
		|| (minDate && base::unixtime::parse(minDate).date() < month);
	if (!covered && !data.requestId) {
		performMonthRequest(key);
		return;
	}

	finishMonth(data);
	fillCoveredMonths(key, periods, dayMedia, offsetDate, noMoreData);
	sendDeferredRequests();
}

std::vector<DayThumbnail> SearchCalendarController::thumbnails(
		const MonthData &data) const {
	auto result = std::vector<DayThumbnail>();
	result.reserve(data.cache.size());
	for (const auto &day : data.cache) {
		auto image = day.photo
			? Ui::MakePhotoThumbnail(day.photo, day.origin, true)
			: Ui::MakeDocumentThumbnail(day.document, day.origin, true);
		result.push_back({
			.date = day.date,
			.image = std::move(image),
			.msgId = day.msgId,
		});
	}
	return result;
}

void SearchCalendarController::finishMonth(MonthData &data) {
	data.loaded = true;
	auto callbacks = base::take(data.callbacks);
	if (callbacks.empty()) {
		return;
	}
	const auto list = thumbnails(data);
	for (const auto &callback : callbacks) {
		callback(list);
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

	for (const auto &day : it->second.cache) {
		if (day.date == dayStart) {
			return day.msgId;
		}
	}

	return std::nullopt;
}

} // namespace Api