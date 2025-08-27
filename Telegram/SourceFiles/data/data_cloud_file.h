/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "ui/image/image.h"
#include "ui/image/image_location.h"

class FileLoader;

namespace Storage {
namespace Cache {
class Database;
} // namespace Cache
} // namespace Storage

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct FileOrigin;

struct CloudFile final {
	enum class Flag : uchar {
		Cancelled = 0x01,
		Failed = 0x02,
		Loaded = 0x04,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };

	~CloudFile();

	void clear();

	ImageLocation location;
	std::unique_ptr<FileLoader> loader;
	int byteSize = 0;
	int progressivePartSize = 0;
	base::flags<Flag> flags;
};

class CloudImage final {
public:
	CloudImage();
	CloudImage(
		not_null<Main::Session*> session,
		const ImageWithLocation &data);

	// This method will replace the location and zero the _view pointer.
	void set(
		not_null<Main::Session*> session,
		const ImageWithLocation &data);

	void update(
		not_null<Main::Session*> session,
		const ImageWithLocation &data);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool failed() const;
	[[nodiscard]] bool loadedOnce() const;
	void load(not_null<Main::Session*> session, FileOrigin origin);
	[[nodiscard]] const ImageLocation &location() const;
	[[nodiscard]] int byteSize() const;

	[[nodiscard]] std::shared_ptr<QImage> createView();
	[[nodiscard]] std::shared_ptr<QImage> activeView() const;
	[[nodiscard]] bool isCurrentView(
		const std::shared_ptr<QImage> &view) const;

private:
	void setToActive(not_null<Main::Session*> session, QImage image);

	CloudFile _file;
	std::weak_ptr<QImage> _view;

};

void UpdateCloudFile(
	CloudFile &file,
	const ImageWithLocation &data,
	Storage::Cache::Database &cache,
	uint8 cacheTag,
	Fn<void(FileOrigin)> restartLoader,
	Fn<void(QImage, QByteArray)> usePreloaded = nullptr);

void LoadCloudFile(
	not_null<Main::Session*> session,
	CloudFile &file,
	FileOrigin origin,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag,
	Fn<bool()> finalCheck,
	Fn<void(QImage, QByteArray)> done,
	Fn<void(bool)> fail = nullptr,
	Fn<void()> progress = nullptr,
	int downloadFrontPartSize = 0);

void LoadCloudFile(
	not_null<Main::Session*> session,
	CloudFile &file,
	FileOrigin origin,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag,
	Fn<bool()> finalCheck,
	Fn<void(QByteArray)> done,
	Fn<void(bool)> fail = nullptr,
	Fn<void()> progress = nullptr);

} // namespace Data
