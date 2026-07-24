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
		if (it->second.loaded) {
			onFinish(it->second.cache);
			return;
		}
	}

	auto &data = _months[key];
	data.callbacks.push_back(std::move(onFinish));

	if (!data.requestId) {
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

	data.requestId = _api.request(
		MTPmessages_GetSearchResultsCalendar(
			MTP_flags(0),
			peer->input(),
			MTPInputPeer(),
			filter,
			MTP_int(data.state.offsetId),
			MTP_int(data.state.offsetDate ? data.state.offsetDate : endDate)
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
			noMoreData);
	}).fail([=] {
		auto &data = _months[key];
		data.requestId = 0;
		data.loaded = true;
		auto callbacks = std::move(data.callbacks);
		data.callbacks.clear();
		for (const auto &callback : callbacks) {
			callback(data.cache);
		}
	}).send();
}

void SearchCalendarController::processMonthData(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const std::vector<FullMsgId> &messages,
		TimeId minDate,
		bool noMoreData) {
	const auto inTargetMonth = [&](TimeId date) {
		const auto parsed = base::unixtime::parse(date).date();
		return (parsed.year() == key.year) && (parsed.month() == key.month);
	};
	const auto dayStartOf = [&](TimeId date) {
		return base::unixtime::serialize(
			QDateTime(base::unixtime::parse(date).date(), QTime()));
	};

	// Representative messages of the page give the thumbnail for each day.
	auto dayImages = base::flat_map<
		TimeId,
		std::shared_ptr<Ui::DynamicImage>>();
	for (const auto &fullId : messages) {
		const auto item = _session->data().message(fullId);
		if (!item || !inTargetMonth(item->date())) {
			continue;
		}
		const auto dayStart = dayStartOf(item->date());
		if (dayImages.contains(dayStart)) {
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
			dayImages.emplace(dayStart, std::move(image));
		}
	}

	// Periods are authoritative: they provide the newest message of each day.
	auto &data = _months[key];
	auto seenDays = base::flat_set<TimeId>();
	for (const auto &thumb : data.cache) {
		seenDays.emplace(thumb.date);
	}
	for (const auto &period : periods) {
		if (!period.maxMsgId || !inTargetMonth(period.date)) {
			continue;
		}
		const auto dayStart = dayStartOf(period.date);
		if (seenDays.contains(dayStart)) {
			continue;
		}
		const auto i = dayImages.find(dayStart);
		if (i == dayImages.end()) {
			continue;
		}
		seenDays.emplace(dayStart);
		data.cache.push_back(DayThumbnail{
			.date = dayStart,
			.image = i->second,
			.msgId = period.maxMsgId,
		});
	}

	const auto month = QDate(key.year, key.month, 1);
	const auto covered = noMoreData
		|| (minDate && base::unixtime::parse(minDate).date() < month);
	if (!covered && !data.requestId) {
		performMonthRequest(key);
		return;
	}

	data.loaded = true;
	auto callbacks = std::move(data.callbacks);
	data.callbacks.clear();
	for (const auto &callback : callbacks) {
		callback(data.cache);
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