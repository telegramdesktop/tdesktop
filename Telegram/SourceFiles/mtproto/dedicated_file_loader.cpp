/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/dedicated_file_loader.h"

#include "mtproto/facade.h"
#include "main/main_account.h" // Account::sessionChanges.
#include "main/main_session.h" // Session::account.
#include "core/application.h"
#include "base/call_delayed.h"

namespace MTP {
namespace {

std::optional<MTPInputChannel> ExtractChannel(
		const MTPcontacts_ResolvedPeer &result) {
	const auto &data = result.c_contacts_resolvedPeer();
	if (const auto peer = peerFromMTP(data.vpeer())) {
		for (const auto &chat : data.vchats().v) {
			if (chat.type() == mtpc_channel) {
				const auto &channel = chat.c_channel();
				if (peer == peerFromChannel(channel.vid())) {
					return MTP_inputChannel(
						channel.vid(),
						MTP_long(channel.vaccess_hash().value_or_empty()));
				}
			}
		}
	}
	return std::nullopt;
}

std::optional<DedicatedLoader::File> ParseFile(
		const MTPmessages_Messages &result) {
	const auto message = GetMessagesElement(result);
	if (!message || message->type() != mtpc_message) {
		LOG(("Update Error: MTP file message not found."));
		return std::nullopt;
	}
	const auto &data = message->c_message();
	const auto media = data.vmedia();
	if (!media || media->type() != mtpc_messageMediaDocument) {
		LOG(("Update Error: MTP file media not found."));
		return std::nullopt;
	}
	const auto &inner = media->c_messageMediaDocument();
	const auto document = inner.vdocument();
	if (!document || document->type() != mtpc_document) {
		LOG(("Update Error: MTP file not found."));
		return std::nullopt;
	}
	const auto &fields = document->c_document();
	const auto name = [&] {
		for (const auto &attribute : fields.vattributes().v) {
			if (attribute.type() == mtpc_documentAttributeFilename) {
				const auto &data = attribute.c_documentAttributeFilename();
				return qs(data.vfile_name());
			}
		}
		return QString();
	}();
	if (name.isEmpty()) {
		LOG(("Update Error: MTP file name not found."));
		return std::nullopt;
	}
	const auto size = fields.vsize().v;
	if (size <= 0) {
		LOG(("Update Error: MTP file size is invalid."));
		return std::nullopt;
	}
	const auto location = MTP_inputDocumentFileLocation(
		fields.vid(),
		fields.vaccess_hash(),
		fields.vfile_reference(),
		MTP_string());
	return DedicatedLoader::File{ name, size, fields.vdc_id().v, location };
}

} // namespace

WeakInstance::WeakInstance(base::weak_ptr<Main::Session> session)
: _session(session)
, _instance(_session ? &_session->account().mtp() : nullptr) {
	if (!valid()) {
		return;
	}

	connect(_instance, &QObject::destroyed, this, [=] {
		_instance = nullptr;
		_session = nullptr;
		die();
	});
	_session->account().sessionChanges(
	) | rpl::filter([](Main::Session *session) {
		return !session;
	}) | rpl::start_with_next([=] {
		die();
	}, _lifetime);
}

base::weak_ptr<Main::Session> WeakInstance::session() const {
	return _session;
}

bool WeakInstance::valid() const {
	return (_session != nullptr);
}

Instance *WeakInstance::instance() const {
	return _instance;
}

void WeakInstance::die() {
	for (const auto &[requestId, fail] : base::take(_requests)) {
		if (_instance) {
			_instance->cancel(requestId);
		}
		fail(Error::Local(
			"UNAVAILABLE",
			"MTP instance is not available."));
	}
}

bool WeakInstance::removeRequest(mtpRequestId requestId) {
	if (const auto i = _requests.find(requestId); i != end(_requests)) {
		_requests.erase(i);
		return true;
	}
	return false;
}

void WeakInstance::reportUnavailable(
		Fn<void(const Error &error)> callback) {
	InvokeQueued(this, [=] {
		callback(Error::Local(
			"UNAVAILABLE",
			"MTP instance is not available."));
	});
}

WeakInstance::~WeakInstance() {
	if (_instance) {
		for (const auto &[requestId, fail] : base::take(_requests)) {
			_instance->cancel(requestId);
		}
	}
}

AbstractDedicatedLoader::AbstractDedicatedLoader(
	const QString &filepath,
	int chunkSize)
: _filepath(filepath)
, _chunkSize(chunkSize) {
}

void AbstractDedicatedLoader::start() {
	if (!validateOutput()
		|| (!_output.isOpen() && !_output.open(QIODevice::Append))) {
		QFile(_filepath).remove();
		threadSafeFailed();
		return;
	}

	LOG(("Update Info: Starting loading '%1' from %2 offset."
		).arg(_filepath
		).arg(alreadySize()));
	startLoading();
}

int AbstractDedicatedLoader::alreadySize() const {
	QMutexLocker lock(&_sizesMutex);
	return _alreadySize;
}

int AbstractDedicatedLoader::totalSize() const {
	QMutexLocker lock(&_sizesMutex);
	return _totalSize;
}

rpl::producer<QString> AbstractDedicatedLoader::ready() const {
	return _ready.events();
}

auto AbstractDedicatedLoader::progress() const -> rpl::producer<Progress> {
	return _progress.events();
}

rpl::producer<> AbstractDedicatedLoader::failed() const {
	return _failed.events();
}

void AbstractDedicatedLoader::wipeFolder() {
	QFileInfo info(_filepath);
	const auto dir = info.dir();
	const auto all = dir.entryInfoList(QDir::Files);
	for (auto i = all.begin(), e = all.end(); i != e; ++i) {
		if (i->absoluteFilePath() != info.absoluteFilePath()) {
			QFile::remove(i->absoluteFilePath());
		}
	}
}

bool AbstractDedicatedLoader::validateOutput() {
	if (_filepath.isEmpty()) {
		return false;
	}

	QFileInfo info(_filepath);
	const auto dir = info.dir();
	if (!dir.exists()) {
		dir.mkdir(dir.absolutePath());
	}
	_output.setFileName(_filepath);

	if (!info.exists()) {
		return true;
	}
	const auto fullSize = info.size();
	if (fullSize < _chunkSize || fullSize > kMaxFileSize) {
		return _output.remove();
	}
	const auto goodSize = int((fullSize % _chunkSize)
		? (fullSize - (fullSize % _chunkSize))
		: fullSize);
	if (_output.resize(goodSize)) {
		_alreadySize = goodSize;
		return true;
	}
	return false;
}

void AbstractDedicatedLoader::threadSafeProgress(Progress progress) {
	crl::on_main(this, [=] {
		_progress.fire_copy(progress);
	});
}

void AbstractDedicatedLoader::threadSafeReady() {
	crl::on_main(this, [=] {
		_ready.fire_copy(_filepath);
	});
}

void AbstractDedicatedLoader::threadSafeFailed() {
	crl::on_main(this, [=] {
		_failed.fire({});
	});
}

void AbstractDedicatedLoader::writeChunk(bytes::const_span data, int totalSize) {
	const auto size = data.size();
	if (size > 0) {
		const auto written = _output.write(QByteArray::fromRawData(
			reinterpret_cast<const char*>(data.data()),
			size));
		if (written != size) {
			threadSafeFailed();
			return;
		}
	}

	const auto progress = [&] {
		QMutexLocker lock(&_sizesMutex);
		if (!_totalSize) {
			_totalSize = totalSize;
		}
		_alreadySize += size;
		return Progress { _alreadySize, _totalSize };
	}();

	if (progress.size > 0 && progress.already >= progress.size) {
		_output.close();
		threadSafeReady();
	} else {
		threadSafeProgress(progress);
	}
}

rpl::lifetime &AbstractDedicatedLoader::lifetime() {
	return _lifetime;
}

DedicatedLoader::DedicatedLoader(
	base::weak_ptr<Main::Session> session,
	const QString &folder,
	const File &file)
: AbstractDedicatedLoader(folder + '/' + file.name, kChunkSize)
, _size(file.size)
, _dcId(file.dcId)
, _location(file.location)
, _mtp(session) {
	Expects(_size > 0);
}

void DedicatedLoader::startLoading() {
	if (!_mtp.valid()) {
		LOG(("Update Error: MTP is unavailable."));
		threadSafeFailed();
		return;
	}

	LOG(("Update Info: Loading using MTP from '%1'.").arg(_dcId));
	_offset = alreadySize();
	writeChunk({}, _size);
	sendRequest();
}

void DedicatedLoader::sendRequest() {
	if (_requests.size() >= kRequestsCount || _offset >= _size) {
		return;
	}
	const auto offset = _offset;
	_requests.push_back({ offset });
	_mtp.send(
		MTPupload_GetFile(
			MTP_flags(0),
			_location,
			MTP_int(offset),
			MTP_int(kChunkSize)),
		[=](const MTPupload_File &result) { gotPart(offset, result); },
		failHandler(),
		MTP::updaterDcId(_dcId));
	_offset += kChunkSize;

	if (_requests.size() < kRequestsCount) {
		base::call_delayed(kNextRequestDelay, this, [=] { sendRequest(); });
	}
}

void DedicatedLoader::gotPart(int offset, const MTPupload_File &result) {
	Expects(!_requests.empty());

	if (result.type() == mtpc_upload_fileCdnRedirect) {
		LOG(("Update Error: MTP does not support cdn right now."));
		threadSafeFailed();
		return;
	}
	const auto &data = result.c_upload_file();
	if (data.vbytes().v.isEmpty()) {
		LOG(("Update Error: MTP empty part received."));
		threadSafeFailed();
		return;
	}

	const auto i = ranges::find(
		_requests,
		offset,
		[](const Request &request) { return request.offset; });
	Assert(i != end(_requests));

	i->bytes = data.vbytes().v;
	while (!_requests.empty() && !_requests.front().bytes.isEmpty()) {
		writeChunk(bytes::make_span(_requests.front().bytes), _size);
		_requests.pop_front();
	}
	sendRequest();
}

Fn<void(const Error &)> DedicatedLoader::failHandler() {
	return [=](const Error &error) {
		LOG(("Update Error: MTP load failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		threadSafeFailed();
	};
}

void ResolveChannel(
		not_null<MTP::WeakInstance*> mtp,
		const QString &username,
		Fn<void(const MTPInputChannel &channel)> done,
		Fn<void()> fail) {
	const auto failed = [&] {
		LOG(("Dedicated MTP Error: Channel '%1' resolve failed."
			).arg(username));
		fail();
	};
	const auto session = mtp->session();
	if (!mtp->valid()) {
		failed();
		return;
	}

	struct ResolveResult {
		base::weak_ptr<Main::Session> session;
		MTPInputChannel channel;
	};
	static std::map<QString, ResolveResult> ResolveCache;

	const auto i = ResolveCache.find(username);
	if (i != end(ResolveCache)) {
		if (i->second.session.get() == session.get()) {
			done(i->second.channel);
			return;
		}
		ResolveCache.erase(i);
	}

	const auto doneHandler = [=](const MTPcontacts_ResolvedPeer &result) {
		Expects(result.type() == mtpc_contacts_resolvedPeer);

		if (const auto channel = ExtractChannel(result)) {
			ResolveCache.emplace(
				username,
				ResolveResult { session, *channel });
			done(*channel);
		} else {
			failed();
		}
	};
	const auto failHandler = [=](const Error &error) {
		LOG(("Dedicated MTP Error: Resolve failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		fail();
	};
	mtp->send(
		MTPcontacts_ResolveUsername(MTP_string(username)),
		doneHandler,
		failHandler);
}

std::optional<MTPMessage> GetMessagesElement(
		const MTPmessages_Messages &list) {
	return list.match([&](const MTPDmessages_messagesNotModified &) {
		return std::optional<MTPMessage>(std::nullopt);
	}, [&](const auto &data) {
		return data.vmessages().v.isEmpty()
			? std::nullopt
			: std::make_optional(data.vmessages().v[0]);
	});
}

void StartDedicatedLoader(
		not_null<MTP::WeakInstance*> mtp,
		const DedicatedLoader::Location &location,
		const QString &folder,
		Fn<void(std::unique_ptr<DedicatedLoader>)> ready) {
	const auto doneHandler = [=](const MTPmessages_Messages &result) {
		const auto file = ParseFile(result);
		ready(file
			? std::make_unique<MTP::DedicatedLoader>(
				mtp->session(),
				folder,
				*file)
			: nullptr);
	};
	const auto failHandler = [=](const Error &error) {
		LOG(("Update Error: MTP check failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		ready(nullptr);
	};

	const auto [username, postId] = location;
	ResolveChannel(mtp, username, [=, postId = postId](
			const MTPInputChannel &channel) {
		mtp->send(
			MTPchannels_GetMessages(
				channel,
				MTP_vector<MTPInputMessage>(
					1,
					MTP_inputMessageID(MTP_int(postId)))),
			doneHandler,
			failHandler);
	}, [=] { ready(nullptr); });
}

} // namespace MTP
