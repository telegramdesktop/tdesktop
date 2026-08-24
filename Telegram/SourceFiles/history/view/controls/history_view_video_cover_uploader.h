/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/radial_animation.h"

class TaskQueue;
struct FilePrepareResult;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Storage {
struct UploadedMedia;
} // namespace Storage

namespace Ui {
struct PreparedFile;
} // namespace Ui

class HistoryItem;
class Image;
class Painter;
class PhotoData;

namespace HistoryView {

class VideoCoverUploader final : public base::has_weak_ptr {
public:
	explicit VideoCoverUploader(Fn<void()> updated);
	~VideoCoverUploader();

	void choose(
		not_null<HistoryItem*> item,
		std::shared_ptr<ChatHelpers::Show> show);
	void reupload(Fn<void(PhotoData*)> done);
	void reset();

	[[nodiscard]] Image *preview() const;
	[[nodiscard]] PhotoData *photo() const;
	[[nodiscard]] bool uploading() const;

	void paintUploading(Painter &p, QRect to);

private:
	void chosen(std::shared_ptr<Ui::PreparedFile> file);
	void startUpload(Ui::PreparedFile &&file);
	void prepared(
		uint64 generation,
		std::shared_ptr<FilePrepareResult> result);
	void uploaded(const Storage::UploadedMedia &data);
	void fail();
	void cancelRequests();

	const Fn<void()> _updated;

	HistoryItem *_item = nullptr;
	std::shared_ptr<ChatHelpers::Show> _show;
	std::unique_ptr<Image> _preview;
	PhotoData *_photo = nullptr;
	PhotoData *_localPhoto = nullptr;
	bool _uploading = false;
	uint64 _generation = 0;
	Main::Session *_session = nullptr;
	FullMsgId _uploadId;
	mtpRequestId _requestId = 0;
	std::shared_ptr<FilePrepareResult> _prepared;
	Fn<void(PhotoData*)> _reuploadDone;
	Ui::InfiniteRadialAnimation _radial;
	std::unique_ptr<TaskQueue> _prepareQueue;
	rpl::lifetime _uploadLifetime;

};

} // namespace HistoryView
