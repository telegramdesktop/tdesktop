/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_api_wrap.h"

#include "export/export_settings.h"
#include "export/data/export_data_types.h"
#include "export/output/export_output_result.h"
#include "export/output/export_output_file.h"
#include "mtproto/mtproto_rpc_sender.h"
#include "base/value_ordering.h"
#include "base/bytes.h"
#include <set>
#include <deque>

namespace Export {
namespace {

constexpr auto kUserpicsSliceLimit = 100;
constexpr auto kFileChunkSize = 128 * 1024;
constexpr auto kFileRequestsCount = 2;
constexpr auto kFileNextRequestDelay = crl::time(20);
constexpr auto kChatsSliceLimit = 100;
constexpr auto kMessagesSliceLimit = 100;
constexpr auto kTopPeerSliceLimit = 100;
constexpr auto kFileMaxSize = 2000 * 1024 * 1024;
constexpr auto kLocationCacheSize = 100'000;

struct LocationKey {
	uint64 type;
	uint64 id;

	inline bool operator<(const LocationKey &other) const {
		return std::tie(type, id) < std::tie(other.type, other.id);
	}
};

std::tuple<const uint64 &, const uint64 &> value_ordering_helper(const LocationKey &value) {
	return std::tie(
		value.type,
		value.id);
}

LocationKey ComputeLocationKey(const Data::FileLocation &value) {
	auto result = LocationKey();
	result.type = value.dcId;
	value.data.match([&](const MTPDinputFileLocation &data) {
		result.type |= (1ULL << 24);
		result.type |= (uint64(uint32(data.vlocal_id().v)) << 32);
		result.id = data.vvolume_id().v;
	}, [&](const MTPDinputDocumentFileLocation &data) {
		const auto letter = data.vthumb_size().v.isEmpty()
			? char(0)
			: data.vthumb_size().v[0];
		result.type |= (2ULL << 24);
		result.type |= (uint64(uint32(letter)) << 16);
		result.id = data.vid().v;
	}, [&](const MTPDinputSecureFileLocation &data) {
		result.type |= (3ULL << 24);
		result.id = data.vid().v;
	}, [&](const MTPDinputEncryptedFileLocation &data) {
		result.type |= (4ULL << 24);
		result.id = data.vid().v;
	}, [&](const MTPDinputTakeoutFileLocation &data) {
		result.type |= (5ULL << 24);
	}, [&](const MTPDinputPhotoFileLocation &data) {
		const auto letter = data.vthumb_size().v.isEmpty()
			? char(0)
			: data.vthumb_size().v[0];
		result.type |= (6ULL << 24);
		result.type |= (uint64(uint32(letter)) << 16);
		result.id = data.vid().v;
	}, [&](const MTPDinputPeerPhotoFileLocation &data) {
		const auto letter = data.is_big() ? char(1) : char(0);
		result.type |= (7ULL << 24);
		result.type |= (uint64(uint32(data.vlocal_id().v)) << 32);
		result.type |= (uint64(uint32(letter)) << 16);
		result.id = data.vvolume_id().v;
	}, [&](const MTPDinputStickerSetThumb &data) {
		result.type |= (8ULL << 24);
		result.type |= (uint64(uint32(data.vlocal_id().v)) << 32);
		result.id = data.vvolume_id().v;
	}, [&](const MTPDinputPhotoLegacyFileLocation &data) {
		result.type |= (9ULL << 24);
		result.type |= (uint64(uint32(data.vlocal_id().v)) << 32);
		result.id = data.vvolume_id().v;
	});
	return result;
}

Settings::Type SettingsFromDialogsType(Data::DialogInfo::Type type) {
	using DialogType = Data::DialogInfo::Type;
	switch (type) {
	case DialogType::Self:
	case DialogType::Personal:
		return Settings::Type::PersonalChats;
	case DialogType::Bot:
		return Settings::Type::BotChats;
	case DialogType::PrivateGroup:
	case DialogType::PrivateSupergroup:
		return Settings::Type::PrivateGroups;
	case DialogType::PublicSupergroup:
		return Settings::Type::PublicGroups;
	case DialogType::PrivateChannel:
		return Settings::Type::PrivateChannels;
	case DialogType::PublicChannel:
		return Settings::Type::PublicChannels;
	}
	return Settings::Type(0);
}

} // namespace

class ApiWrap::LoadedFileCache {
public:
	using Location = Data::FileLocation;

	LoadedFileCache(int limit);

	void save(const Location &location, const QString &relativePath);
	std::optional<QString> find(const Location &location) const;

private:
	int _limit = 0;
	std::map<LocationKey, QString> _map;
	std::deque<LocationKey> _list;

};

struct ApiWrap::StartProcess {
	FnMut<void(StartInfo)> done;

	enum class Step {
		UserpicsCount,
		SplitRanges,
		DialogsCount,
		LeftChannelsCount,
	};
	std::deque<Step> steps;
	int splitIndex = 0;
	StartInfo info;
};

struct ApiWrap::ContactsProcess {
	FnMut<void(Data::ContactsList&&)> done;

	Data::ContactsList result;

	int topPeersOffset = 0;
};

struct ApiWrap::UserpicsProcess {
	FnMut<bool(Data::UserpicsInfo&&)> start;
	Fn<bool(DownloadProgress)> fileProgress;
	Fn<bool(Data::UserpicsSlice&&)> handleSlice;
	FnMut<void()> finish;

	int processed = 0;
	std::optional<Data::UserpicsSlice> slice;
	uint64 maxId = 0;
	bool lastSlice = false;
	int fileIndex = 0;
};

struct ApiWrap::OtherDataProcess {
	Data::File file;
	FnMut<void(Data::File&&)> done;
};

struct ApiWrap::FileProcess {
	FileProcess(const QString &path, Output::Stats *stats);

	Output::File file;
	QString relativePath;

	Fn<bool(FileProgress)> progress;
	FnMut<void(const QString &relativePath)> done;

	Data::FileLocation location;
	Data::FileOrigin origin;
	int offset = 0;
	int size = 0;

	struct Request {
		int offset = 0;
		QByteArray bytes;
	};
	std::deque<Request> requests;
};

struct ApiWrap::FileProgress {
	int ready = 0;
	int total = 0;
};

struct ApiWrap::ChatsProcess {
	Fn<bool(int count)> progress;
	FnMut<void(Data::DialogsInfo&&)> done;

	Data::DialogsInfo info;
	int processedCount = 0;
	std::map<Data::PeerId, int> indexByPeer;
};

struct ApiWrap::LeftChannelsProcess : ChatsProcess {
	int fullCount = 0;
	int offset = 0;
	bool finished = false;
};

struct ApiWrap::DialogsProcess : ChatsProcess {
	int splitIndexPlusOne = 0;
	TimeId offsetDate = 0;
	int32 offsetId = 0;
	MTPInputPeer offsetPeer = MTP_inputPeerEmpty();
};

struct ApiWrap::ChatProcess {
	Data::DialogInfo info;

	FnMut<bool(const Data::DialogInfo &)> start;
	Fn<bool(DownloadProgress)> fileProgress;
	Fn<bool(Data::MessagesSlice&&)> handleSlice;
	FnMut<void()> done;

	FnMut<void(MTPmessages_Messages&&)> requestDone;

	int localSplitIndex = 0;
	int32 largestIdPlusOne = 1;

	Data::ParseMediaContext context;
	std::optional<Data::MessagesSlice> slice;
	bool lastSlice = false;
	int fileIndex = 0;
};


template <typename Request>
class ApiWrap::RequestBuilder {
public:
	using Original = MTP::ConcurrentSender::SpecificRequestBuilder<Request>;
	using Response = typename Request::ResponseType;

	RequestBuilder(
		Original &&builder,
		FnMut<void(RPCError&&)> commonFailHandler);

	[[nodiscard]] RequestBuilder &done(FnMut<void()> &&handler);
	[[nodiscard]] RequestBuilder &done(
		FnMut<void(Response &&)> &&handler);
	[[nodiscard]] RequestBuilder &fail(
		FnMut<bool(const RPCError &)> &&handler);

	mtpRequestId send();

private:
	Original _builder;
	FnMut<void(RPCError&&)> _commonFailHandler;

};

template <typename Request>
ApiWrap::RequestBuilder<Request>::RequestBuilder(
	Original &&builder,
	FnMut<void(RPCError&&)> commonFailHandler)
: _builder(std::move(builder))
, _commonFailHandler(std::move(commonFailHandler)) {
}

template <typename Request>
auto ApiWrap::RequestBuilder<Request>::done(
	FnMut<void()> &&handler
) -> RequestBuilder& {
	if (handler) {
		auto &silence_warning = _builder.done(std::move(handler));
	}
	return *this;
}

template <typename Request>
auto ApiWrap::RequestBuilder<Request>::done(
	FnMut<void(Response &&)> &&handler
) -> RequestBuilder& {
	if (handler) {
		auto &silence_warning = _builder.done(std::move(handler));
	}
	return *this;
}

template <typename Request>
auto ApiWrap::RequestBuilder<Request>::fail(
	FnMut<bool(const RPCError &)> &&handler
) -> RequestBuilder& {
	if (handler) {
		auto &silence_warning = _builder.fail([
			common = base::take(_commonFailHandler),
			specific = std::move(handler)
		](RPCError &&error) mutable {
			if (!specific(error)) {
				common(std::move(error));
			}
		});
	}
	return *this;
}

template <typename Request>
mtpRequestId ApiWrap::RequestBuilder<Request>::send() {
	return _commonFailHandler
		? _builder.fail(base::take(_commonFailHandler)).send()
		: _builder.send();
}

ApiWrap::LoadedFileCache::LoadedFileCache(int limit) : _limit(limit) {
	Expects(limit >= 0);
}

void ApiWrap::LoadedFileCache::save(
		const Location &location,
		const QString &relativePath) {
	if (!location) {
		return;
	}
	const auto key = ComputeLocationKey(location);
	_map[key] = relativePath;
	_list.push_back(key);
	if (_list.size() > _limit) {
		const auto key = _list.front();
		_list.pop_front();
		_map.erase(key);
	}
}

std::optional<QString> ApiWrap::LoadedFileCache::find(
		const Location &location) const {
	if (!location) {
		return std::nullopt;
	}
	const auto key = ComputeLocationKey(location);
	if (const auto i = _map.find(key); i != end(_map)) {
		return i->second;
	}
	return std::nullopt;
}

ApiWrap::FileProcess::FileProcess(const QString &path, Output::Stats *stats)
: file(path, stats) {
}

template <typename Request>
auto ApiWrap::mainRequest(Request &&request) {
	Expects(_takeoutId.has_value());

	auto original = std::move(_mtp.request(MTPInvokeWithTakeout<Request>(
		MTP_long(*_takeoutId),
		std::forward<Request>(request)
	)).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)));

	return RequestBuilder<MTPInvokeWithTakeout<Request>>(
		std::move(original),
		[=](RPCError &&result) { error(std::move(result)); });
}

template <typename Request>
auto ApiWrap::splitRequest(int index, Request &&request) {
	Expects(index < _splits.size());

	//if (index == _splits.size() - 1) {
	//	return mainRequest(std::forward<Request>(request));
	//}
	return mainRequest(MTPInvokeWithMessagesRange<Request>(
		_splits[index],
		std::forward<Request>(request)));
}

auto ApiWrap::fileRequest(const Data::FileLocation &location, int offset) {
	Expects(location.dcId != 0
		|| location.data.type() == mtpc_inputTakeoutFileLocation);
	Expects(_takeoutId.has_value());

	return std::move(_mtp.request(MTPInvokeWithTakeout<MTPupload_GetFile>(
		MTP_long(*_takeoutId),
		MTPupload_GetFile(
			MTP_flags(0),
			location.data,
			MTP_int(offset),
			MTP_int(kFileChunkSize))
	)).fail([=](RPCError &&result) {
		if (result.type() == qstr("TAKEOUT_FILE_EMPTY")
			&& _otherDataProcess != nullptr) {
			filePartDone(
				0,
				MTP_upload_file(
					MTP_storage_filePartial(),
					MTP_int(0),
					MTP_bytes()));
		} else if (result.type() == qstr("LOCATION_INVALID")
			|| result.type() == qstr("VERSION_INVALID")) {
			filePartUnavailable();
		} else if (result.code() == 400
			&& result.type().startsWith(qstr("FILE_REFERENCE_"))) {
			filePartRefreshReference(offset);
		} else {
			error(std::move(result));
		}
	}).toDC(MTP::ShiftDcId(location.dcId, MTP::kExportMediaDcShift)));
}

ApiWrap::ApiWrap(QPointer<MTP::Instance> weak, Fn<void(FnMut<void()>)> runner)
: _mtp(weak, std::move(runner))
, _fileCache(std::make_unique<LoadedFileCache>(kLocationCacheSize)) {
}

rpl::producer<RPCError> ApiWrap::errors() const {
	return _errors.events();
}

rpl::producer<Output::Result> ApiWrap::ioErrors() const {
	return _ioErrors.events();
}

void ApiWrap::startExport(
		const Settings &settings,
		Output::Stats *stats,
		FnMut<void(StartInfo)> done) {
	Expects(_settings == nullptr);
	Expects(_startProcess == nullptr);

	_settings = std::make_unique<Settings>(settings);
	_stats = stats;
	_startProcess = std::make_unique<StartProcess>();
	_startProcess->done = std::move(done);

	using Step = StartProcess::Step;
	if (_settings->types & Settings::Type::Userpics) {
		_startProcess->steps.push_back(Step::UserpicsCount);
	}
	if (_settings->types & Settings::Type::AnyChatsMask) {
		_startProcess->steps.push_back(Step::SplitRanges);
	}
	if (_settings->types & Settings::Type::AnyChatsMask) {
		_startProcess->steps.push_back(Step::DialogsCount);
	}
	if (_settings->types & Settings::Type::GroupsChannelsMask) {
		if (!_settings->onlySinglePeer()) {
			_startProcess->steps.push_back(Step::LeftChannelsCount);
		}
	}
	startMainSession([=] {
		sendNextStartRequest();
	});
}

void ApiWrap::sendNextStartRequest() {
	Expects(_startProcess != nullptr);

	auto &steps = _startProcess->steps;
	if (steps.empty()) {
		finishStartProcess();
		return;
	}
	using Step = StartProcess::Step;
	const auto step = steps.front();
	steps.pop_front();
	switch (step) {
	case Step::UserpicsCount:
		return requestUserpicsCount();
	case Step::SplitRanges:
		return requestSplitRanges();
	case Step::DialogsCount:
		return requestDialogsCount();
	case Step::LeftChannelsCount:
		return requestLeftChannelsCount();
	}
	Unexpected("Step in ApiWrap::sendNextStartRequest.");
}

void ApiWrap::requestUserpicsCount() {
	Expects(_startProcess != nullptr);

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0),  // offset
		MTP_long(0), // max_id
		MTP_int(0)   // limit
	)).done([=](const MTPphotos_Photos &result) {
		Expects(_settings != nullptr);
		Expects(_startProcess != nullptr);

		_startProcess->info.userpicsCount = result.match(
		[](const MTPDphotos_photos &data) {
			return int(data.vphotos().v.size());
		}, [](const MTPDphotos_photosSlice &data) {
			return data.vcount().v;
		});

		sendNextStartRequest();
	}).send();
}

void ApiWrap::requestSplitRanges() {
	Expects(_startProcess != nullptr);

	mainRequest(MTPmessages_GetSplitRanges(
	)).done([=](const MTPVector<MTPMessageRange> &result) {
		_splits = result.v;
		if (_splits.empty()) {
			_splits.push_back(MTP_messageRange(
				MTP_int(1),
				MTP_int(std::numeric_limits<int>::max())));
		}
		_startProcess->splitIndex = useOnlyLastSplit()
			? (_splits.size() - 1)
			: 0;

		sendNextStartRequest();
	}).send();
}

void ApiWrap::requestDialogsCount() {
	Expects(_startProcess != nullptr);

	if (_settings->onlySinglePeer()) {
		_startProcess->info.dialogsCount =
			(_settings->singlePeer.type() == mtpc_inputPeerChannel
				? 1
				: _splits.size());
		sendNextStartRequest();
		return;
	}

	const auto offsetDate = 0;
	const auto offsetId = 0;
	const auto offsetPeer = MTP_inputPeerEmpty();
	const auto limit = 1;
	const auto hash = 0;
	splitRequest(_startProcess->splitIndex, MTPmessages_GetDialogs(
		MTP_flags(0),
		MTPint(), // folder_id
		MTP_int(offsetDate),
		MTP_int(offsetId),
		offsetPeer,
		MTP_int(limit),
		MTP_int(hash)
	)).done([=](const MTPmessages_Dialogs &result) {
		Expects(_settings != nullptr);
		Expects(_startProcess != nullptr);

		const auto count = result.match(
		[](const MTPDmessages_dialogs &data) {
			return int(data.vdialogs().v.size());
		}, [](const MTPDmessages_dialogsSlice &data) {
			return data.vcount().v;
		}, [](const MTPDmessages_dialogsNotModified &data) {
			return -1;
		});
		if (count < 0) {
			error("Unexpected dialogsNotModified received.");
			return;
		}
		_startProcess->info.dialogsCount += count;

		if (++_startProcess->splitIndex >= _splits.size()) {
			sendNextStartRequest();
		} else {
			requestDialogsCount();
		}
	}).send();
}

void ApiWrap::requestLeftChannelsCount() {
	Expects(_startProcess != nullptr);
	Expects(_leftChannelsProcess == nullptr);

	_leftChannelsProcess = std::make_unique<LeftChannelsProcess>();
	requestLeftChannelsSliceGeneric([=] {
		Expects(_startProcess != nullptr);
		Expects(_leftChannelsProcess != nullptr);

		_startProcess->info.dialogsCount
			+= _leftChannelsProcess->fullCount;
		sendNextStartRequest();
	});
}

void ApiWrap::finishStartProcess() {
	Expects(_startProcess != nullptr);

	const auto process = base::take(_startProcess);
	process->done(process->info);
}

bool ApiWrap::useOnlyLastSplit() const {
	return !(_settings->types & Settings::Type::NonChannelChatsMask);
}

void ApiWrap::requestLeftChannelsList(
		Fn<bool(int count)> progress,
		FnMut<void(Data::DialogsInfo&&)> done) {
	Expects(_leftChannelsProcess != nullptr);

	_leftChannelsProcess->progress = std::move(progress);
	_leftChannelsProcess->done = std::move(done);
	requestLeftChannelsSlice();
}

void ApiWrap::requestLeftChannelsSlice() {
	requestLeftChannelsSliceGeneric([=] {
		Expects(_leftChannelsProcess != nullptr);

		if (_leftChannelsProcess->finished) {
			const auto process = base::take(_leftChannelsProcess);
			process->done(std::move(process->info));
		} else {
			requestLeftChannelsSlice();
		}
	});
}

void ApiWrap::requestDialogsList(
		Fn<bool(int count)> progress,
		FnMut<void(Data::DialogsInfo&&)> done) {
	Expects(_dialogsProcess == nullptr);

	_dialogsProcess = std::make_unique<DialogsProcess>();
	_dialogsProcess->splitIndexPlusOne = _splits.size();
	_dialogsProcess->progress = std::move(progress);
	_dialogsProcess->done = std::move(done);

	requestDialogsSlice();
}

void ApiWrap::startMainSession(FnMut<void()> done) {
	using Type = Settings::Type;
	const auto sizeLimit = _settings->media.sizeLimit;
	const auto hasFiles = ((_settings->media.types != 0) && (sizeLimit > 0))
		|| (_settings->types & Type::Userpics);

	using Flag = MTPaccount_InitTakeoutSession::Flag;
	const auto flags = Flag(0)
		| (_settings->types & Type::Contacts ? Flag::f_contacts : Flag(0))
		| (hasFiles ? Flag::f_files : Flag(0))
		| ((hasFiles && sizeLimit < kFileMaxSize)
			? Flag::f_file_max_size
			: Flag(0))
		| (_settings->types & (Type::PersonalChats | Type::BotChats)
			? Flag::f_message_users
			: Flag(0))
		| (_settings->types & Type::PrivateGroups
			? (Flag::f_message_chats | Flag::f_message_megagroups)
			: Flag(0))
		| (_settings->types & Type::PublicGroups
			? Flag::f_message_megagroups
			: Flag(0))
		| (_settings->types & (Type::PrivateChannels | Type::PublicChannels)
			? Flag::f_message_channels
			: Flag(0));

	_mtp.request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done([=, done = std::move(done)](
			const MTPVector<MTPUser> &result) mutable {
		for (const auto &user : result.v) {
			user.match([&](const MTPDuser &data) {
				if (data.is_self()) {
					_selfId = data.vid().v;
				}
			}, [&](const MTPDuserEmpty&) {
			});
		}
		if (!_selfId) {
			error("Could not retrieve selfId.");
			return;
		}
		_mtp.request(MTPaccount_InitTakeoutSession(
			MTP_flags(flags),
			MTP_int(sizeLimit)
		)).done([=, done = std::move(done)](
				const MTPaccount_Takeout &result) mutable {
			_takeoutId = result.match([](const MTPDaccount_takeout &data) {
				return data.vid().v;
			});
			done();
		}).fail([=](RPCError &&result) {
			error(std::move(result));
		}).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
	}).fail([=](RPCError &&result) {
		error(std::move(result));
	}).send();
}

void ApiWrap::requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done) {
	mainRequest(MTPusers_GetFullUser(
		_user
	)).done([=, done = std::move(done)](const MTPUserFull &result) mutable {
		Expects(result.type() == mtpc_userFull);

		const auto &full = result.c_userFull();
		if (full.vuser().type() == mtpc_user) {
			done(Data::ParsePersonalInfo(result));
		} else {
			error("Bad user type.");
		}
	}).send();
}

void ApiWrap::requestOtherData(
		const QString &suggestedPath,
		FnMut<void(Data::File&&)> done) {
	Expects(_otherDataProcess == nullptr);

	_otherDataProcess = std::make_unique<OtherDataProcess>();
	_otherDataProcess->done = std::move(done);
	_otherDataProcess->file.location.data = MTP_inputTakeoutFileLocation();
	_otherDataProcess->file.suggestedPath = suggestedPath;
	loadFile(
		_otherDataProcess->file,
		Data::FileOrigin(),
		[](FileProgress progress) { return true; },
		[=](const QString &result) { otherDataDone(result); });
}

void ApiWrap::otherDataDone(const QString &relativePath) {
	Expects(_otherDataProcess != nullptr);

	const auto process = base::take(_otherDataProcess);
	process->file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		process->file.skipReason = Data::File::SkipReason::Unavailable;
	}
	process->done(std::move(process->file));
}

void ApiWrap::requestUserpics(
		FnMut<bool(Data::UserpicsInfo&&)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish) {
	Expects(_userpicsProcess == nullptr);

	_userpicsProcess = std::make_unique<UserpicsProcess>();
	_userpicsProcess->start = std::move(start);
	_userpicsProcess->fileProgress = std::move(progress);
	_userpicsProcess->handleSlice = std::move(slice);
	_userpicsProcess->finish = std::move(finish);

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0), // offset
		MTP_long(_userpicsProcess->maxId),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) mutable {
		Expects(_userpicsProcess != nullptr);

		auto startInfo = result.match(
		[](const MTPDphotos_photos &data) {
			return Data::UserpicsInfo{ data.vphotos().v.size() };
		}, [](const MTPDphotos_photosSlice &data) {
			return Data::UserpicsInfo{ data.vcount().v };
		});
		if (!_userpicsProcess->start(std::move(startInfo))) {
			return;
		}

		handleUserpicsSlice(result);
	}).send();
}

void ApiWrap::handleUserpicsSlice(const MTPphotos_Photos &result) {
	Expects(_userpicsProcess != nullptr);

	result.match([&](const auto &data) {
		if constexpr (MTPDphotos_photos::Is<decltype(data)>()) {
			_userpicsProcess->lastSlice = true;
		}
		loadUserpicsFiles(Data::ParseUserpicsSlice(
			data.vphotos(),
			_userpicsProcess->processed));
	});
}

void ApiWrap::loadUserpicsFiles(Data::UserpicsSlice &&slice) {
	Expects(_userpicsProcess != nullptr);
	Expects(!_userpicsProcess->slice.has_value());

	if (slice.list.empty()) {
		_userpicsProcess->lastSlice = true;
	}
	_userpicsProcess->slice = std::move(slice);
	_userpicsProcess->fileIndex = 0;
	loadNextUserpic();
}

void ApiWrap::loadNextUserpic() {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());

	for (auto &list = _userpicsProcess->slice->list
		; _userpicsProcess->fileIndex < list.size()
		; ++_userpicsProcess->fileIndex) {
		const auto ready = processFileLoad(
			list[_userpicsProcess->fileIndex].image.file,
			Data::FileOrigin(),
			[=](FileProgress value) { return loadUserpicProgress(value); },
			[=](const QString &path) { loadUserpicDone(path); });
		if (!ready) {
			return;
		}
	}
	finishUserpicsSlice();
}

void ApiWrap::finishUserpicsSlice() {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());

	auto slice = *base::take(_userpicsProcess->slice);
	if (!slice.list.empty()) {
		_userpicsProcess->processed += slice.list.size();
		_userpicsProcess->maxId = slice.list.back().id;
		if (!_userpicsProcess->handleSlice(std::move(slice))) {
			return;
		}
	}
	if (_userpicsProcess->lastSlice) {
		finishUserpics();
		return;
	}

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0), // offset
		MTP_long(_userpicsProcess->maxId),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) {
		handleUserpicsSlice(result);
	}).send();
}

bool ApiWrap::loadUserpicProgress(FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());
	Expects((_userpicsProcess->fileIndex >= 0)
		&& (_userpicsProcess->fileIndex
			< _userpicsProcess->slice->list.size()));

	return _userpicsProcess->fileProgress(DownloadProgress{
		_fileProcess->relativePath,
		_userpicsProcess->fileIndex,
		progress.ready,
		progress.total });
}

void ApiWrap::loadUserpicDone(const QString &relativePath) {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());
	Expects((_userpicsProcess->fileIndex >= 0)
		&& (_userpicsProcess->fileIndex
			< _userpicsProcess->slice->list.size()));

	const auto index = _userpicsProcess->fileIndex;
	auto &file = _userpicsProcess->slice->list[index].image.file;
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextUserpic();
}

void ApiWrap::finishUserpics() {
	Expects(_userpicsProcess != nullptr);

	base::take(_userpicsProcess)->finish();
}

void ApiWrap::requestContacts(FnMut<void(Data::ContactsList&&)> done) {
	Expects(_contactsProcess == nullptr);

	_contactsProcess = std::make_unique<ContactsProcess>();
	_contactsProcess->done = std::move(done);
	mainRequest(MTPcontacts_GetSaved(
	)).done([=](const MTPVector<MTPSavedContact> &result) {
		_contactsProcess->result = Data::ParseContactsList(result);
		requestTopPeersSlice();
	}).send();
}

void ApiWrap::requestTopPeersSlice() {
	Expects(_contactsProcess != nullptr);

	using Flag = MTPcontacts_GetTopPeers::Flag;
	mainRequest(MTPcontacts_GetTopPeers(
		MTP_flags(Flag::f_correspondents
			| Flag::f_bots_inline
			| Flag::f_phone_calls),
		MTP_int(_contactsProcess->topPeersOffset),
		MTP_int(kTopPeerSliceLimit),
		MTP_int(0) // hash
	)).done([=](const MTPcontacts_TopPeers &result) {
		Expects(_contactsProcess != nullptr);

		if (!Data::AppendTopPeers(_contactsProcess->result, result)) {
			error("Unexpected data in ApiWrap::requestTopPeersSlice.");
			return;
		}

		const auto offset = _contactsProcess->topPeersOffset;
		const auto loaded = result.match(
		[](const MTPDcontacts_topPeersNotModified &data) {
			return true;
		}, [](const MTPDcontacts_topPeersDisabled &data) {
			return true;
		}, [&](const MTPDcontacts_topPeers &data) {
			for (const auto &category : data.vcategories().v) {
				const auto loaded = category.match(
				[&](const MTPDtopPeerCategoryPeers &data) {
					return offset + data.vpeers().v.size() >= data.vcount().v;
				});
				if (!loaded) {
					return false;
				}
			}
			return true;
		});

		if (loaded) {
			auto process = base::take(_contactsProcess);
			process->done(std::move(process->result));
		} else {
			_contactsProcess->topPeersOffset = std::max(std::max(
				_contactsProcess->result.correspondents.size(),
				_contactsProcess->result.inlineBots.size()),
				_contactsProcess->result.phoneCalls.size());
			requestTopPeersSlice();
		}
	}).send();
}

void ApiWrap::requestSessions(FnMut<void(Data::SessionsList&&)> done) {
	mainRequest(MTPaccount_GetAuthorizations(
	)).done([=, done = std::move(done)](
			const MTPaccount_Authorizations &result) mutable {
		auto list = Data::ParseSessionsList(result);
		mainRequest(MTPaccount_GetWebAuthorizations(
		)).done([=, done = std::move(done), list = std::move(list)](
				const MTPaccount_WebAuthorizations &result) mutable {
			list.webList = Data::ParseWebSessionsList(result).webList;
			done(std::move(list));
		}).send();
	}).send();
}

void ApiWrap::requestMessages(
		const Data::DialogInfo &info,
		FnMut<bool(const Data::DialogInfo &)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::MessagesSlice&&)> slice,
		FnMut<void()> done) {
	Expects(_chatProcess == nullptr);
	Expects(_selfId.has_value());

	_chatProcess = std::make_unique<ChatProcess>();
	_chatProcess->context.selfPeerId = Data::UserPeerId(*_selfId);
	_chatProcess->info = info;
	_chatProcess->start = std::move(start);
	_chatProcess->fileProgress = std::move(progress);
	_chatProcess->handleSlice = std::move(slice);
	_chatProcess->done = std::move(done);

	requestMessagesCount(0);
}

void ApiWrap::requestMessagesCount(int localSplitIndex) {
	Expects(_chatProcess != nullptr);
	Expects(localSplitIndex < _chatProcess->info.splits.size());

	requestChatMessages(
		_chatProcess->info.splits[localSplitIndex],
		0, // offset_id
		0, // add_offset
		1, // limit
		[=](const MTPmessages_Messages &result) {
		Expects(_chatProcess != nullptr);

		const auto count = result.match(
			[](const MTPDmessages_messages &data) {
			return data.vmessages().v.size();
		}, [](const MTPDmessages_messagesSlice &data) {
			return data.vcount().v;
		}, [](const MTPDmessages_channelMessages &data) {
			return data.vcount().v;
		}, [](const MTPDmessages_messagesNotModified &data) {
			return -1;
		});
		if (count < 0) {
			error("Unexpected messagesNotModified received.");
			return;
		}
		const auto skipSplit = !Data::SingleMessageAfter(
			result,
			_settings->singlePeerFrom);
		if (skipSplit) {
			// No messages from the requested range, skip this split.
			messagesCountLoaded(localSplitIndex, 0);
			return;
		}
		checkFirstMessageDate(localSplitIndex, count);
	});
}

void ApiWrap::checkFirstMessageDate(int localSplitIndex, int count) {
	Expects(_chatProcess != nullptr);
	Expects(localSplitIndex < _chatProcess->info.splits.size());

	if (_settings->singlePeerTill <= 0) {
		messagesCountLoaded(localSplitIndex, count);
		return;
	}

	// Request first message in this split to check if its' date < till.
	requestChatMessages(
		_chatProcess->info.splits[localSplitIndex],
		1, // offset_id
		-1, // add_offset
		1, // limit
		[=](const MTPmessages_Messages &result) {
		Expects(_chatProcess != nullptr);

		const auto skipSplit = !Data::SingleMessageBefore(
			result,
			_settings->singlePeerTill);
		messagesCountLoaded(localSplitIndex, skipSplit ? 0 : count);
	});
}

void ApiWrap::messagesCountLoaded(int localSplitIndex, int count) {
	Expects(_chatProcess != nullptr);
	Expects(localSplitIndex < _chatProcess->info.splits.size());

	_chatProcess->info.messagesCountPerSplit[localSplitIndex] = count;
	if (localSplitIndex + 1 < _chatProcess->info.splits.size()) {
		requestMessagesCount(localSplitIndex + 1);
	} else if (_chatProcess->start(_chatProcess->info)) {
		requestMessagesSlice();
	}
}

void ApiWrap::finishExport(FnMut<void()> done) {
	const auto guard = gsl::finally([&] { _takeoutId = std::nullopt; });

	mainRequest(MTPaccount_FinishTakeoutSession(
		MTP_flags(MTPaccount_FinishTakeoutSession::Flag::f_success)
	)).done(std::move(done)).send();
}

void ApiWrap::cancelExportFast() {
	if (_takeoutId.has_value()) {
		const auto requestId = mainRequest(MTPaccount_FinishTakeoutSession(
			MTP_flags(0)
		)).send();
		_mtp.request(requestId).detach();
	}
}

void ApiWrap::requestSinglePeerDialog() {
	auto doneSinglePeer = [=](const auto &result) {
		appendSinglePeerDialogs(
			Data::ParseDialogsInfo(_settings->singlePeer, result));
	};
	const auto requestUser = [&](const MTPInputUser &data) {
		mainRequest(MTPusers_GetUsers(
			MTP_vector<MTPInputUser>(1, data)
		)).done(std::move(doneSinglePeer)).send();
	};
	_settings->singlePeer.match([&](const MTPDinputPeerUser &data) {
		requestUser(MTP_inputUser(data.vuser_id(), data.vaccess_hash()));
	}, [&](const MTPDinputPeerChat &data) {
		mainRequest(MTPmessages_GetChats(
			MTP_vector<MTPint>(1, data.vchat_id())
		)).done(std::move(doneSinglePeer)).send();
	}, [&](const MTPDinputPeerChannel &data) {
		mainRequest(MTPchannels_GetChannels(
			MTP_vector<MTPInputChannel>(
				1,
				MTP_inputChannel(data.vchannel_id(), data.vaccess_hash()))
		)).done(std::move(doneSinglePeer)).send();
	}, [&](const MTPDinputPeerSelf &data) {
		requestUser(MTP_inputUserSelf());
	}, [&](const MTPDinputPeerUserFromMessage &data) {
		Unexpected("From message peer in ApiWrap::requestSinglePeerDialog.");
	}, [&](const MTPDinputPeerChannelFromMessage &data) {
		Unexpected("From message peer in ApiWrap::requestSinglePeerDialog.");
	}, [](const MTPDinputPeerEmpty &data) {
		Unexpected("Empty peer in ApiWrap::requestSinglePeerDialog.");
	});
}

mtpRequestId ApiWrap::requestSinglePeerMigrated(
		const Data::DialogInfo &info) {
	const auto input = info.input.match([&](
		const MTPDinputPeerChannel & data) {
		return MTP_inputChannel(
			data.vchannel_id(),
			data.vaccess_hash());
	}, [](auto&&) -> MTPinputChannel {
		Unexpected("Peer type in a supergroup.");
	});
	return mainRequest(MTPchannels_GetFullChannel(
		input
	)).done([=](const MTPmessages_ChatFull &result) {
		auto info = result.match([&](
				const MTPDmessages_chatFull &data) {
			const auto migratedChatId = data.vfull_chat().match([&](
					const MTPDchannelFull &data) {
				return data.vmigrated_from_chat_id().value_or_empty();
			}, [](auto &&other) {
				return 0;
			});
			return migratedChatId
				? Data::ParseDialogsInfo(
					MTP_inputPeerChat(MTP_int(migratedChatId)),
					MTP_messages_chats(data.vchats()))
				: Data::DialogsInfo();
		});
		appendSinglePeerDialogs(std::move(info));
	}).send();
}

void ApiWrap::appendSinglePeerDialogs(Data::DialogsInfo &&info) {
	const auto isSupergroupType = [](Data::DialogInfo::Type type) {
		using Type = Data::DialogInfo::Type;
		return (type == Type::PrivateSupergroup)
			|| (type == Type::PublicSupergroup);
	};
	const auto isChannelType = [](Data::DialogInfo::Type type) {
		using Type = Data::DialogInfo::Type;
		return (type == Type::PrivateChannel)
			|| (type == Type::PublicChannel);
	};

	auto migratedRequestId = mtpRequestId(0);
	const auto last = _dialogsProcess->splitIndexPlusOne - 1;
	for (auto &info : info.chats) {
		if (isSupergroupType(info.type) && !migratedRequestId) {
			migratedRequestId = requestSinglePeerMigrated(info);
			continue;
		} else if (isChannelType(info.type)) {
			continue;
		}
		for (auto i = last; i != 0; --i) {
			info.splits.push_back(i - 1);
			info.messagesCountPerSplit.push_back(0);
		}
	}

	if (!migratedRequestId) {
		_dialogsProcess->processedCount += info.chats.size();
	}
	appendDialogsSlice(std::move(info));

	if (migratedRequestId
		|| !_dialogsProcess->progress(_dialogsProcess->processedCount)) {
		return;
	}
	finishDialogsList();
}

void ApiWrap::requestDialogsSlice() {
	Expects(_dialogsProcess != nullptr);

	if (_settings->onlySinglePeer()) {
		requestSinglePeerDialog();
		return;
	}

	const auto splitIndex = _dialogsProcess->splitIndexPlusOne - 1;
	const auto hash = 0;
	splitRequest(splitIndex, MTPmessages_GetDialogs(
		MTP_flags(0),
		MTPint(), // folder_id
		MTP_int(_dialogsProcess->offsetDate),
		MTP_int(_dialogsProcess->offsetId),
		_dialogsProcess->offsetPeer,
		MTP_int(kChatsSliceLimit),
		MTP_int(hash)
	)).done([=](const MTPmessages_Dialogs &result) {
		if (result.type() == mtpc_messages_dialogsNotModified) {
			error("Unexpected dialogsNotModified received.");
			return;
		}
		auto finished = result.match(
		[](const MTPDmessages_dialogs &data) {
			return true;
		}, [](const MTPDmessages_dialogsSlice &data) {
			return data.vdialogs().v.isEmpty();
		}, [](const MTPDmessages_dialogsNotModified &data) {
			return true;
		});

		auto info = Data::ParseDialogsInfo(result);
		_dialogsProcess->processedCount += info.chats.size();
		const auto last = info.chats.empty()
			? Data::DialogInfo()
			: info.chats.back();
		appendDialogsSlice(std::move(info));

		if (!_dialogsProcess->progress(_dialogsProcess->processedCount)) {
			return;
		}

		if (!finished && last.topMessageDate > 0) {
			_dialogsProcess->offsetId = last.topMessageId;
			_dialogsProcess->offsetDate = last.topMessageDate;
			_dialogsProcess->offsetPeer = last.input;
		} else if (!useOnlyLastSplit()
			&& --_dialogsProcess->splitIndexPlusOne > 0) {
			_dialogsProcess->offsetId = 0;
			_dialogsProcess->offsetDate = 0;
			_dialogsProcess->offsetPeer = MTP_inputPeerEmpty();
		} else {
			requestLeftChannelsIfNeeded();
			return;
		}
		requestDialogsSlice();
	}).send();
}

void ApiWrap::appendDialogsSlice(Data::DialogsInfo &&info) {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->splitIndexPlusOne <= _splits.size());

	appendChatsSlice(
		*_dialogsProcess,
		_dialogsProcess->info.chats,
		std::move(info.chats),
		_dialogsProcess->splitIndexPlusOne - 1);
}

void ApiWrap::requestLeftChannelsIfNeeded() {
	if (_settings->types & Settings::Type::GroupsChannelsMask) {
		requestLeftChannelsList([=](int count) {
			Expects(_dialogsProcess != nullptr);

			return _dialogsProcess->progress(
				_dialogsProcess->processedCount + count);
		}, [=](Data::DialogsInfo &&result) {
			Expects(_dialogsProcess != nullptr);

			_dialogsProcess->info.left = std::move(result.left);
			finishDialogsList();
		});
	} else {
		finishDialogsList();
	}
}

void ApiWrap::finishDialogsList() {
	Expects(_dialogsProcess != nullptr);

	const auto process = base::take(_dialogsProcess);
	Data::FinalizeDialogsInfo(process->info, *_settings);
	process->done(std::move(process->info));
}

void ApiWrap::requestLeftChannelsSliceGeneric(FnMut<void()> done) {
	Expects(_leftChannelsProcess != nullptr);

	mainRequest(MTPchannels_GetLeftChannels(
		MTP_int(_leftChannelsProcess->offset)
	)).done([=, done = std::move(done)](
			const MTPmessages_Chats &result) mutable {
		Expects(_leftChannelsProcess != nullptr);

		appendLeftChannelsSlice(Data::ParseLeftChannelsInfo(result));

		const auto process = _leftChannelsProcess.get();
		process->offset += result.match(
		[](const auto &data) {
			return int(data.vchats().v.size());
		});

		process->fullCount = result.match(
		[](const MTPDmessages_chats &data) {
			return int(data.vchats().v.size());
		}, [](const MTPDmessages_chatsSlice &data) {
			return data.vcount().v;
		});

		process->finished = result.match(
		[](const MTPDmessages_chats &data) {
			return true;
		}, [](const MTPDmessages_chatsSlice &data) {
			return data.vchats().v.isEmpty();
		});

		if (process->progress) {
			if (!process->progress(process->info.left.size())) {
				return;
			}
		}

		done();
	}).send();
}

void ApiWrap::appendLeftChannelsSlice(Data::DialogsInfo &&info) {
	Expects(_leftChannelsProcess != nullptr);
	Expects(!_splits.empty());

	appendChatsSlice(
		*_leftChannelsProcess,
		_leftChannelsProcess->info.left,
		std::move(info.left),
		_splits.size() - 1);
}

void ApiWrap::appendChatsSlice(
		ChatsProcess &process,
		std::vector<Data::DialogInfo> &to,
		std::vector<Data::DialogInfo> &&from,
		int splitIndex) {
	Expects(_settings != nullptr);

	const auto types = _settings->types;
	const auto goodByTypes = [&](const Data::DialogInfo &info) {
		return ((types & SettingsFromDialogsType(info.type)) != 0);
	};
	auto filtered = ranges::view::all(
		from
	) | ranges::view::filter([&](const Data::DialogInfo &info) {
		if (goodByTypes(info)) {
			return true;
		} else if (info.migratedToChannelId
			&& (((types & Settings::Type::PublicGroups) != 0)
				|| ((types & Settings::Type::PrivateGroups) != 0))) {
			return true;
		}
		return false;
	});
	to.reserve(to.size() + from.size());
	for (auto &info : filtered) {
		const auto nextIndex = to.size();
		if (info.migratedToChannelId) {
			const auto toPeerId = Data::ChatPeerId(info.migratedToChannelId);
			const auto i = process.indexByPeer.find(toPeerId);
			if (i != process.indexByPeer.end()
				&& Data::AddMigrateFromSlice(
					to[i->second],
					info,
					splitIndex,
					int(_splits.size()))) {
				continue;
			} else if (!goodByTypes(info)) {
				continue;
			}
		}
		const auto [i, ok] = process.indexByPeer.emplace(
			info.peerId,
			nextIndex);
		if (ok) {
			to.push_back(std::move(info));
		}
		to[i->second].splits.push_back(splitIndex);
		to[i->second].messagesCountPerSplit.push_back(0);
	}
}

void ApiWrap::requestMessagesSlice() {
	Expects(_chatProcess != nullptr);

	const auto count = _chatProcess->info.messagesCountPerSplit[
		_chatProcess->localSplitIndex];
	if (!count) {
		loadMessagesFiles({});
		return;
	}
	requestChatMessages(
		_chatProcess->info.splits[_chatProcess->localSplitIndex],
		_chatProcess->largestIdPlusOne,
		-kMessagesSliceLimit,
		kMessagesSliceLimit,
		[=](const MTPmessages_Messages &result) {
		Expects(_chatProcess != nullptr);

		result.match([&](const MTPDmessages_messagesNotModified &data) {
			error("Unexpected messagesNotModified received.");
		}, [&](const auto &data) {
			if constexpr (MTPDmessages_messages::Is<decltype(data)>()) {
				_chatProcess->lastSlice = true;
			}
			loadMessagesFiles(Data::ParseMessagesSlice(
				_chatProcess->context,
				data.vmessages(),
				data.vusers(),
				data.vchats(),
				_chatProcess->info.relativePath));
		});
	});
}

void ApiWrap::requestChatMessages(
		int splitIndex,
		int offsetId,
		int addOffset,
		int limit,
		FnMut<void(MTPmessages_Messages&&)> done) {
	Expects(_chatProcess != nullptr);

	_chatProcess->requestDone = std::move(done);
	const auto doneHandler = [=](MTPmessages_Messages &&result) {
		Expects(_chatProcess != nullptr);

		base::take(_chatProcess->requestDone)(std::move(result));
	};
	const auto splitsCount = int(_splits.size());
	const auto realPeerInput = (splitIndex >= 0)
		? _chatProcess->info.input
		: _chatProcess->info.migratedFromInput;
	const auto realSplitIndex = (splitIndex >= 0)
		? splitIndex
		: (splitsCount + splitIndex);
	if (_chatProcess->info.onlyMyMessages) {
		splitRequest(realSplitIndex, MTPmessages_Search(
			MTP_flags(MTPmessages_Search::Flag::f_from_id),
			realPeerInput,
			MTP_string(), // query
			MTP_inputPeerSelf(),
			MTPint(), // top_msg_id
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(offsetId),
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_int(0) // hash
		)).done(doneHandler).send();
	} else {
		splitRequest(realSplitIndex, MTPmessages_GetHistory(
			realPeerInput,
			MTP_int(offsetId),
			MTP_int(0), // offset_date
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_int(0)  // hash
		)).fail([=](const RPCError &error) {
			Expects(_chatProcess != nullptr);

			if (error.type() == qstr("CHANNEL_PRIVATE")) {
				if (realPeerInput.type() == mtpc_inputPeerChannel
					&& !_chatProcess->info.onlyMyMessages) {

					// Perhaps we just left / were kicked from channel.
					// Just switch to only my messages.
					_chatProcess->info.onlyMyMessages = true;
					requestChatMessages(
						splitIndex,
						offsetId,
						addOffset,
						limit,
						base::take(_chatProcess->requestDone));
					return true;
				}
			}
			return false;
		}).done(doneHandler).send();
	}
}

void ApiWrap::loadMessagesFiles(Data::MessagesSlice &&slice) {
	Expects(_chatProcess != nullptr);
	Expects(!_chatProcess->slice.has_value());

	if (slice.list.empty()) {
		_chatProcess->lastSlice = true;
	}
	_chatProcess->slice = std::move(slice);
	_chatProcess->fileIndex = 0;

	loadNextMessageFile();
}

Data::Message *ApiWrap::currentFileMessage() const {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());

	return &_chatProcess->slice->list[_chatProcess->fileIndex];
}

Data::FileOrigin ApiWrap::currentFileMessageOrigin() const {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());

	const auto splitIndex = _chatProcess->info.splits[
		_chatProcess->localSplitIndex];
	auto result = Data::FileOrigin();
	result.messageId = currentFileMessage()->id;
	result.split = (splitIndex >= 0)
		? splitIndex
		: (int(_splits.size()) + splitIndex);
	result.peer = (splitIndex >= 0)
		? _chatProcess->info.input
		: _chatProcess->info.migratedFromInput;
	return result;
}

void ApiWrap::loadNextMessageFile() {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());

	for (auto &list = _chatProcess->slice->list
		; _chatProcess->fileIndex < list.size()
		; ++_chatProcess->fileIndex) {
		const auto &message = list[_chatProcess->fileIndex];
		if (Data::SkipMessageByDate(message, *_settings)) {
			continue;
		}
		const auto fileProgress = [=](FileProgress value) {
			return loadMessageFileProgress(value);
		};
		const auto ready = processFileLoad(
			list[_chatProcess->fileIndex].file(),
			currentFileMessageOrigin(),
			fileProgress,
			[=](const QString &path) { loadMessageFileDone(path); },
			currentFileMessage());
		if (!ready) {
			return;
		}
		const auto thumbProgress = [=](FileProgress value) {
			return loadMessageThumbProgress(value);
		};
		const auto thumbReady = processFileLoad(
			list[_chatProcess->fileIndex].thumb().file,
			currentFileMessageOrigin(),
			thumbProgress,
			[=](const QString &path) { loadMessageThumbDone(path); },
			currentFileMessage());
		if (!thumbReady) {
			return;
		}
	}
	finishMessagesSlice();
}

void ApiWrap::finishMessagesSlice() {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());

	auto slice = *base::take(_chatProcess->slice);
	if (!slice.list.empty()) {
		_chatProcess->largestIdPlusOne = slice.list.back().id + 1;
		const auto splitIndex = _chatProcess->info.splits[
			_chatProcess->localSplitIndex];
		if (splitIndex < 0) {
			slice = AdjustMigrateMessageIds(std::move(slice));
		}
		if (!_chatProcess->handleSlice(std::move(slice))) {
			return;
		}
	}
	if (_chatProcess->lastSlice
		&& (++_chatProcess->localSplitIndex
			< _chatProcess->info.splits.size())) {
		_chatProcess->lastSlice = false;
		_chatProcess->largestIdPlusOne = 1;
	}
	if (!_chatProcess->lastSlice) {
		requestMessagesSlice();
	} else {
		finishMessages();
	}
}

bool ApiWrap::loadMessageFileProgress(FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects((_chatProcess->fileIndex >= 0)
		&& (_chatProcess->fileIndex < _chatProcess->slice->list.size()));

	return _chatProcess->fileProgress(DownloadProgress{
		_fileProcess->relativePath,
		_chatProcess->fileIndex,
		progress.ready,
		progress.total });
}

void ApiWrap::loadMessageFileDone(const QString &relativePath) {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects((_chatProcess->fileIndex >= 0)
		&& (_chatProcess->fileIndex < _chatProcess->slice->list.size()));

	const auto index = _chatProcess->fileIndex;
	auto &file = _chatProcess->slice->list[index].file();
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextMessageFile();
}

bool ApiWrap::loadMessageThumbProgress(FileProgress progress) {
	return loadMessageFileProgress(progress);
}

void ApiWrap::loadMessageThumbDone(const QString &relativePath) {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects((_chatProcess->fileIndex >= 0)
		&& (_chatProcess->fileIndex < _chatProcess->slice->list.size()));

	const auto index = _chatProcess->fileIndex;
	auto &file = _chatProcess->slice->list[index].thumb().file;
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextMessageFile();
}

void ApiWrap::finishMessages() {
	Expects(_chatProcess != nullptr);
	Expects(!_chatProcess->slice.has_value());

	const auto process = base::take(_chatProcess);
	process->done();
}

bool ApiWrap::processFileLoad(
		Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done,
		Data::Message *message) {
	using SkipReason = Data::File::SkipReason;

	if (!file.relativePath.isEmpty()
		|| file.skipReason != SkipReason::None) {
		return true;
	} else if (!file.location && file.content.isEmpty()) {
		file.skipReason = SkipReason::Unavailable;
		return true;
	} else if (writePreloadedFile(file, origin)) {
		return !file.relativePath.isEmpty();
	}

	using Type = MediaSettings::Type;
	const auto type = message ? v::match(message->media.content, [&](
			const Data::Document &data) {
		if (data.isSticker) {
			return Type::Sticker;
		} else if (data.isVideoMessage) {
			return Type::VideoMessage;
		} else if (data.isVoiceMessage) {
			return Type::VoiceMessage;
		} else if (data.isAnimated) {
			return Type::GIF;
		} else if (data.isVideoFile) {
			return Type::Video;
		} else {
			return Type::File;
		}
	}, [](const auto &data) {
		return Type::Photo;
	}) : Type(0);

	const auto limit = _settings->media.sizeLimit;
	if (message && Data::SkipMessageByDate(*message, *_settings)) {
		file.skipReason = SkipReason::DateLimits;
		return true;
	} else if ((_settings->media.types & type) != type) {
		file.skipReason = SkipReason::FileType;
		return true;
	} else if ((message ? message->file().size : file.size) >= limit) {
		// Don't load thumbs for large files that we skip.
		file.skipReason = SkipReason::FileSize;
		return true;
	}
	loadFile(file, origin, std::move(progress), std::move(done));
	return false;
}

bool ApiWrap::writePreloadedFile(
		Data::File &file,
		const Data::FileOrigin &origin) {
	Expects(_settings != nullptr);

	using namespace Output;

	if (const auto path = _fileCache->find(file.location)) {
		file.relativePath = *path;
		return true;
	} else if (!file.content.isEmpty()) {
		const auto process = prepareFileProcess(file, origin);
		if (const auto result = process->file.writeBlock(file.content)) {
			file.relativePath = process->relativePath;
			_fileCache->save(file.location, file.relativePath);
		} else {
			ioError(result);
		}
		return true;
	}
	return false;
}

void ApiWrap::loadFile(
		const Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done) {
	Expects(_fileProcess == nullptr);
	Expects(file.location.dcId != 0
		|| file.location.data.type() == mtpc_inputTakeoutFileLocation);

	_fileProcess = prepareFileProcess(file, origin);
	_fileProcess->progress = std::move(progress);
	_fileProcess->done = std::move(done);

	if (_fileProcess->progress) {
		const auto progress = FileProgress{
			_fileProcess->file.size(),
			_fileProcess->size
		};
		if (!_fileProcess->progress(progress)) {
			return;
		}
	}

	loadFilePart();
}

auto ApiWrap::prepareFileProcess(
	const Data::File &file,
	const Data::FileOrigin &origin) const
-> std::unique_ptr<FileProcess> {
	Expects(_settings != nullptr);

	const auto relativePath = Output::File::PrepareRelativePath(
		_settings->path,
		file.suggestedPath);
	auto result = std::make_unique<FileProcess>(
		_settings->path + relativePath,
		_stats);
	result->relativePath = relativePath;
	result->location = file.location;
	result->size = file.size;
	result->origin = origin;
	return result;
}

void ApiWrap::loadFilePart() {
	if (!_fileProcess
		|| _fileProcess->requests.size() >= kFileRequestsCount
		|| (_fileProcess->size > 0
			&& _fileProcess->offset >= _fileProcess->size)) {
		return;
	}

	const auto offset = _fileProcess->offset;
	_fileProcess->requests.push_back({ offset });
	fileRequest(
		_fileProcess->location,
		_fileProcess->offset
	).done([=](const MTPupload_File &result) {
		filePartDone(offset, result);
	}).send();
	_fileProcess->offset += kFileChunkSize;

	if (_fileProcess->size > 0
		&& _fileProcess->requests.size() < kFileRequestsCount) {
		//const auto runner = _runner;
		//crl::on_main([=] {
		//	QTimer::singleShot(kFileNextRequestDelay, [=] {
		//		runner([=] {
		//			loadFilePart();
		//		});
		//	});
		//});
	}
}

void ApiWrap::filePartDone(int offset, const MTPupload_File &result) {
	Expects(_fileProcess != nullptr);
	Expects(!_fileProcess->requests.empty());

	if (result.type() == mtpc_upload_fileCdnRedirect) {
		error("Cdn redirect is not supported.");
		return;
	}
	const auto &data = result.c_upload_file();
	if (data.vbytes().v.isEmpty()) {
		if (_fileProcess->size > 0) {
			error("Empty bytes received in file part.");
			return;
		}
		const auto result = _fileProcess->file.writeBlock({});
		if (!result) {
			ioError(result);
			return;
		}
	} else {
		using Request = FileProcess::Request;
		auto &requests = _fileProcess->requests;
		const auto i = ranges::find(
			requests,
			offset,
			[](const Request &request) { return request.offset; });
		Assert(i != end(requests));

		i->bytes = data.vbytes().v;

		auto &file = _fileProcess->file;
		while (!requests.empty() && !requests.front().bytes.isEmpty()) {
			const auto &bytes = requests.front().bytes;
			if (const auto result = file.writeBlock(bytes); !result) {
				ioError(result);
				return;
			}
			requests.pop_front();
		}

		if (_fileProcess->progress) {
			_fileProcess->progress(FileProgress{
				file.size(),
				_fileProcess->size });
		}

		if (!requests.empty()
			|| !_fileProcess->size
			|| _fileProcess->size > _fileProcess->offset) {
			loadFilePart();
			return;
		}
	}

	auto process = base::take(_fileProcess);
	const auto relativePath = process->relativePath;
	_fileCache->save(process->location, relativePath);
	process->done(process->relativePath);
}

void ApiWrap::filePartRefreshReference(int offset) {
	Expects(_fileProcess != nullptr);

	const auto &origin = _fileProcess->origin;
	if (!origin.messageId) {
		error("FILE_REFERENCE error for non-message file.");
		return;
	}
	if (origin.peer.type() == mtpc_inputPeerChannel
		|| origin.peer.type() == mtpc_inputPeerChannelFromMessage) {
		const auto channel = (origin.peer.type() == mtpc_inputPeerChannel)
			? MTP_inputChannel(
				origin.peer.c_inputPeerChannel().vchannel_id(),
				origin.peer.c_inputPeerChannel().vaccess_hash())
			: MTP_inputChannelFromMessage(
				origin.peer.c_inputPeerChannelFromMessage().vpeer(),
				origin.peer.c_inputPeerChannelFromMessage().vmsg_id(),
				origin.peer.c_inputPeerChannelFromMessage().vchannel_id());
		mainRequest(MTPchannels_GetMessages(
			channel,
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(origin.messageId)))
		)).fail([=](const RPCError &error) {
			filePartUnavailable();
			return true;
		}).done([=](const MTPmessages_Messages &result) {
			filePartExtractReference(offset, result);
		}).send();
	} else {
		splitRequest(origin.split, MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(origin.messageId)))
		)).fail([=](const RPCError &error) {
			filePartUnavailable();
			return true;
		}).done([=](const MTPmessages_Messages &result) {
			filePartExtractReference(offset, result);
		}).send();
	}
}

void ApiWrap::filePartExtractReference(
		int offset,
		const MTPmessages_Messages &result) {
	Expects(_fileProcess != nullptr);

	result.match([&](const MTPDmessages_messagesNotModified &data) {
		error("Unexpected messagesNotModified received.");
	}, [&](const auto &data) {
		Expects(_selfId.has_value());

		auto context = Data::ParseMediaContext();
		context.selfPeerId = Data::UserPeerId(*_selfId);
		const auto messages = Data::ParseMessagesSlice(
			context,
			data.vmessages(),
			data.vusers(),
			data.vchats(),
			_chatProcess->info.relativePath);
		for (const auto &message : messages.list) {
			if (message.id == _fileProcess->origin.messageId) {
				const auto refresh1 = Data::RefreshFileReference(
					_fileProcess->location,
					message.file().location);
				const auto refresh2 = Data::RefreshFileReference(
					_fileProcess->location,
					message.thumb().file.location);
				if (refresh1 || refresh2) {
					fileRequest(
						_fileProcess->location,
						offset
					).done([=](const MTPupload_File &result) {
						filePartDone(offset, result);
					}).send();
					return;
				}
			}
		}
		filePartUnavailable();
	});
}

void ApiWrap::filePartUnavailable() {
	Expects(_fileProcess != nullptr);
	Expects(!_fileProcess->requests.empty());

	LOG(("Export Error: File unavailable."));

	base::take(_fileProcess)->done(QString());
}

void ApiWrap::error(RPCError &&error) {
	_errors.fire(std::move(error));
}

void ApiWrap::error(const QString &text) {
	error(MTP_rpc_error(MTP_int(0), MTP_string("API_ERROR: " + text)));
}

void ApiWrap::ioError(const Output::Result &result) {
	_ioErrors.fire_copy(result);
}

ApiWrap::~ApiWrap() = default;

} // namespace Export
