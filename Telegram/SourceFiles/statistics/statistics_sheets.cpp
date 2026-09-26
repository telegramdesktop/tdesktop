/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_sheets.h"

#include "data/data_statistics.h"
#include "lang/lang_keys.h"

namespace Statistic {
namespace {

constexpr auto kOneDay = 3600 * 24 * 1000;

[[nodiscard]] bool WithoutTimestamps(const Data::StatisticalChart &chart) {
	return chart.x.empty() || (chart.x.front() < kOneDay);
}

[[nodiscard]] Xlsx::Sheet SheetFromChart(
		const QString &title,
		const Data::StatisticalChart &chart) {
	const auto hours = WithoutTimestamps(chart);
	const auto whole = (chart.timeStep >= kOneDay);

	auto result = Xlsx::Sheet{ .name = title };
	result.rows.reserve(chart.x.size() + 1);

	auto header = std::vector<Xlsx::Cell>();
	header.reserve(chart.lines.size() + 1);
	header.push_back(Xlsx::Header(hours ? u"Hour"_q : u"Date"_q));
	for (const auto &line : chart.lines) {
		header.push_back(Xlsx::Header(line.name));
	}
	result.rows.push_back(std::move(header));

	for (auto i = 0; i != int(chart.x.size()); ++i) {
		auto row = std::vector<Xlsx::Cell>();
		row.reserve(chart.lines.size() + 1);
		row.push_back(hours
			? Xlsx::Number(chart.x[i])
			: whole
			? Xlsx::Date(chart.x[i])
			: Xlsx::DateTime(chart.x[i]));
		for (const auto &line : chart.lines) {
			row.push_back((i < int(line.y.size()))
				? Xlsx::Number(float64(line.y[i]))
				: Xlsx::Cell());
		}
		result.rows.push_back(std::move(row));
	}
	return result;
}

void AddChart(
		std::vector<Xlsx::Sheet> &sheets,
		const QString &title,
		const Data::StatisticalGraph &graph) {
	if (graph.chart) {
		sheets.push_back(SheetFromChart(title, graph.chart));
	}
}

void AddValue(
		std::vector<std::vector<Xlsx::Cell>> &rows,
		const QString &title,
		const Data::StatisticalValue &value) {
	rows.push_back({
		Xlsx::Text(title),
		Xlsx::Number(value.value),
		Xlsx::Number(value.previousValue),
	});
}

void AddCounter(
		std::vector<std::vector<Xlsx::Cell>> &rows,
		const QString &title,
		float64 value) {
	rows.push_back({
		Xlsx::Text(title),
		(value < 0) ? Xlsx::Cell() : Xlsx::Number(value),
	});
}

[[nodiscard]] std::vector<std::vector<Xlsx::Cell>> OverviewRows(
		const Data::AnyStatistics &stats) {
	auto result = std::vector<std::vector<Xlsx::Cell>>();
	result.push_back({
		Xlsx::Cell(),
		Xlsx::Header(u"Value"_q),
		Xlsx::Header(u"Previous"_q),
	});
	if (const auto &s = stats.channel) {
		AddValue(
			result,
			tr::lng_stats_overview_member_count(tr::now),
			s.memberCount);
		AddValue(
			result,
			tr::lng_stats_overview_mean_view_count(tr::now),
			s.meanViewCount);
		AddValue(
			result,
			tr::lng_stats_overview_mean_share_count(tr::now),
			s.meanShareCount);
		AddValue(
			result,
			tr::lng_stats_overview_mean_reactions_count(tr::now),
			s.meanReactionCount);
		AddValue(
			result,
			tr::lng_stats_overview_mean_story_view_count(tr::now),
			s.meanStoryViewCount);
		AddValue(
			result,
			tr::lng_stats_overview_mean_story_share_count(tr::now),
			s.meanStoryShareCount);
		AddValue(
			result,
			tr::lng_stats_overview_mean_story_reactions_count(tr::now),
			s.meanStoryReactionCount);
		AddCounter(
			result,
			tr::lng_stats_overview_enabled_notifications(tr::now),
			s.enabledNotificationsPercentage);
	} else if (const auto &s = stats.supergroup) {
		AddValue(
			result,
			tr::lng_stats_overview_member_count(tr::now),
			s.memberCount);
		AddValue(
			result,
			tr::lng_stats_overview_messages(tr::now),
			s.messageCount);
		AddValue(
			result,
			tr::lng_stats_overview_group_mean_view_count(tr::now),
			s.viewerCount);
		AddValue(
			result,
			tr::lng_stats_overview_group_mean_post_count(tr::now),
			s.senderCount);
	} else {
		const auto &post = stats.message ? stats.message : stats.story;
		AddCounter(
			result,
			tr::lng_stats_overview_message_views(tr::now),
			post.views);
		AddCounter(
			result,
			tr::lng_manage_peer_reactions(tr::now),
			post.reactions);
		AddCounter(
			result,
			tr::lng_stats_overview_message_public_shares(tr::now),
			post.publicForwards);
		AddCounter(
			result,
			tr::lng_stats_overview_message_private_shares(tr::now),
			post.privateForwards);
	}
	return result;
}

} // namespace

std::vector<Xlsx::Sheet> Sheets(
		const Data::AnyStatistics &stats,
		const Data::StatisticalGraph &pollVotes) {
	auto result = std::vector<Xlsx::Sheet>();
	result.push_back({
		.name = tr::lng_stats_overview_title(tr::now),
		.rows = OverviewRows(stats),
	});
	if (const auto &s = stats.channel) {
		AddChart(
			result,
			tr::lng_chart_title_member_count(tr::now),
			s.memberCountGraph);
		AddChart(result, tr::lng_chart_title_join(tr::now), s.joinGraph);
		AddChart(result, tr::lng_chart_title_mute(tr::now), s.muteGraph);
		AddChart(
			result,
			tr::lng_chart_title_view_count_by_hour(tr::now),
			s.viewCountByHourGraph);
		AddChart(
			result,
			tr::lng_chart_title_view_count_by_source(tr::now),
			s.viewCountBySourceGraph);
		AddChart(
			result,
			tr::lng_chart_title_join_by_source(tr::now),
			s.joinBySourceGraph);
		AddChart(
			result,
			tr::lng_chart_title_language(tr::now),
			s.languageGraph);
		AddChart(
			result,
			tr::lng_chart_title_message_interaction(tr::now),
			s.messageInteractionGraph);
		AddChart(
			result,
			tr::lng_chart_title_instant_view_interaction(tr::now),
			s.instantViewInteractionGraph);
		AddChart(
			result,
			tr::lng_chart_title_reactions_by_emotion(tr::now),
			s.reactionsByEmotionGraph);
		AddChart(
			result,
			tr::lng_chart_title_story_interactions(tr::now),
			s.storyInteractionsGraph);
		AddChart(
			result,
			tr::lng_chart_title_story_reactions_by_emotion(tr::now),
			s.storyReactionsByEmotionGraph);
	} else if (const auto &s = stats.supergroup) {
		AddChart(
			result,
			tr::lng_chart_title_member_count(tr::now),
			s.memberCountGraph);
		AddChart(
			result,
			tr::lng_chart_title_group_join(tr::now),
			s.joinGraph);
		AddChart(
			result,
			tr::lng_chart_title_group_join_by_source(tr::now),
			s.joinBySourceGraph);
		AddChart(
			result,
			tr::lng_chart_title_group_language(tr::now),
			s.languageGraph);
		AddChart(
			result,
			tr::lng_chart_title_group_message_content(tr::now),
			s.messageContentGraph);
		AddChart(
			result,
			tr::lng_chart_title_group_action(tr::now),
			s.actionGraph);
		AddChart(result, tr::lng_chart_title_group_day(tr::now), s.dayGraph);
		AddChart(result, tr::lng_chart_title_group_week(tr::now), s.weekGraph);
	} else {
		const auto &post = stats.message ? stats.message : stats.story;
		AddChart(
			result,
			tr::lng_chart_title_message_interaction(tr::now),
			post.messageInteractionGraph);
		AddChart(
			result,
			tr::lng_chart_title_reactions_by_emotion(tr::now),
			post.reactionsByEmotionGraph);
		AddChart(
			result,
			tr::lng_notification_reactions_poll_votes(tr::now),
			pollVotes);
	}
	return result;
}

} // namespace Statistic
