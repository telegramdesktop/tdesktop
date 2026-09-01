/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_export.h"

#include "base/base_file_utilities.h"
#include "base/debug_log.h"
#include "core/file_utilities.h"
#include "data/data_peer.h"
#include "data/data_statistics.h"
#include "lang/lang_keys.h"
#include "statistics/statistics_sheets.h"
#include "ui/layers/show.h"
#include "ui/rp_widget.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"

namespace Info::Statistics {
namespace {

using namespace ::Statistic;

[[nodiscard]] QString SuggestedName(
		not_null<PeerData*> peer,
		FullMsgId contextId,
		FullStoryId storyId) {
	constexpr auto kMaxTitleLength = 48;

	auto name = base::FileNameFromUserString(peer->name().simplified());
	if (name.size() > kMaxTitleLength) {
		name = name.mid(0, kMaxTitleLength).trimmed();
	}
	if (contextId) {
		name += u"_%1"_q.arg(contextId.msg.bare);
	} else if (storyId) {
		name += u"_story_%1"_q.arg(storyId.story);
	}
	return name
		+ '_'
		+ QDate::currentDate().toString(Qt::ISODate)
		+ u".xlsx"_q;
}

} // namespace

bool ExportAvailable(const Data::AnyStatistics &stats) {
	return stats.channel
		|| stats.supergroup
		|| stats.message
		|| stats.story;
}

void ExportToFile(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		const Data::AnyStatistics &stats,
		const Data::StatisticalGraph &pollVotes,
		FullMsgId contextId,
		FullStoryId storyId) {
	if (!ExportAvailable(stats)) {
		return;
	}
	FileDialog::GetWritePath(
		parent.get(),
		tr::lng_stats_export(tr::now),
		u"Spreadsheets (*.xlsx)"_q,
		SuggestedName(peer, contextId, storyId),
		[=, stats = stats, pollVotes = pollVotes](const QString &path) {
			const auto fail = [&] {
				show->showToast(tr::lng_stats_export_error(tr::now));
			};
			const auto content = Xlsx::Serialize(Sheets(stats, pollVotes));
			if (content.isEmpty()) {
				LOG(("Statistics Error: Could not serialize the workbook."));
				fail();
				return;
			}
			auto file = QFile(path);
			if (!file.open(QIODevice::WriteOnly)
				|| file.write(content) != content.size()) {
				LOG(("Statistics Error: Could not write '%1'.").arg(path));
				fail();
				return;
			}
			file.close();
			show->showToast({
				.text = tr::lng_stats_export_done(
					tr::now,
					lt_file,
					Ui::Text::Link(
						QFileInfo(path).fileName(),
						u"internal:show_saved_file"_q),
					Ui::Text::WithEntities),
				.filter = [=](const auto ...) {
					File::ShowInFolder(path);
					return false;
				},
			});
		});
}

} // namespace Info::Statistics
