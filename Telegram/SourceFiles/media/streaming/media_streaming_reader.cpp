/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_reader.h"

#include "media/streaming/media_streaming_loader.h"
#include "storage/cache/storage_cache_database.h"
#include "data/data_session.h"

namespace Media {
namespace Streaming {
namespace {

template <typename Range> // Range::value_type is Pair<int, QByteArray>
int FindNotLoadedStart(Range &&parts, int offset) {
	auto result = offset;
	for (const auto &part : parts) {
		const auto partStart = part.first;
		const auto partEnd = partStart + part.second.size();
		if (partStart <= result && partEnd >= result) {
			result = partEnd;
		} else {
			break;
		}
	}
	return result;
}

template <typename Range> // Range::value_type is Pair<int, QByteArray>
void CopyLoaded(bytes::span buffer, Range &&parts, int offset, int till) {
	auto filled = offset;
	for (const auto &part : parts) {
		const auto bytes = bytes::make_span(part.second);
		const auto partStart = part.first;
		const auto partEnd = int(partStart + bytes.size());
		const auto copyTill = std::min(partEnd, till);
		Assert(partStart <= filled && filled < copyTill);

		const auto from = filled - partStart;
		const auto copy = copyTill - filled;
		bytes::copy(buffer, bytes.subspan(from, copy));
		buffer = buffer.subspan(copy);
		filled += copy;
	}
}

} // namespace

Reader::Reader(
	not_null<Data::Session*> owner,
	std::unique_ptr<Loader> loader)
: _owner(owner)
, _loader(std::move(loader)) {
	_loader->parts(
	) | rpl::start_with_next([=](LoadedPart &&part) {
		QMutexLocker lock(&_loadedPartsMutex);
		_loadedParts.push_back(std::move(part));
		lock.unlock();

		if (const auto waiting = _waiting.load()) {
			_waiting = nullptr;
			waiting->release();
		}
	}, _lifetime);
}

int Reader::size() const {
	return _loader->size();
}

bool Reader::failed() const {
	return _failed;
}

bool Reader::fill(
		bytes::span buffer,
		int offset,
		crl::semaphore *notify) {
	Expects(offset + buffer.size() <= size());

	const auto wait = [&](int offset) {
		_waiting = notify;
		loadFor(offset);
		return false;
	};
	const auto done = [&] {
		_waiting = nullptr;
		return true;
	};
	const auto failed = [&] {
		_waiting = nullptr;
		if (notify) {
			notify->release();
		}
		return false;
	};

	processLoadedParts();
	if (_failed) {
		return failed();
	}

	const auto after = ranges::upper_bound(
		_data,
		offset,
		ranges::less(),
		&base::flat_map<int, QByteArray>::value_type::first);
	if (after == begin(_data)) {
		return wait(offset);
	}

	const auto till = int(offset + buffer.size());
	const auto start = after - 1;
	const auto finish = ranges::lower_bound(
		start,
		end(_data),
		till,
		ranges::less(),
		&base::flat_map<int, QByteArray>::value_type::first);
	const auto parts = ranges::make_iterator_range(start, finish);

	const auto haveTill = FindNotLoadedStart(parts, offset);
	if (haveTill < till) {
		return wait(haveTill);
	}
	CopyLoaded(buffer, parts, offset, till);
	return done();
}

void Reader::processLoadedParts() {
	QMutexLocker lock(&_loadedPartsMutex);
	auto loaded = std::move(_loadedParts);
	lock.unlock();

	if (_failed) {
		return;
	}
	for (auto &part : loaded) {
		if (part.offset == LoadedPart::kFailedOffset
			|| (part.bytes.size() != Loader::kPartSize
				&& part.offset + part.bytes.size() != size())) {
			_failed = true;
			return;
		}
		_data.emplace(part.offset, std::move(part.bytes));
	}
}

void Reader::loadFor(int offset) {
	const auto part = offset / Loader::kPartSize;
	_loader->load(part * Loader::kPartSize);
}

Reader::~Reader() = default;

} // namespace Streaming
} // namespace Media
