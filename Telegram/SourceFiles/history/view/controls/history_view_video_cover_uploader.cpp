/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_video_cover_uploader.h"

#include "apiwrap.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class PrepareCoverTask final : public Task {
public:
	PrepareCoverTask(
		FileLoadTask::Args &&args,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done)
	: _task(std::move(args))
	, _done(std::move(done)) {
	}

	void process() override {
		_task.process({ .generateGoodThumbnail = false });
	}
	void finish() override {
		_done(_task.peekResult());
	}

private:
	FileLoadTask _task;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;

};

[[nodiscard]] QImage PrepareCoverPreview(QImage preview) {
	const auto side = st::historyReplyPreview;
	const auto w = std::max(preview.width(), 1);
	const auto h = std::max(preview.height(), 1);
	const auto thumbSize = (w > h)
		? QSize(w * side / h, side)
		: QSize(side, h * side / w);
	return Images::Prepare(
		std::move(preview),
		thumbSize * style::DevicePixelRatio(),
		{
			.options = Images::Option::TransparentBackground,
			.outer = { side, side },
		});
}

[[nodiscard]] DocumentData *ItemVideo(HistoryItem *item) {
	const auto media = item ? item->media() : nullptr;
	const auto document = media ? media->document() : nullptr;
	return (document && document->isVideoFile()) ? document : nullptr;
}

} // namespace

VideoCoverUploader::VideoCoverUploader(Fn<void()> updated)
: _updated(std::move(updated))
, _radial([=] { _updated(); }, st::historyEditCoverRadial) {
}

VideoCoverUploader::~VideoCoverUploader() {
	cancelRequests();
}

void VideoCoverUploader::choose(
		not_null<HistoryItem*> item,
		std::shared_ptr<ChatHelpers::Show> show) {
	const auto video = ItemVideo(item);
	if (!show || !video || video->dimensions.isEmpty()) {
		return;
	}
	_item = item;
	const auto parent = show->toastParent().get();
	const auto checkResult = [=](const Ui::PreparedList &list) {
		if (list.files.empty()) {
			return true;
		} else if (list.files.front().type != Ui::PreparedFile::Type::Photo) {
			show->showToast(tr::lng_choose_cover_bad(tr::now));
			return false;
		}
		return true;
	};
	const auto callback = crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (_item != item) {
			return;
		}
		const auto showError = [=](tr::phrase<> text) {
			show->showToast(text(tr::now));
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			checkResult,
			showError,
			st::sendMediaPreviewSize,
			show->session().premium());
		if (!list || list->files.empty()) {
			return;
		}
		const auto file = std::make_shared<Ui::PreparedFile>(
			std::move(list->files.front()));
		const auto video = ItemVideo(item);
		if (!video) {
			return;
		}
		Editor::OpenWithPreparedFile(
			parent,
			show,
			file.get(),
			st::sendMediaPreviewSize,
			crl::guard(this, [=](bool ok) {
				if (ok && _item == item) {
					_show = show;
					chosen(file);
				}
			}),
			PhotoSideLimit(true),
			video->dimensions);
	});
	FileDialog::GetOpenPath(
		parent,
		tr::lng_choose_cover(tr::now),
		FileDialog::ImagesFilter(),
		callback);
}

void VideoCoverUploader::chosen(std::shared_ptr<Ui::PreparedFile> file) {
	_preview = file->preview.isNull()
		? nullptr
		: std::make_unique<Image>(PrepareCoverPreview(file->preview));
	_photo = nullptr;
	startUpload(std::move(*file));
	_updated();
}

void VideoCoverUploader::startUpload(Ui::PreparedFile &&file) {
	cancelRequests();

	const auto item = _item;
	const auto video = ItemVideo(item);
	using ImageInfo = Ui::PreparedFileInformation::Image;
	const auto image = (video && file.information)
		? std::get_if<ImageInfo>(&file.information->media)
		: nullptr;
	if (!image) {
		return;
	}
	if (image->modifications) {
		image->data = Editor::ImageModified(
			std::move(image->data),
			image->modifications);
	}
	const auto dimensions = video->dimensions;
	const auto limit = PhotoSideLimit(true);
	const auto target = (dimensions.width() > limit
		|| dimensions.height() > limit)
		? dimensions.scaled(limit, limit, Qt::KeepAspectRatio)
		: dimensions;
	image->data = image->data.scaled(
		target,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	file.path = QString();
	file.content = QByteArray();

	const auto session = &item->history()->session();
	_session = session;
	_uploading = true;
	_radial.start();
	const auto generation = ++_generation;
	if (!_prepareQueue) {
		_prepareQueue = std::make_unique<TaskQueue>();
	}
	const auto weak = base::make_weak(this);
	_prepareQueue->addTask(std::make_unique<PrepareCoverTask>(
		FileLoadTask::Args{
			.session = session,
			.filepath = file.path,
			.content = file.content,
			.information = std::move(file.information),
			.videoCover = nullptr,
			.type = SendMediaType::Photo,
			.to = FileLoadTo(
				item->history()->peer->id,
				Api::SendOptions(),
				FullReplyTo(),
				MsgId()),
			.caption = TextWithTags(),
			.spoiler = false,
		},
		[=](std::shared_ptr<FilePrepareResult> result) {
			if (const auto strong = weak.get()) {
				strong->prepared(generation, std::move(result));
			}
		}));
}

void VideoCoverUploader::prepared(
		uint64 generation,
		std::shared_ptr<FilePrepareResult> result) {
	if (!_uploading
		|| (generation != _generation)
		|| !_session
		|| !_item) {
		return;
	} else if (!result || result->type != SendMediaType::Photo) {
		fail();
		return;
	}
	const auto session = _session;
	_prepared = result;
	if (!_photo) {
		_localPhoto = session->data().processPhoto(
			result->photo,
			result->photoThumbs);
	}
	_uploadId = FullMsgId(
		_item->history()->peer->id,
		session->data().nextLocalMessageId());

	_uploadLifetime.destroy();
	session->uploader().photoReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		if (_uploading && (data.fullId == _uploadId)) {
			uploaded(data);
		}
	}, _uploadLifetime);
	session->uploader().photoFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		if (_uploading && (id == _uploadId)) {
			fail();
		}
	}, _uploadLifetime);

	session->uploader().upload(_uploadId, std::move(result));
}

void VideoCoverUploader::uploaded(const Storage::UploadedMedia &data) {
	const auto item = _item;
	const auto session = _session;
	if (!item || !session) {
		fail();
		return;
	}
	_requestId = session->api().request(MTPmessages_UploadMedia(
		MTP_flags(0),
		MTPstring(), // business_connection_id
		item->history()->peer->input(),
		MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			data.info.file,
			MTP_vector<MTPInputDocument>(0),
			MTP_int(0),
			MTPInputDocument()) // video
	)).done([=](const MTPMessageMedia &result) {
		_requestId = 0;
		const auto failed = [&] { fail(); };
		result.match([&](const MTPDmessageMediaPhoto &data) {
			const auto photo = data.vphoto();
			if (!photo) {
				failed();
				return;
			}
			photo->match([&](const MTPDphoto &) {
				if (const auto local = base::take(_localPhoto)) {
					session->data().photoConvert(local, *photo);
					_photo = local;
				} else {
					_photo = session->data().processPhoto(*photo);
				}
				_uploading = false;
				_show = nullptr;
				_radial.stop();
				_uploadLifetime.destroy();
				if (const auto done = base::take(_reuploadDone)) {
					done(_photo);
				}
				_updated();
			}, [&](const MTPDphotoEmpty &) {
				failed();
			});
		}, [&](const auto &) {
			failed();
		});
	}).fail([=] {
		_requestId = 0;
		fail();
	}).send();
}

void VideoCoverUploader::reupload(Fn<void(PhotoData*)> done) {
	if (!_item || !_session || !_prepared || _uploading) {
		done(nullptr);
		return;
	}
	_reuploadDone = std::move(done);
	_uploading = true;
	_radial.start();
	prepared(++_generation, _prepared);
	_updated();
}

void VideoCoverUploader::fail() {
	const auto show = _show;
	const auto done = base::take(_reuploadDone);
	cancelRequests();
	_preview = nullptr;
	_photo = nullptr;
	if (show) {
		show->showToast(tr::lng_attach_failed(tr::now));
	}
	if (done) {
		done(nullptr);
	}
	_updated();
}

void VideoCoverUploader::cancelRequests() {
	if (const auto session = base::take(_session)) {
		if (_uploadId) {
			session->uploader().cancel(_uploadId);
		}
		if (_requestId) {
			session->api().request(base::take(_requestId)).cancel();
		}
	}
	_uploadId = FullMsgId();
	_requestId = 0;
	_uploading = false;
	_show = nullptr;
	_localPhoto = nullptr;
	_reuploadDone = nullptr;
	_radial.stop(anim::type::instant);
	_uploadLifetime.destroy();
	++_generation;
}

void VideoCoverUploader::reset() {
	cancelRequests();
	_item = nullptr;
	_preview = nullptr;
	_photo = nullptr;
	_prepared = nullptr;
}

Image *VideoCoverUploader::preview() const {
	return _preview.get();
}

PhotoData *VideoCoverUploader::photo() const {
	return _photo;
}

bool VideoCoverUploader::uploading() const {
	return _uploading;
}

void VideoCoverUploader::paintUploading(Painter &p, QRect to) {
	if (!_uploading && !_radial.animating()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto bg = st::historyEditCoverRadialBg;
	const auto inner = QRect(
		to.x() + (to.width() - bg) / 2,
		to.y() + (to.height() - bg) / 2,
		bg,
		bg);
	p.setPen(Qt::NoPen);
	p.setBrush(st::msgDateImgBg);
	p.drawEllipse(inner);
	const auto &size = st::historyEditCoverRadial.size;
	const auto position = QPoint(
		to.x() + (to.width() - size.width()) / 2,
		to.y() + (to.height() - size.height()) / 2);
	_radial.draw(
		p,
		position,
		position.x() * 2 + size.width());
}

} // namespace HistoryView
