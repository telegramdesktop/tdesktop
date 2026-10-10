/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include "export/data/export_message_slice.h"

#include <initializer_list>
#include <iostream>

namespace {

auto FailedChecks = 0;
auto TotalChecks = 0;

struct SliceCase {
	const char *name = nullptr;
	TimeId from = 0;
	TimeId till = 0;
	bool topic = false;
	std::initializer_list<TimeId> dates;
	std::initializer_list<int32> expectedIds;
	bool expectedUpperBound = false;
};

void CheckSlice(const SliceCase &test) {
	auto settings = Export::Settings();
	settings.singlePeerFrom = test.from;
	settings.singlePeerTill = test.till;
	if (test.topic) {
		settings.singlePeer = MTP_inputPeerChannel(MTP_long(1), MTP_long(2));
		settings.singleTopicRootId = 1;
	}

	auto slice = Export::Data::MessagesSlice();
	auto id = int32(0);
	for (const auto date : test.dates) {
		auto message = Export::Data::Message();
		message.id = ++id;
		message.date = date;
		slice.list.push_back(std::move(message));
	}
	const auto reachedUpperBound = Export::Data::TrimMessagesSliceByDateRange(
		slice,
		settings);
	auto ids = std::vector<int32>();
	for (const auto &message : slice.list) {
		ids.push_back(message.id);
	}

	++TotalChecks;
	if (ids != std::vector<int32>(test.expectedIds)
		|| reachedUpperBound != test.expectedUpperBound) {
		++FailedChecks;
		std::cout << "FAILED: " << test.name << std::endl;
	}
}

} // namespace

int main() {
	const auto cases = {
		SliceCase{ "Unbounded history", 0, 0, false,
			{ 50, 100, 150, 200 }, { 1, 2, 3, 4 }, false },
		SliceCase{ "From-only history", 100, 0, false,
			{ 50, 100, 150, 200 }, { 2, 3, 4 }, false },
		SliceCase{ "Till-only history", 0, 200, false,
			{ 50, 100, 150, 200 }, { 1, 2, 3 }, true },
		SliceCase{ "Bounded history", 100, 200, false,
			{ 50, 100, 150, 200 }, { 2, 3 }, true },
		SliceCase{ "Inclusive lower boundary", 100, 0, false,
			{ 99, 100, 101 }, { 2, 3 }, false },
		SliceCase{ "Exclusive upper boundary", 0, 200, false,
			{ 199, 200, 201 }, { 1 }, true },
		SliceCase{ "History below range", 100, 200, false,
			{ 80, 90 }, {}, false },
		SliceCase{ "History above range", 100, 200, false,
			{ 200, 210 }, {}, true },
		SliceCase{ "Empty history", 100, 200, false,
			{}, {}, false },
		SliceCase{ "Repeated boundary timestamps", 100, 200, false,
			{ 99, 100, 100, 199, 200, 200 }, { 2, 3, 4 }, true },
		SliceCase{ "Old topic root retains pagination cursor", 100, 200, true,
			{ 50 }, { 1 }, false },
		SliceCase{ "Old topic replies retain pagination cursor", 100, 200, true,
			{ 60, 70 }, { 1, 2 }, false },
		SliceCase{ "Topic page across both bounds stays intact", 100, 200, true,
			{ 90, 100, 199, 200 }, { 1, 2, 3, 4 }, false },
		SliceCase{ "Topic page past upper bound stays intact", 100, 200, true,
			{ 200, 210 }, { 1, 2 }, false },
		SliceCase{ "Empty topic page remains empty", 100, 200, true,
			{}, {}, false },
	};
	for (const auto &test : cases) {
		CheckSlice(test);
	}

	std::cout << (TotalChecks - FailedChecks) << '/' << TotalChecks
		<< " checks passed" << std::endl;
	return FailedChecks ? 1 : 0;
}
