/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class DocumentData;

namespace Media {
namespace Streaming {
class Reader;
class Document;
} // namespace Streaming
} // namespace Media

namespace Data {

class Session;
struct FileOrigin;

class Streaming final {
public:
	explicit Streaming(not_null<Session*> owner);
	Streaming(const Streaming &other) = delete;
	Streaming &operator=(const Streaming &other) = delete;
	~Streaming();

	using Reader = ::Media::Streaming::Reader;
	using Document = ::Media::Streaming::Document;

	[[nodiscard]] std::shared_ptr<Reader> sharedReader(
		not_null<DocumentData*> document,
		FileOrigin origin,
		bool forceRemoteLoader = false);
	[[nodiscard]] std::shared_ptr<Document> sharedDocument(
		not_null<DocumentData*> document,
		FileOrigin origin);

	void keepAlive(not_null<DocumentData*> document);

private:
	void clearKeptAlive();

	const not_null<Session*> _owner;

	base::flat_map<not_null<DocumentData*>, std::weak_ptr<Reader>> _readers;
	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<Document>> _documents;

	base::flat_map<std::shared_ptr<Document>, crl::time> _keptAlive;
	base::Timer _keptAliveTimer;

};

} // namespace Data
