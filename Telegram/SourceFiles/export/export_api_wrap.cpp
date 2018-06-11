/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_api_wrap.h"

#include "export/data/export_data_types.h"
#include "export/output/export_output_file.h"
#include "mtproto/rpc_sender.h"

#include <deque>

namespace Export {
namespace {

constexpr auto kUserpicsSliceLimit = 2;
constexpr auto kFileChunkSize = 128 * 1024;
constexpr auto kFileRequestsCount = 2;
constexpr auto kFileNextRequestDelay = TimeMs(20);

} // namespace

struct ApiWrap::UserpicsProcess {
	FnMut<void(Data::UserpicsInfo&&)> start;
	Fn<void(Data::UserpicsSlice&&)> handleSlice;
	FnMut<void()> finish;

	base::optional<Data::UserpicsSlice> slice;
	bool lastSlice = false;
	int loading = -1;

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

ApiWrap::FileProcess::FileProcess(const QString &path) : file(path) {
}

template <typename Request>
auto ApiWrap::mainRequest(Request &&request) {
	return std::move(_mtp.request(
		std::move(request)
	).fail([=](RPCError &&result) {
		error(std::move(result));
	}).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)));
}

auto ApiWrap::fileRequest(const Data::FileLocation &location, int offset) {
	Expects(location.dcId != 0);

	return std::move(_mtp.request(MTPupload_GetFile(
		location.data,
		MTP_int(offset),
		MTP_int(kFileChunkSize)
	)).fail([=](RPCError &&result) {
		error(std::move(result));
	}).toDC(MTP::ShiftDcId(location.dcId, MTP::kExportDcShift)));
}

ApiWrap::ApiWrap(Fn<void(FnMut<void()>)> runner)
: _mtp(std::move(runner)) {
}

void ApiWrap::setFilesBaseFolder(const QString &folder) {
	Expects(folder.endsWith('/'));

	_filesFolder = folder;
}

rpl::producer<RPCError> ApiWrap::errors() const {
	return _errors.events();
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
			switch (result.type()) {
			case mtpc_photos_photos: {
				const auto &data = result.c_photos_photos();
				info.count = data.vphotos.v.size();
			} break;

			case mtpc_photos_photosSlice: {
				const auto &data = result.c_photos_photosSlice();
				info.count = data.vcount.v;
			} break;

			default: Unexpected("Photos type in Controller::exportUserpics.");
			}
			return info;
		}());

		handleUserpicsSlice(result);
	}).send();
}

void ApiWrap::handleUserpicsSlice(const MTPphotos_Photos &result) {
	Expects(_userpicsProcess != nullptr);

	switch (result.type()) {
	case mtpc_photos_photos: {
		const auto &data = result.c_photos_photos();
		_userpicsProcess->lastSlice = true;
		loadUserpicsFiles(Data::ParseUserpicsSlice(data.vphotos));
	} break;

	case mtpc_photos_photosSlice: {
		const auto &data = result.c_photos_photosSlice();
		loadUserpicsFiles(Data::ParseUserpicsSlice(data.vphotos));
	} break;

	default: Unexpected("Photos type in Controller::exportUserpicsSlice.");
	}
}

void ApiWrap::loadUserpicsFiles(Data::UserpicsSlice &&slice) {
	Expects(_userpicsProcess != nullptr);
	Expects(!_userpicsProcess->slice.has_value());

	if (slice.list.empty()) {
		_userpicsProcess->lastSlice = true;
	}
	_userpicsProcess->slice = std::move(slice);
	_userpicsProcess->loading = -1;
	loadNextUserpic();
}

void ApiWrap::loadNextUserpic() {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());

	const auto &list = _userpicsProcess->slice->list;
	++_userpicsProcess->loading;
	if (_userpicsProcess->loading < list.size()) {
		loadFile(
			list[_userpicsProcess->loading].image,
			[=](const QString &path) { loadUserpicDone(path); });
		return;
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
	Expects((_userpicsProcess->loading >= 0)
		&& (_userpicsProcess->loading
			< _userpicsProcess->slice->list.size()));

	const auto index = _userpicsProcess->loading;
	_userpicsProcess->slice->list[index].image.relativePath = relativePath;
	loadNextUserpic();
}

void ApiWrap::finishUserpics() {
	Expects(_userpicsProcess != nullptr);

	base::take(_userpicsProcess)->finish();
}

void ApiWrap::requestContacts(FnMut<void(Data::ContactsList&&)> done) {
	const auto hash = 0;
	mainRequest(MTPcontacts_GetContacts(
		MTP_int(hash)
	)).done([=, done = std::move(done)](
			const MTPcontacts_Contacts &result) mutable {
		if (result.type() == mtpc_contacts_contacts) {
			done(Data::ParseContactsList(result));
		} else {
			error("Bad contacts type.");
		}
	}).send();
}

void ApiWrap::loadFile(const Data::File &file, FnMut<void(QString)> done) {
	Expects(_fileProcess == nullptr);

	if (!file.relativePath.isEmpty()) {
		done(file.relativePath);
	}
	using namespace Output;
	const auto relativePath = File::PrepareRelativePath(
		_filesFolder,
		file.suggestedPath);
	_fileProcess = std::make_unique<FileProcess>(
		_filesFolder + relativePath);
	_fileProcess->relativePath = relativePath;
	_fileProcess->location = file.location;
	_fileProcess->done = std::move(done);

	if (!file.content.isEmpty()) {
		auto &output = _fileProcess->file;
		if (output.writeBlock(file.content) == File::Result::Success) {
			_fileProcess->done(relativePath);
		} else {
			error(QString("Could not open '%1'.").arg(relativePath));
		}
	} else if (!file.location.dcId) {
		_fileProcess->done(QString());
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
