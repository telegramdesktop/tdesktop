/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_streaming.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_reader.h"
#include "media/streaming/media_streaming_document.h"

namespace Data {
namespace {

constexpr auto kKeepAliveTimeout = 5 * crl::time(1000);

template <typename Object, typename Data>
bool PruneDestroyedAndSet(
		base::flat_map<
			not_null<Data*>,
			std::weak_ptr<Object>> &objects,
		not_null<Data*> data,
		const std::shared_ptr<Object> &object) {
	auto result = false;
	for (auto i = begin(objects); i != end(objects);) {
		if (i->first == data) {
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


template <typename Data>
[[nodiscard]] std::shared_ptr<Streaming::Reader> Streaming::sharedReader(
		base::flat_map<not_null<Data*>, std::weak_ptr<Reader>> &readers,
		not_null<Data*> data,
		FileOrigin origin,
		bool forceRemoteLoader) {
	const auto i = readers.find(data);
	if (i != end(readers)) {
		if (auto result = i->second.lock()) {
			if (!forceRemoteLoader || result->isRemoteLoader()) {
				return result;
			}
		}
	}
	auto loader = data->createStreamingLoader(origin, forceRemoteLoader);
	if (!loader) {
		return nullptr;
	}
	auto result = std::make_shared<Reader>(
		std::move(loader),
		&_owner->cacheBigFile());
	if (!PruneDestroyedAndSet(readers, data, result)) {
		readers.emplace_or_assign(data, result);
	}
	return result;

}

template <typename Data>
[[nodiscard]] std::shared_ptr<Streaming::Document> Streaming::sharedDocument(
		base::flat_map<not_null<Data*>, std::weak_ptr<Document>> &documents,
		base::flat_map<not_null<Data*>, std::weak_ptr<Reader>> &readers,
		not_null<Data*> data,
		FileOrigin origin) {
	const auto i = documents.find(data);
	if (i != end(documents)) {
		if (auto result = i->second.lock()) {
			return result;
		}
	}
	auto reader = sharedReader(readers, data, origin);
	if (!reader) {
		return nullptr;
	}
	auto result = std::make_shared<Document>(data, std::move(reader));
	if (!PruneDestroyedAndSet(documents, data, result)) {
		documents.emplace_or_assign(data, result);
	}
	return result;
}

template <typename Data>
void Streaming::keepAlive(
		base::flat_map<not_null<Data*>, std::weak_ptr<Document>> &documents,
		not_null<Data*> data) {
	const auto i = documents.find(data);
	if (i == end(documents)) {
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

std::shared_ptr<Streaming::Reader> Streaming::sharedReader(
		not_null<DocumentData*> document,
		FileOrigin origin,
		bool forceRemoteLoader) {
	return sharedReader(_fileReaders, document, origin, forceRemoteLoader);
}

std::shared_ptr<Streaming::Document> Streaming::sharedDocument(
		not_null<DocumentData*> document,
		FileOrigin origin) {
	return sharedDocument(_fileDocuments, _fileReaders, document, origin);
}

std::shared_ptr<Streaming::Reader> Streaming::sharedReader(
		not_null<PhotoData*> photo,
		FileOrigin origin,
		bool forceRemoteLoader) {
	return sharedReader(_photoReaders, photo, origin, forceRemoteLoader);
}

std::shared_ptr<Streaming::Document> Streaming::sharedDocument(
		not_null<PhotoData*> photo,
		FileOrigin origin) {
	return sharedDocument(_photoDocuments, _photoReaders, photo, origin);
}

void Streaming::keepAlive(not_null<DocumentData*> document) {
	keepAlive(_fileDocuments, document);
}

void Streaming::keepAlive(not_null<PhotoData*> photo) {
	keepAlive(_photoDocuments, photo);
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
