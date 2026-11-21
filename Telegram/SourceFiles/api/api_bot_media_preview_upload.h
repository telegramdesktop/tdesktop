/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_stories.h"
#include "data/data_types.h"

struct FilePrepareResult;
class TaskQueue;
class UserData;

namespace Storage {
struct UploadedMedia;
} // namespace Storage

namespace Api {

class BotMediaPreviewUpload final {
public:
	explicit BotMediaPreviewUpload(not_null<UserData*> bot);
	~BotMediaPreviewUpload();

	void start(
		SendStoryMedia media,
		Fn<void(MTPInputMedia)> done,
		Fn<void()> fail);

private:
	void prepared(std::shared_ptr<FilePrepareResult> file);
	void uploaded(const Storage::UploadedMedia &media);
	void failed();

	const not_null<UserData*> _bot;
	std::shared_ptr<std::atomic<bool>> _cancel;
	std::unique_ptr<TaskQueue> _queue;
	std::shared_ptr<FilePrepareResult> _file;
	FullMsgId _uploadId;
	Fn<void(MTPInputMedia)> _done;
	Fn<void()> _fail;
	rpl::lifetime _lifetime;

};

} // namespace Api
