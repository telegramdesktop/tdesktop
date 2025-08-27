/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class PhotoData;
class DocumentData;

namespace Media::Streaming {
class Reader;
class Document;
} // namespace Media::Streaming

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
	[[nodiscard]] std::shared_ptr<Document> sharedDocument(
		not_null<DocumentData*> quality,
		not_null<DocumentData*> original,
		HistoryItem *context,
		FileOrigin origin);

	[[nodiscard]] std::shared_ptr<Reader> sharedReader(
		not_null<PhotoData*> photo,
		FileOrigin origin,
		bool forceRemoteLoader = false);
	[[nodiscard]] std::shared_ptr<Document> sharedDocument(
		not_null<PhotoData*> photo,
		FileOrigin origin);

	void keepAlive(not_null<DocumentData*> document);
	void keepAlive(not_null<PhotoData*> photo);

private:
	void clearKeptAlive();

	template <typename Data>
	[[nodiscard]] std::shared_ptr<Reader> sharedReader(
		base::flat_map<not_null<Data*>, std::weak_ptr<Reader>> &readers,
		not_null<Data*> data,
		FileOrigin origin,
		bool forceRemoteLoader = false);

	template <typename Data>
	[[nodiscard]] std::shared_ptr<Document> sharedDocument(
		base::flat_map<not_null<Data*>, std::weak_ptr<Document>> &documents,
		base::flat_map<not_null<Data*>, std::weak_ptr<Reader>> &readers,
		not_null<Data*> data,
		DocumentData *original,
		HistoryItem *context,
		FileOrigin origin);

	template <typename Data>
	void keepAlive(
		base::flat_map<not_null<Data*>, std::weak_ptr<Document>> &documents,
		not_null<Data*> data);

	const not_null<Session*> _owner;

	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<Reader>> _fileReaders;
	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<Document>> _fileDocuments;

	base::flat_map<not_null<PhotoData*>, std::weak_ptr<Reader>> _photoReaders;
	base::flat_map<
		not_null<PhotoData*>,
		std::weak_ptr<Document>> _photoDocuments;

	base::flat_map<std::shared_ptr<Document>, crl::time> _keptAlive;
	base::Timer _keptAliveTimer;

};

} // namespace Data
