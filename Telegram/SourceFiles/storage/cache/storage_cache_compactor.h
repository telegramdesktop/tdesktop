/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_types.h"
#include <crl/crl_object_on_queue.h>

namespace Storage {
namespace Cache {
namespace details {

class CompactorObject;
class DatabaseObject;

class Compactor {
public:
	Compactor(
		const QString &path,
		crl::weak_on_queue<DatabaseObject> database);

	~Compactor();

private:
	using Implementation = CompactorObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace details
} // namespace Cache
} // namespace Storage
