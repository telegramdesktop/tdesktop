/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "storage/storage_shared_media.h"

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

	struct MonthData {
		std::vector<DayThumbnail> cache;
		std::vector<Fn<void(std::vector<DayThumbnail>)>> callbacks;
		mtpRequestId requestId = 0;
		MonthState state;
	};

	void performMonthRequest(const MonthKey &key);
	void processMonthMessages(
		const MonthKey &key,
		const std::vector<FullMsgId> &messages,
		TimeId minDate,
		MsgId minMsgId,
		bool noMoreData);

	const not_null<Main::Session*> _session;
	const PeerId _peerId;
	const Storage::SharedMediaType _type;

	MTP::Sender _api;

	base::flat_map<MonthKey, MonthData> _months;

};

} // namespace Api
