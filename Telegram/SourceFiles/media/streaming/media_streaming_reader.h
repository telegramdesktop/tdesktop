/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace Data {
class Session;
} // namespace Data

namespace Media {
namespace Streaming {

class Loader;
struct LoadedPart;

class Reader final {
public:
	Reader(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	int size() const;
	bool fill(
		bytes::span buffer,
		int offset,
		crl::semaphore *notify = nullptr);
	bool failed() const;

	~Reader();

private:
	void processLoadedParts();
	void loadFor(int offset);

	const not_null<Data::Session*> _owner;
	const std::unique_ptr<Loader> _loader;

	QMutex _loadedPartsMutex;
	std::vector<LoadedPart> _loadedParts;
	std::atomic<crl::semaphore*> _waiting = nullptr;

	// #TODO streaming optimize
	base::flat_map<int, QByteArray> _data;
	bool _failed = false;
	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
