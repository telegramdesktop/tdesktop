/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_types.h"
#include "base/binary_guard.h"

namespace Storage {
namespace Cache {
namespace details {

class CleanerObject;

class Cleaner {
public:
	Cleaner(
		const QString &base,
		base::binary_guard &&guard,
		FnMut<void(Error)> done);

	~Cleaner();

private:
	using Implementation = details::CleanerObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace details
} // namespace Cache
} // namespace Storage
