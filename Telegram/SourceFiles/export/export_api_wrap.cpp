/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_api_wrap.h"

#include "export/export_settings.h"
#include "export/data/export_data_types.h"
#include "export/output/export_output_file.h"
#include "mtproto/rpc_sender.h"
#include "base/bytes.h"

#include <deque>

namespace Export {
namespace {

constexpr auto kUserpicsSliceLimit = 100;
constexpr auto kFileChunkSize = 128 * 1024;
constexpr auto kFileRequestsCount = 2;
constexpr auto kFileNextRequestDelay = TimeMs(20);
constexpr auto kChatsSliceLimit = 100;
constexpr auto kMessagesSliceLimit = 100;
constexpr auto kFileMaxSize = 1500 * 1024 * 1024;

bool WillLoadFile(const Data::File &file) {
	return file.relativePath.isEmpty()
		&& (!file.content.isEmpty() || file.location.dcId != 0);
}

} // namespace

struct ApiWrap::UserpicsProcess {
	FnMut<void(Data::UserpicsInfo&&)> start;
	Fn<void(Data::UserpicsSlice&&)> handleSlice;
	FnMut<void()> finish;

	base::optional<Data::UserpicsSlice> slice;
	bool lastSlice = false;
	int fileIndex = -1;

};

struct ApiWrap::FileProcess {
	FileProcess(const QString &path);

	Output::File file;
	QString relativePath;

	FnMut<void(const QString &relativePath)> done;

	Data::FileLocation location;
	int offset = 0;
	int size = 0;

	struct Request {
		int offset = 0;
		QByteArray bytes;
	};
	std::deque<Request> requests;

};

struct ApiWrap::DialogsProcess {
	Data::DialogsInfo info;

	FnMut<void(const Data::DialogsInfo&)> start;
	Fn<void(const Data::DialogInfo&)> startOne;
	Fn<void(Data::MessagesSlice&&)> sliceOne;
	Fn<void()> finishOne;
	FnMut<void()> finish;

	Data::TimeId offsetDate = 0;
	int32 offsetId = 0;
	MTPInputPeer offsetPeer = MTP_inputPeerEmpty();

	struct Single;
	std::unique_ptr<Single> single;
	int singleIndex = -1;

};

struct ApiWrap::DialogsProcess::Single {
	Single(const Data::DialogInfo &info);

	Data::DialogInfo info;
	int32 offsetId = 1;

	base::optional<Data::MessagesSlice> slice;
	bool lastSlice = false;
	int fileIndex = -1;

};

ApiWrap::FileProcess::FileProcess(const QString &path) : file(path) {
}

ApiWrap::DialogsProcess::Single::Single(const Data::DialogInfo &info)
: info(info) {
}

template <typename Request>
auto ApiWrap::mainRequest(Request &&request) {
	Expects(_takeoutId.has_value());

	return std::move(_mtp.request(MTPInvokeWithTakeout<Request>(
		MTP_long(*_takeoutId),
		request
	)).fail([=](RPCError &&result) {
		error(std::move(result));
	}).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)));
}

auto ApiWrap::fileRequest(const Data::FileLocation &location, int offset) {
	Expects(location.dcId != 0);
	Expects(_takeoutId.has_value());

	return std::move(_mtp.request(MTPInvokeWithTakeout<MTPupload_GetFile>(
		MTP_long(*_takeoutId),
		MTPupload_GetFile(
			location.data,
			MTP_int(offset),
			MTP_int(kFileChunkSize))
	)).fail([=](RPCError &&result) {
		error(std::move(result));
	}).toDC(MTP::ShiftDcId(location.dcId, MTP::kExportMediaDcShift)));
}

ApiWrap::ApiWrap(Fn<void(FnMut<void()>)> runner)
: _mtp(std::move(runner)) {
}

rpl::producer<RPCError> ApiWrap::errors() const {
	return _errors.events();
}

void ApiWrap::startExport(
		const Settings &settings,
		FnMut<void()> done) {
	Expects(_settings == nullptr);

	_settings = std::make_unique<Settings>(settings);
	startMainSession(std::move(done));
}

void ApiWrap::startMainSession(FnMut<void()> done) {
	const auto sizeLimit = _settings->media.sizeLimit;
	const auto hasFiles = (_settings->media.types != 0) && (sizeLimit > 0);

	using Type = Settings::Type;
	using Flag = MTPaccount_InitTakeoutSession::Flag;
	const auto flags = Flag(0)
		| (_settings->types & Type::Contacts ? Flag::f_contacts : Flag(0))
		| (hasFiles ? Flag::f_files : Flag(0))
		| (sizeLimit < kFileMaxSize ? Flag::f_file_max_size : Flag(0))
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

	_mtp.request(MTPaccount_InitTakeoutSession(
		MTP_flags(flags),
		MTP_int(sizeLimit)
	)).done([=, done = std::move(done)](
			const MTPaccount_Takeout &result) mutable {
		_takeoutId = result.match([](const MTPDaccount_takeout &data) {
			return data.vid.v;
		});
		done();
	}).fail([=](RPCError &&result) {
		error(std::move(result));
	}).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
}

void ApiWrap::requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done) {
	mainRequest(MTPusers_GetFullUser(
		_user
	)).done([=, done = std::move(done)](const MTPUserFull &result) mutable {
		Expects(result.type() == mtpc_userFull);

		const auto &full = result.c_userFull();
		if (full.vuser.type() == mtpc_user) {
			done(Data::ParsePersonalInfo(result));
		} else {
			error("Bad user type.");
		}
	}).send();
}

void ApiWrap::requestUserpics(
		FnMut<void(Data::UserpicsInfo&&)> start,
		Fn<void(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish) {
	Expects(_userpicsProcess == nullptr);

	_userpicsProcess = std::make_unique<UserpicsProcess>();
	_userpicsProcess->start = std::move(start);
	_userpicsProcess->handleSlice = std::move(slice);
	_userpicsProcess->finish = std::move(finish);

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0),
		MTP_long(0),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) mutable {
		Expects(_userpicsProcess != nullptr);

		_userpicsProcess->start([&] {
			auto info = Data::UserpicsInfo();
			result.match([&](const MTPDphotos_photos &data) {
				info.count = data.vphotos.v.size();
			}, [&](const MTPDphotos_photosSlice &data) {
				info.count = data.vcount.v;
			});
			return info;
		}());

		handleUserpicsSlice(result);
	}).send();
}

void ApiWrap::handleUserpicsSlice(const MTPphotos_Photos &result) {
	Expects(_userpicsProcess != nullptr);

	result.match([&](const auto &data) {
		if constexpr (MTPDphotos_photos::Is<decltype(data)>()) {
			_userpicsProcess->lastSlice = true;
		}
		loadUserpicsFiles(Data::ParseUserpicsSlice(data.vphotos));
	});
}

void ApiWrap::loadUserpicsFiles(Data::UserpicsSlice &&slice) {
	Expects(_userpicsProcess != nullptr);
	Expects(!_userpicsProcess->slice.has_value());

	if (slice.list.empty()) {
		_userpicsProcess->lastSlice = true;
	}
	_userpicsProcess->slice = std::move(slice);
	_userpicsProcess->fileIndex = -1;
	loadNextUserpic();
}

void ApiWrap::loadNextUserpic() {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());

	const auto &list = _userpicsProcess->slice->list;
	while (true) {
		const auto index = ++_userpicsProcess->fileIndex;
		if (index >= list.size()) {
			break;
		}
		const auto &file = list[index].image.file;
		if (WillLoadFile(file)) {
			loadFile(
				file,
				[=](const QString &path) { loadUserpicDone(path); });
			return;
		}
	}
	const auto lastUserpicId = list.empty()
		? base::none
		: base::make_optional(list.back().id);

	_userpicsProcess->handleSlice(*base::take(_userpicsProcess->slice));

	if (_userpicsProcess->lastSlice) {
		finishUserpics();
		return;
	}

	Assert(lastUserpicId.has_value());
	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0),
		MTP_long(*lastUserpicId),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) {
		handleUserpicsSlice(result);
	}).send();
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
	loadNextUserpic();
}

void ApiWrap::finishUserpics() {
	Expects(_userpicsProcess != nullptr);

	base::take(_userpicsProcess)->finish();
}

void ApiWrap::requestContacts(FnMut<void(Data::ContactsList&&)> done) {
	mainRequest(MTPcontacts_GetSaved(
	)).done([=, done = std::move(done)](
			const MTPVector<MTPSavedContact> &result) mutable {
		done(Data::ParseContactsList(result));
	}).send();
}

void ApiWrap::requestSessions(FnMut<void(Data::SessionsList&&)> done) {
	mainRequest(MTPaccount_GetAuthorizations(
	)).done([=, done = std::move(done)](
			const MTPaccount_Authorizations &result) mutable {
		done(Data::ParseSessionsList(result));
	}).send();
}

void ApiWrap::requestDialogs(
		FnMut<void(const Data::DialogsInfo&)> start,
		Fn<void(const Data::DialogInfo&)> startOne,
		Fn<void(Data::MessagesSlice&&)> sliceOne,
		Fn<void()> finishOne,
		FnMut<void()> finish) {
	Expects(_dialogsProcess == nullptr);

	_dialogsProcess = std::make_unique<DialogsProcess>();
	_dialogsProcess->start = std::move(start);
	_dialogsProcess->startOne = std::move(startOne);
	_dialogsProcess->sliceOne = std::move(sliceOne);
	_dialogsProcess->finishOne = std::move(finishOne);
	_dialogsProcess->finish = std::move(finish);

	requestDialogsSlice();
}

void ApiWrap::requestDialogsSlice() {
	Expects(_dialogsProcess != nullptr);

	mainRequest(MTPmessages_GetDialogs(
		MTP_flags(0),
		MTP_int(_dialogsProcess->offsetDate),
		MTP_int(_dialogsProcess->offsetId),
		_dialogsProcess->offsetPeer,
		MTP_int(kChatsSliceLimit)
	)).done([=](const MTPmessages_Dialogs &result) mutable {
		const auto finished = [&] {
			switch (result.type()) {
			case mtpc_messages_dialogs: return true;
			case mtpc_messages_dialogsSlice: {
				const auto &data = result.c_messages_dialogsSlice();
				return data.vdialogs.v.isEmpty();
			} break;
			default: Unexpected("Type in ApiWrap::requestChatsSlice.");
			}
		}();
		auto info = Data::ParseDialogsInfo(result);
		if (finished || info.list.empty()) {
			finishDialogsList();
		} else {
			const auto &last = info.list.back();
			_dialogsProcess->offsetId = last.topMessageId;
			_dialogsProcess->offsetDate = last.topMessageDate;
			_dialogsProcess->offsetPeer = last.input;

			appendDialogsSlice(std::move(info));

			requestDialogsSlice();
		}
	}).send();
}

void ApiWrap::appendDialogsSlice(Data::DialogsInfo &&info) {
	Expects(_dialogsProcess != nullptr);
	Expects(_settings != nullptr);

	const auto types = _settings->types | _settings->fullChats;
	auto filtered = ranges::view::all(
		info.list
	) | ranges::view::filter([&](const Data::DialogInfo &info) {
		const auto bit = [&] {
			using DialogType = Data::DialogInfo::Type;
			switch (info.type) {
			case DialogType::Personal:
				return Settings::Type::PersonalChats;
			case DialogType::Bot:
				return Settings::Type::BotChats;
			case DialogType::PrivateGroup:
				return Settings::Type::PrivateGroups;
			case DialogType::PublicGroup:
				return Settings::Type::PublicGroups;
			case DialogType::PrivateChannel:
				return Settings::Type::PrivateChannels;
			case DialogType::PublicChannel:
				return Settings::Type::PublicChannels;
			}
			return Settings::Type(0);
		}();
		return (types & bit) != 0;
	});
	auto &list = _dialogsProcess->info.list;
	list.reserve(list.size());
	for (auto &info : filtered) {
		list.push_back(std::move(info));
	}
}

void ApiWrap::finishDialogsList() {
	Expects(_dialogsProcess != nullptr);

	ranges::reverse(_dialogsProcess->info.list);
	fillDialogsPaths();

	_dialogsProcess->start(_dialogsProcess->info);
	requestNextDialog();
}

void ApiWrap::fillDialogsPaths() {
	Expects(_dialogsProcess != nullptr);

	auto &list = _dialogsProcess->info.list;
	const auto digits = Data::NumberToString(list.size() - 1).size();
	auto index = 0;
	for (auto &dialog : list) {
		const auto number = Data::NumberToString(++index, digits, '0');
		dialog.relativePath = "Chats/chat_" + number + '/';
	}
}

void ApiWrap::requestNextDialog() {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single == nullptr);

	const auto index = ++_dialogsProcess->singleIndex;
	if (index < 11) {// _dialogsProcess->info.list.size()) {
		const auto &one = _dialogsProcess->info.list[index];
		_dialogsProcess->single = std::make_unique<DialogsProcess::Single>(one);
		_dialogsProcess->startOne(one);
		requestMessagesSlice();
		return;
	}
	finishDialogs();
}

void ApiWrap::requestMessagesSlice() {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single != nullptr);

	const auto process = _dialogsProcess->single.get();
	mainRequest(MTPmessages_GetHistory(
		process->info.input,
		MTP_int(process->offsetId),
		MTP_int(0), // offset_date
		MTP_int(-kMessagesSliceLimit),
		MTP_int(kMessagesSliceLimit),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_int(0)  // hash
	)).done([=](const MTPmessages_Messages &result) mutable {
		Expects(_dialogsProcess != nullptr);
		Expects(_dialogsProcess->single != nullptr);

		const auto process = _dialogsProcess->single.get();
		result.match([&](const MTPDmessages_messagesNotModified &data) {
			error("Unexpected messagesNotModified received.");
		}, [&](const auto &data) {
			if constexpr (MTPDmessages_messages::Is<decltype(data)>()) {
				process->lastSlice = true;
			}
			loadMessagesFiles(Data::ParseMessagesSlice(
				data.vmessages,
				data.vusers,
				data.vchats,
				process->info.relativePath));
		});
	}).send();
}

void ApiWrap::loadMessagesFiles(Data::MessagesSlice &&slice) {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single != nullptr);
	Expects(!_dialogsProcess->single->slice.has_value());

	const auto process = _dialogsProcess->single.get();
	if (slice.list.empty()) {
		process->lastSlice = true;
	}
	process->slice = std::move(slice);
	process->fileIndex = -1;

	loadNextMessageFile();
}

void ApiWrap::loadNextMessageFile() {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single != nullptr);
	Expects(_dialogsProcess->single->slice.has_value());

	const auto process = _dialogsProcess->single.get();
	const auto &list = process->slice->list;
	while (true) {
		const auto index = ++process->fileIndex;
		if (index >= list.size()) {
			break;
		}
		const auto &file = list[index].file();
		if (WillLoadFile(file)) {
			loadFile(
				file,
				[=](const QString &path) { loadMessageFileDone(path); });
			return;
		}
	}

	if (!list.empty()) {
		process->offsetId = list.back().id + 1;
	}
	_dialogsProcess->sliceOne(*base::take(process->slice));

	if (process->lastSlice) {
		finishMessages();
	} else {
		requestMessagesSlice();
	}
}

void ApiWrap::loadMessageFileDone(const QString &relativePath) {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single != nullptr);
	Expects(_dialogsProcess->single->slice.has_value());
	Expects((_dialogsProcess->single->fileIndex >= 0)
		&& (_dialogsProcess->single->fileIndex
			< _dialogsProcess->single->slice->list.size()));

	const auto process = _dialogsProcess->single.get();
	const auto index = process->fileIndex;
	process->slice->list[index].file().relativePath = relativePath;
	loadNextMessageFile();
}

void ApiWrap::finishMessages() {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single != nullptr);
	Expects(!_dialogsProcess->single->slice.has_value());

	_dialogsProcess->single = nullptr;
	_dialogsProcess->finishOne();

	requestNextDialog();
}

void ApiWrap::finishDialogs() {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->single == nullptr);

	base::take(_dialogsProcess)->finish();
}

void ApiWrap::loadFile(const Data::File &file, FnMut<void(QString)> done) {
	Expects(_fileProcess == nullptr);
	Expects(_settings != nullptr);
	Expects(WillLoadFile(file));

	using namespace Output;
	const auto relativePath = File::PrepareRelativePath(
		_settings->path,
		file.suggestedPath);
	_fileProcess = std::make_unique<FileProcess>(
		_settings->path + relativePath);
	_fileProcess->relativePath = relativePath;
	_fileProcess->location = file.location;
	_fileProcess->size = file.size;
	_fileProcess->done = std::move(done);

	if (!file.content.isEmpty()) {
		auto &output = _fileProcess->file;
		if (output.writeBlock(file.content) == File::Result::Success) {
			_fileProcess->done(relativePath);
		} else {
			error(QString("Could not open '%1'.").arg(relativePath));
		}
	} else {
		loadFilePart();
	}
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
	if (data.vbytes.v.isEmpty()) {
		if (_fileProcess->size > 0) {
			error("Empty bytes received in file part.");
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

		i->bytes = data.vbytes.v;

		auto &file = _fileProcess->file;
		while (!requests.empty() && !requests.front().bytes.isEmpty()) {
			const auto &bytes = requests.front().bytes;
			if (file.writeBlock(bytes) != Output::File::Result::Success) {
				error(QString("Could not write bytes to '%1'."
				).arg(_fileProcess->relativePath));
				return;
			}
			requests.pop_front();
		}

		if (!requests.empty()
			|| !_fileProcess->size
			|| _fileProcess->size > _fileProcess->offset) {
			loadFilePart();
			return;
		}
	}
	auto process = base::take(_fileProcess);
	process->done(process->relativePath);
}

void ApiWrap::error(RPCError &&error) {
	_errors.fire(std::move(error));
}

void ApiWrap::error(const QString &text) {
	error(MTP_rpc_error(MTP_int(0), MTP_string("API_ERROR: " + text)));
}

ApiWrap::~ApiWrap() = default;

} // namespace Export
