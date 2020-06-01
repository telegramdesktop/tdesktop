/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_streaming.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_reader.h"
#include "media/streaming/media_streaming_document.h"

namespace Data {
namespace {

constexpr auto kKeepAliveTimeout = 5 * crl::time(1000);

template <typename Object>
bool PruneDestroyedAndSet(
		base::flat_map<
			not_null<DocumentData*>,
			std::weak_ptr<Object>> &objects,
		not_null<DocumentData*> document,
		const std::shared_ptr<Object> &object) {
	auto result = false;
	for (auto i = begin(objects); i != end(objects);) {
		if (i->first == document) {
			(i++)->second = object;
			result = true;
		} else if (i->second.lock() != nullptr) {
			++i;
		} else {
			i = objects.erase(i);
		}
	}
	return result;
}

} // namespace

Streaming::Streaming(not_null<Session*> owner)
: _owner(owner)
, _keptAliveTimer([=] { clearKeptAlive(); }) {
}

Streaming::~Streaming() = default;

std::shared_ptr<Streaming::Reader> Streaming::sharedReader(
		not_null<DocumentData*> document,
		FileOrigin origin,
		bool forceRemoteLoader) {
	const auto i = _readers.find(document);
	if (i != end(_readers)) {
		if (auto result = i->second.lock()) {
			if (!forceRemoteLoader || result->isRemoteLoader()) {
				return result;
			}
		}
	}
	auto loader = document->createStreamingLoader(origin, forceRemoteLoader);
	if (!loader) {
		return nullptr;
	}
	auto result = std::make_shared<Reader>(
		std::move(loader),
		&_owner->cacheBigFile());
	if (!PruneDestroyedAndSet(_readers, document, result)) {
		_readers.emplace_or_assign(document, result);
	}
	return result;
}

std::shared_ptr<Streaming::Document> Streaming::sharedDocument(
		not_null<DocumentData*> document,
		FileOrigin origin) {
	const auto i = _documents.find(document);
	if (i != end(_documents)) {
		if (auto result = i->second.lock()) {
			return result;
		}
	}
	auto reader = sharedReader(document, origin);
	if (!reader) {
		return nullptr;
	}
	auto result = std::make_shared<Document>(document, std::move(reader));
	if (!PruneDestroyedAndSet(_documents, document, result)) {
		_documents.emplace_or_assign(document, result);
	}
	return result;
}

void Streaming::keepAlive(not_null<DocumentData*> document) {
	const auto i = _documents.find(document);
	if (i == end(_documents)) {
		return;
	}
	auto shared = i->second.lock();
	if (!shared) {
		return;
	}
	const auto till = crl::now() + kKeepAliveTimeout;
	const auto j = _keptAlive.find(shared);
	if (j != end(_keptAlive)) {
		j->second = till;
	} else {
		_keptAlive.emplace(std::move(shared), till);
	}
	if (!_keptAliveTimer.isActive()) {
		_keptAliveTimer.callOnce(kKeepAliveTimeout);
	}
}

void Streaming::clearKeptAlive() {
	const auto now = crl::now();
	auto min = std::numeric_limits<crl::time>::max();
	for (auto i = begin(_keptAlive); i != end(_keptAlive);) {
		const auto wait = (i->second - now);
		if (wait <= 0) {
			i = _keptAlive.erase(i);
		} else {
			++i;
			if (min > wait) {
				min = wait;
			}
		}
	}
	if (!_keptAlive.empty()) {
		_keptAliveTimer.callOnce(min);
	}
}

} // namespace Data
