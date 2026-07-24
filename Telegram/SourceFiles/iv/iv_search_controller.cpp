/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_search_controller.h"

#include "base/invoke_queued.h"
#include "iv/iv_search_bar.h"
#include "iv/markdown/iv_markdown_article.h"
#include "logs.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace Iv {

using Markdown::MarkdownArticleSearchMatch;

SearchController::SearchController(
	not_null<QWidget*> barParent,
	rpl::producer<int> barWidth,
	SearchHost host,
	SearchBarMode barMode)
: _barParent(barParent)
, _host(std::move(host))
, _searchBar(std::make_unique<SearchBar>(
	barParent,
	std::move(barWidth),
	barMode)) {
	_searchBar->raise();
	_searchBar->closeRequests() | rpl::on_next([=] {
		InvokeQueued(_barParent, [=] {
			hide();
		});
	}, _searchBar->lifetime());
	_searchBar->queryChanges() | rpl::on_next([=](const QString &query) {
		applySearchQuery(query);
	}, _searchBar->lifetime());
	_searchBar->navigateRequests() | rpl::on_next([=](int delta) {
		stepSearchResult(delta);
	}, _searchBar->lifetime());
	_searchBar->focusChanges() | rpl::on_next([=](bool focused) {
		if (!focused || !_searchBar->shown()) {
			return;
		}
		if (_host.fieldFocused) {
			_host.fieldFocused();
		}
		refresh();
	}, _searchBar->lifetime());
}

SearchController::~SearchController() = default;

void SearchController::toggle() {
	if (_searchBar->shown()) {
		hide();
		return;
	}
	_searchBar->show(anim::type::normal);
	_searchBar->setInnerFocus();
	refresh();
}

void SearchController::hide() {
	_searchBar->hide(anim::type::normal);
	_searchBar->setResults(0, 0);
	if (_host.ready()) {
		_host.applyMatches({}, -1);
		_host.focusContent();
	}
	invalidateSearchSession();
	_searchEntries.clear();
	_searchCurrentEntry = -1;
}

bool SearchController::shown() const {
	return _searchBar->shown();
}

void SearchController::refresh() {
	invalidateSearchSession();
	if (_searchBar->shown()) {
		rebuildSearchResults(_searchCurrentEntry, false);
	}
}

void SearchController::moveBar(int x, int y) {
	_searchBar->move(x, y);
}

void SearchController::raiseBar() {
	_searchBar->raise();
}

rpl::producer<int> SearchController::barHeightValue() const {
	return _searchBar->heightValue();
}

auto SearchController::ScanSearchEntries(
		const SearchSources &sources,
		const QString &query)
-> std::vector<SearchEntry> {
	auto result = std::vector<SearchEntry>();
	if (query.isEmpty()) {
		return result;
	}
	const auto scan = [&](const QString &text, auto &&push) {
		auto from = 0;
		while ((from = int(text.indexOf(
				query,
				from,
				Qt::CaseInsensitive))) >= 0) {
			push(from, from + int(query.size()));
			from += int(query.size());
		}
	};
	for (auto i = 0; i != int(sources.size()); ++i) {
		const auto &source = sources[i];
		scan(source.text, [&](int from, int to) {
			result.push_back({ .segment = i, .from = from, .to = to });
		});
		if (!source.hiddenText.isEmpty()) {
			scan(source.hiddenText, [&](int, int) {
				result.push_back({
					.hiddenDetailsId = source.detailsAnchorId,
				});
			});
		}
	}
	return result;
}

void SearchController::ensureSearchSnapshot() {
	if (_searchSnapshot || !_host.ready()) {
		return;
	}
	_searchSnapshot = std::make_shared<const SearchSources>(
		_host.sources());
}

void SearchController::invalidateSearchSession() {
	_searchSnapshot = nullptr;
	_searchCache.clear();
	++_searchGeneration;
}

auto SearchController::rescanSearchEntries()
-> std::vector<SearchEntry> {
	invalidateSearchSession();
	ensureSearchSnapshot();
	if (!_searchSnapshot || _searchQuery.isEmpty()) {
		return {};
	}
	auto result = ScanSearchEntries(*_searchSnapshot, _searchQuery);
	_searchCache.emplace(_searchQuery, result);
	return result;
}

void SearchController::applySearchQuery(const QString &query) {
	_searchQuery = query;
	rebuildSearchResults(0, true);
}

void SearchController::rebuildSearchResults(
		int preferredCurrent,
		bool activate) {
	_searchDesiredCurrent = preferredCurrent;
	_searchDesiredActivate = activate;
	if (_searchQuery.isEmpty() || !_host.ready()) {
		applySearchEntries({}, preferredCurrent, activate);
		return;
	}
	const auto i = _searchCache.find(_searchQuery);
	if (i != end(_searchCache)) {
		DEBUG_LOG(("Native Markdown IV: search cache hit: %1"
			).arg(_searchQuery));
		auto entries = i->second;
		applySearchEntries(std::move(entries), preferredCurrent, activate);
		return;
	}
	if (_searchScanInFlight) {
		DEBUG_LOG(("Native Markdown IV: search coalesced: %1"
			).arg(_searchQuery));
		return;
	}
	startSearchScan();
}

void SearchController::applySearchEntries(
		std::vector<SearchEntry> &&entries,
		int preferredCurrent,
		bool activate) {
	_searchEntries = std::move(entries);
	const auto total = int(_searchEntries.size());
	_searchCurrentEntry = total
		? std::clamp(preferredCurrent, 0, total - 1)
		: -1;
	applyCurrentSearchEntry(activate);
}

void SearchController::startSearchScan() {
	Expects(!_searchScanInFlight);

	ensureSearchSnapshot();
	if (!_searchSnapshot) {
		return;
	}
	_searchScanInFlight = true;
	DEBUG_LOG(("Native Markdown IV: search request: %1"
		).arg(_searchQuery));
	const auto weak = base::make_weak(this);
	crl::async([
		weak,
		query = _searchQuery,
		generation = _searchGeneration,
		snapshot = _searchSnapshot
	] {
		if (!weak) {
			return;
		}
		auto entries = ScanSearchEntries(*snapshot, query);
		crl::on_main([
			weak,
			query,
			generation,
			entries = std::move(entries)
		]() mutable {
			if (const auto strong = weak.get()) {
				strong->finishSearchScan(
					query,
					generation,
					std::move(entries));
			}
		});
	});
}

void SearchController::finishSearchScan(
		const QString &query,
		int generation,
		std::vector<SearchEntry> &&entries) {
	_searchScanInFlight = false;
	if (generation != _searchGeneration) {
		DEBUG_LOG(("Native Markdown IV: search response dropped: %1"
			).arg(query));
	} else {
		DEBUG_LOG(("Native Markdown IV: search response: %1 (%2 matches)"
			).arg(query
			).arg(int(entries.size())));
		_searchCache[query] = entries;
	}
	if (!_searchBar->shown() || _searchQuery.isEmpty()) {
		return;
	}
	if (generation == _searchGeneration && query == _searchQuery) {
		applySearchEntries(
			std::move(entries),
			_searchDesiredCurrent,
			_searchDesiredActivate);
	} else {
		rebuildSearchResults(
			_searchDesiredCurrent,
			_searchDesiredActivate);
	}
}

void SearchController::resolveCurrentSearchEntry() {
	if (!_host.ready()) {
		return;
	}
	while (_searchCurrentEntry >= 0
		&& _searchCurrentEntry < int(_searchEntries.size())) {
		const auto anchorId
			= _searchEntries[_searchCurrentEntry].hiddenDetailsId;
		if (anchorId.isEmpty()) {
			return;
		}
		auto runStart = _searchCurrentEntry;
		while (runStart > 0
			&& (_searchEntries[runStart - 1].hiddenDetailsId
				== anchorId)) {
			--runStart;
		}
		auto runEnd = _searchCurrentEntry + 1;
		while (runEnd < int(_searchEntries.size())
			&& _searchEntries[runEnd].hiddenDetailsId == anchorId) {
			++runEnd;
		}
		const auto offset = _searchCurrentEntry - runStart;
		const auto oldTotal = int(_searchEntries.size());
		if (!_host.expandDetails(anchorId)) {
			return;
		}
		_searchEntries = rescanSearchEntries();
		const auto newTotal = int(_searchEntries.size());
		const auto materialized = newTotal
			- (oldTotal - (runEnd - runStart));
		if (!newTotal) {
			_searchCurrentEntry = -1;
		} else if (materialized <= 0) {
			_searchCurrentEntry = std::min(runStart, newTotal - 1);
		} else {
			_searchCurrentEntry = runStart
				+ std::min(offset, materialized - 1);
		}
	}
}

void SearchController::applyCurrentSearchEntry(bool activate) {
	if (activate) {
		resolveCurrentSearchEntry();
	}
	const auto total = int(_searchEntries.size());
	auto matches = std::vector<MarkdownArticleSearchMatch>();
	auto currentMatch = -1;
	auto currentSegment = -1;
	for (auto i = 0; i != total; ++i) {
		const auto &entry = _searchEntries[i];
		if (entry.segment < 0) {
			continue;
		} else if (i == _searchCurrentEntry) {
			currentMatch = int(matches.size());
			currentSegment = entry.segment;
		}
		matches.push_back({
			.segment = entry.segment,
			.from = entry.from,
			.to = entry.to,
		});
	}
	if (_host.ready()) {
		_host.applyMatches(std::move(matches), currentMatch);
		if (activate && currentSegment >= 0) {
			_host.scrollToSegment(currentSegment);
		}
	}
	_searchBar->setResults(total ? (_searchCurrentEntry + 1) : 0, total);
}

void SearchController::stepSearchResult(int delta) {
	const auto total = int(_searchEntries.size());
	if (!total || !_host.ready()) {
		return;
	}
	_searchCurrentEntry = (_searchCurrentEntry + delta + total) % total;
	applyCurrentSearchEntry(true);
}

} // namespace Iv
