/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot_media_preview_upload.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "media/media_video_encode.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"

namespace Api {
namespace {

class PrepareTask final : public Task {
public:
	PrepareTask(
		FileLoadTask::Args args,
		SendStoryMedia media,
		std::shared_ptr<std::atomic<bool>> cancel,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done);

	void process() override;
	void finish() override;

private:
	bool prepareVideo(const SendStoryVideo &video);

	FileLoadTask::Args _args;
	SendStoryMedia _media;
	std::shared_ptr<std::atomic<bool>> _cancel;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;
	std::shared_ptr<FilePrepareResult> _result;

};

PrepareTask::PrepareTask(
	FileLoadTask::Args args,
	SendStoryMedia media,
	std::shared_ptr<std::atomic<bool>> cancel,
	Fn<void(std::shared_ptr<FilePrepareResult>)> done)
: _args(std::move(args))
, _media(std::move(media))
, _cancel(std::move(cancel))
, _done(std::move(done)) {
}

void PrepareTask::process() {
	using Info = Ui::PreparedFileInformation;
	if (const auto photo = std::get_if<SendStoryPhoto>(&_media)) {
		if (photo->image.isNull()) {
			return;
		}
		_args.information = std::make_unique<Info>();
		_args.information->media = Info::Image{
			.data = std::move(photo->image),
		};
		_args.type = SendMediaType::Photo;
	} else if (const auto video = std::get_if<SendStoryVideo>(&_media)) {
		if (!prepareVideo(*video)) {
			return;
		}
		_args.type = SendMediaType::File;
	} else {
		return;
	}
	const auto expected = _args.type;
	auto task = FileLoadTask(std::move(_args));
	task.process();
	const auto &file = task.peekResult();
	if (!file || file->type != expected || file->filesize <= 0) {
		return;
	}
	if (expected == SendMediaType::File
		&& (file->document.type() != mtpc_document
			|| file->filemime != u"video/mp4"_q)) {
		return;
	}
	_result = file;
}

bool PrepareTask::prepareVideo(const SendStoryVideo &video) {
	using Info = Ui::PreparedFileInformation;
	if (video.job || video.source) {
		using namespace Media::Encode;
		auto job = video.job ? *video.job : Job{
			.source = *video.source,
		};
		const auto progress = [&](float64) {
			return !_cancel->load();
		};
		auto encoded = Run(std::move(job), progress);
		if (encoded.empty()) {
			return false;
		}
		_args.content = std::move(encoded.bytes);
	} else {
		_args.filepath = video.path;
	}
	_args.information = FileLoadTask::ReadMediaInformation(
		_args.filepath,
		_args.content,
		u"video/mp4"_q);
	return v::is<Info::Video>(_args.information->media);
}

void PrepareTask::finish() {
	_done(std::move(_result));
}

[[nodiscard]] MTPInputMedia UploadedInputMedia(
		const FilePrepareResult &file,
		const RemoteFileInfo &remote) {
	if (file.type == SendMediaType::Photo) {
		return MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			remote.file,
			MTP_vector<MTPInputDocument>(),
			MTP_int(0),
			MTPInputDocument());
	}
	using Flag = MTPDinputMediaUploadedDocument::Flag;
	const auto &document = file.document.c_document();
	const auto &attributes = document.vattributes();
	const auto isSilentVideo = [](const auto &attr) {
		return (attr.type() == mtpc_documentAttributeVideo)
			&& attr.c_documentAttributeVideo().is_nosound();
	};
	const auto silent = ranges::any_of(attributes.v, isSilentVideo);
	const auto flags = (remote.thumb ? Flag::f_thumb : Flag())
		| (silent ? Flag::f_nosound_video : Flag());
	return MTP_inputMediaUploadedDocument(
		MTP_flags(flags),
		remote.file,
		remote.thumb.value_or(MTPInputFile()),
		document.vmime_type(),
		attributes,
		MTP_vector<MTPInputDocument>(),
		MTPInputPhoto(),
		MTP_int(0),
		MTP_int(0));
}

} // namespace

BotMediaPreviewUpload::BotMediaPreviewUpload(not_null<UserData*> bot)
: _bot(bot) {
	auto &uploader = _bot->session().uploader();
	const auto ready = [=](const Storage::UploadedMedia &media) {
		if (_uploadId == media.fullId) {
			uploaded(media);
		}
	};
	const auto error = [=](FullMsgId id) {
		if (_uploadId == id) {
			failed();
		}
	};
	rpl::merge(
		uploader.photoReady(),
		uploader.documentReady()
	) | rpl::on_next(ready, _lifetime);
	rpl::merge(
		uploader.photoFailed(),
		uploader.documentFailed()
	) | rpl::on_next(error, _lifetime);
}

BotMediaPreviewUpload::~BotMediaPreviewUpload() {
	_lifetime.destroy();
	if (_cancel) {
		_cancel->store(true);
	}
	if (_uploadId) {
		_bot->session().uploader().cancel(_uploadId);
	}
}

void BotMediaPreviewUpload::start(
		SendStoryMedia media,
		Fn<void(MTPInputMedia)> done,
		Fn<void()> fail) {
	Expects(!_done);
	_done = std::move(done);
	_fail = std::move(fail);
	_cancel = std::make_shared<std::atomic<bool>>(false);
	if (!_queue) {
		_queue = std::make_unique<TaskQueue>();
	}
	auto args = FileLoadTask::Args{
		.session = &_bot->session(),
		.type = SendMediaType::Photo,
		.to = { _bot->id, SendOptions(), FullReplyTo(), MsgId() },
		.sendLargePhotos = true,
	};
	const auto prepared = [=](std::shared_ptr<FilePrepareResult> file) {
		this->prepared(std::move(file));
	};
	auto task = std::make_unique<PrepareTask>(
		std::move(args),
		std::move(media),
		_cancel,
		prepared);
	_queue->addTask(std::move(task));
}

void BotMediaPreviewUpload::prepared(
		std::shared_ptr<FilePrepareResult> file) {
	if (!file) {
		failed();
		return;
	}
	auto &session = _bot->session();
	_uploadId = FullMsgId(_bot->id, session.data().nextLocalMessageId());
	_file = std::move(file);
	session.uploader().upload(_uploadId, _file);
}

void BotMediaPreviewUpload::uploaded(const Storage::UploadedMedia &media) {
	const auto file = base::take(_file);
	_uploadId = {};
	_fail = nullptr;
	const auto done = base::take(_done);
	done(UploadedInputMedia(*file, media.info));
}

void BotMediaPreviewUpload::failed() {
	_file = nullptr;
	_uploadId = {};
	_done = nullptr;
	const auto fail = base::take(_fail);
	fail();
}

} // namespace Api
