/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Data {
struct AnyStatistics;
struct StatisticalGraph;
} // namespace Data

namespace Ui {
class RpWidget;
class Show;
} // namespace Ui

namespace Info::Statistics {

[[nodiscard]] bool ExportAvailable(const Data::AnyStatistics &stats);

void ExportToFile(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	const Data::AnyStatistics &stats,
	const Data::StatisticalGraph &pollVotes,
	FullMsgId contextId,
	FullStoryId storyId);

} // namespace Info::Statistics
