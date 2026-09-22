/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "storage/storage_shared_media.h"

class DocumentData;
class PhotoData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class DynamicImage;
} // namespace Ui

namespace Api {

struct CalendarPeriod {
	TimeId date = 0;
	MsgId minMsgId = 0;
	MsgId maxMsgId = 0;
	int count = 0;
};

struct CalendarResult {
	std::vector<CalendarPeriod> periods;
	int count = 0;
	TimeId minDate = 0;
	MsgId minMsgId = 0;
};

struct DayThumbnail {
	TimeId date = 0;
	std::shared_ptr<Ui::DynamicImage> image;
	MsgId msgId = 0;
};

class SearchCalendarController final {
public:
	SearchCalendarController(
		not_null<Main::Session*> session,
		PeerId peerId,
		Storage::SharedMediaType type);

	void monthThumbnails(
		TimeId date,
		Fn<void(std::vector<DayThumbnail>)> onFinish);

	[[nodiscard]] std::optional<MsgId> resolveMsgIdByDate(TimeId date) const;

private:
	struct MonthKey {
		PeerId peerId = 0;
		int year = 0;
		int month = 0;

		friend inline auto operator<=>(
			const MonthKey &,
			const MonthKey &) = default;
	};

	struct MonthState {
		MsgId offsetId = 0;
		TimeId offsetDate = 0;
	};

	struct MonthDay {
		TimeId date = 0;
		MsgId msgId = 0;
		FullMsgId origin;
		PhotoData *photo = nullptr;
		DocumentData *document = nullptr;
	};

	struct MonthData {
		std::vector<MonthDay> cache;
		std::vector<Fn<void(std::vector<DayThumbnail>)>> callbacks;
		mtpRequestId requestId = 0;
		MonthState state;
		bool loaded = false;
		bool deferred = false;
	};

	[[nodiscard]] std::optional<MonthKey> requestingNewest() const;
	void sendDeferredRequests();

	[[nodiscard]] std::vector<DayThumbnail> thumbnails(
		const MonthData &data) const;
	[[nodiscard]] base::flat_map<TimeId, MonthDay> collectDayMedia(
		const std::vector<FullMsgId> &messages) const;
	void fillMonth(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const base::flat_map<TimeId, MonthDay> &dayMedia);
	void fillCoveredMonths(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const base::flat_map<TimeId, MonthDay> &dayMedia,
		TimeId offsetDate,
		bool noMoreData);
	void finishMonth(MonthData &data);
	void performMonthRequest(const MonthKey &key);
	void processMonthData(
		const MonthKey &key,
		const std::vector<CalendarPeriod> &periods,
		const std::vector<FullMsgId> &messages,
		TimeId minDate,
		TimeId offsetDate,
		bool noMoreData);

	const not_null<Main::Session*> _session;
	const PeerId _peerId;
	const Storage::SharedMediaType _type;

	MTP::Sender _api;

	base::flat_map<MonthKey, MonthData> _months;

};

} // namespace Api
