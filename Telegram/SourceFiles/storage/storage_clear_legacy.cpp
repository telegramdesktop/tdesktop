/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_clear_legacy.h"

#include <crl/crl_async.h>

namespace Storage {
namespace {

constexpr auto kClearPartSize = size_type(10000);

} // namespace

void ClearLegacyFilesPart(
		const QString &base,
		CollectGoodFiles filter,
		base::flat_set<QString> &&skip = {}) {
	filter([
		=,
		files = details::CollectFiles(base, kClearPartSize, skip)
	](base::flat_set<QString> &&skip) mutable {
		crl::async([
			=,
			files = std::move(files),
			skip = std::move(skip)
		]() mutable {
			for (const auto &name : files) {
				if (!skip.contains(name)
					&& !details::RemoveLegacyFile(base + name)) {
					skip.emplace(name);
				}
			}
			if (files.size() == kClearPartSize) {
				ClearLegacyFilesPart(base, filter, std::move(skip));
			}
		});
	});
}

void ClearLegacyFiles(const QString &base, CollectGoodFiles filter) {
	Expects(base.endsWith('/'));

	crl::async([=] {
		ClearLegacyFilesPart(base, std::move(filter));
	});
}

} // namespace Storage
