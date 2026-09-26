/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "iv/iv_search_bar.h"

#include <rpl/producer.h>

#include <memory>
#include <vector>
#include <QtCore/QString>

class QWidget;

namespace Iv::Markdown {
struct MarkdownArticleSearchMatch;
struct MarkdownArticleSearchSource;
} // namespace Iv::Markdown

namespace Iv {

struct SearchHost {
	Fn<bool()> ready;
	Fn<std::vector<Markdown::MarkdownArticleSearchSource>()> sources;
	Fn<void(
		std::vector<Markdown::MarkdownArticleSearchMatch>,
		int current)> applyMatches;
	Fn<void(int segmentIndex)> scrollToSegment;
	Fn<bool(const QString &anchorId)> expandDetails;
	Fn<void()> focusContent;
	Fn<void()> fieldFocused;
};

class SearchController final : public base::has_weak_ptr {
public:
	SearchController(
		not_null<QWidget*> barParent,
		rpl::producer<int> barWidth,
		SearchHost host,
		SearchBarMode barMode = SearchBarMode::WindowStrip);
	~SearchController();

	void toggle();
	void hide();
	[[nodiscard]] bool shown() const;
	void refresh();
	void moveBar(int x, int y);
	void raiseBar();
	[[nodiscard]] rpl::producer<int> barHeightValue() const;

private:
	struct SearchEntry {
		int segment = -1;
		int from = 0;
		int to = 0;
		QString hiddenDetailsId;
	};
	using SearchSources
		= std::vector<Markdown::MarkdownArticleSearchSource>;

	void applySearchQuery(const QString &query);
	void rebuildSearchResults(int preferredCurrent, bool activate);
	void resolveCurrentSearchEntry();
	void applyCurrentSearchEntry(bool activate);
	void stepSearchResult(int delta);
	[[nodiscard]] static std::vector<SearchEntry> ScanSearchEntries(
		const SearchSources &sources,
		const QString &query);
	void ensureSearchSnapshot();
	void invalidateSearchSession();
	[[nodiscard]] std::vector<SearchEntry> rescanSearchEntries();
	void applySearchEntries(
		std::vector<SearchEntry> &&entries,
		int preferredCurrent,
		bool activate);
	void startSearchScan();
	void finishSearchScan(
		const QString &query,
		int generation,
		std::vector<SearchEntry> &&entries);

	const not_null<QWidget*> _barParent;
	SearchHost _host;
	std::unique_ptr<SearchBar> _searchBar;
	QString _searchQuery;
	std::vector<SearchEntry> _searchEntries;
	int _searchCurrentEntry = -1;
	std::shared_ptr<const SearchSources> _searchSnapshot;
	base::flat_map<QString, std::vector<SearchEntry>> _searchCache;
	int _searchGeneration = 0;
	int _searchDesiredCurrent = 0;
	bool _searchDesiredActivate = false;
	bool _searchScanInFlight = false;

};

} // namespace Iv
