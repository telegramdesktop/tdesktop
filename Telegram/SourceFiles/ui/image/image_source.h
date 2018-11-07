/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image.h"

namespace Images {

class ImageSource : public Source {
public:
	ImageSource(QImage &&data, const QByteArray &format);

	void load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	QImage takeLoaded() override;
	void unload() override;

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override;
	void automaticLoadSettingsChanged() override;

	bool loading() override;
	bool displayLoading() override;
	void cancel() override;
	float64 progress() override;
	int loadOffset() override;

	const StorageImageLocation &location() override;
	void refreshFileReference(const QByteArray &data) override;
	std::optional<Storage::Cache::Key> cacheKey() override;
	void setDelayedStorageLocation(
		const StorageImageLocation &location) override;
	void performDelayedLoad(Data::FileOrigin origin) override;
	bool isDelayedStorageImage() const override;
	void setImageBytes(const QByteArray &bytes) override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

	QByteArray bytesForCache() override;

private:
	QImage _data;
	QByteArray _format;
	QByteArray _bytes;
	int _width = 0;
	int _height = 0;

};

class LocalFileSource : public Source {
public:
	LocalFileSource(
		const QString &path,
		const QByteArray &content,
		const QByteArray &format,
		QImage &&data = QImage());

	void load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	QImage takeLoaded() override;
	void unload() override;

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override;
	void automaticLoadSettingsChanged() override;

	bool loading() override;
	bool displayLoading() override;
	void cancel() override;
	float64 progress() override;
	int loadOffset() override;

	const StorageImageLocation &location() override;
	void refreshFileReference(const QByteArray &data) override;
	std::optional<Storage::Cache::Key> cacheKey() override;
	void setDelayedStorageLocation(
		const StorageImageLocation &location) override;
	void performDelayedLoad(Data::FileOrigin origin) override;
	bool isDelayedStorageImage() const override;
	void setImageBytes(const QByteArray &bytes) override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

	QByteArray bytesForCache() override;

private:
	void ensureDimensionsKnown();

	QString _path;
	QByteArray _bytes;
	QByteArray _format;
	QImage _data;
	int _width = 0;
	int _height = 0;

};

class RemoteSource : public Source {
public:
	void load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	QImage takeLoaded() override;
	void unload() override;

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override;
	void automaticLoadSettingsChanged() override;

	bool loading() override;
	bool displayLoading() override;
	void cancel() override;
	float64 progress() override;
	int loadOffset() override;

	const StorageImageLocation &location() override;
	void refreshFileReference(const QByteArray &data) override;
	void setDelayedStorageLocation(
		const StorageImageLocation &location) override;
	void performDelayedLoad(Data::FileOrigin origin) override;
	bool isDelayedStorageImage() const override;
	void setImageBytes(const QByteArray &bytes) override;

	QByteArray bytesForCache() override;

	~RemoteSource();

protected:
	// If after loading the image we need to shrink it to fit into a
	// specific size, you can return this size here.
	virtual QSize shrinkBox() const = 0;
	virtual FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) = 0;

	void loadLocal();

private:
	bool loaderValid() const;
	void destroyLoaderDelayed(FileLoader *newValue = nullptr);

	FileLoader *_loader = nullptr;

};

class StorageSource : public RemoteSource {
public:
	StorageSource(
		const StorageImageLocation &location,
		int size);

	const StorageImageLocation &location() override;
	std::optional<Storage::Cache::Key> cacheKey() override;

	void refreshFileReference(const QByteArray &data) override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

protected:
	QSize shrinkBox() const override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	StorageImageLocation _location;
	int _size = 0;

};

class WebCachedSource : public RemoteSource {
public:
	WebCachedSource(const WebFileLocation &location, QSize box, int size = 0);
	WebCachedSource(
		const WebFileLocation &location,
		int width,
		int height,
		int size = 0);

	std::optional<Storage::Cache::Key> cacheKey() override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

protected:
	QSize shrinkBox() const override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	WebFileLocation _location;
	QSize _box;
	int _width = 0;
	int _height = 0;
	int _size = 0;

};

class GeoPointSource : public RemoteSource {
public:
	GeoPointSource(const GeoPointLocation &location);

	std::optional<Storage::Cache::Key> cacheKey() override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

protected:
	QSize shrinkBox() const override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	GeoPointLocation _location;
	int _size = 0;

};

class DelayedStorageSource : public StorageSource {
public:
	DelayedStorageSource();
	DelayedStorageSource(int width, int height);

	void load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) override;

	void setDelayedStorageLocation(
		const StorageImageLocation &location) override;
	bool isDelayedStorageImage() const override;
	void performDelayedLoad(Data::FileOrigin origin) override;

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override; // auto load photo
	void automaticLoadSettingsChanged() override;

	bool loading() override {
		return _location.isNull() ? _loadRequested : StorageSource::loading();
	}
	bool displayLoading() override;
	void cancel() override;


private:
	bool _loadRequested = false;
	bool _loadCancelled = false;
	bool _loadFromCloud = false;

};

class WebUrlSource : public RemoteSource {
public:
	// If !box.isEmpty() then resize the image to fit in this box.
	explicit WebUrlSource(const QString &url, QSize box = QSize());
	WebUrlSource(const QString &url, int width, int height);

	std::optional<Storage::Cache::Key> cacheKey() override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

protected:
	QSize shrinkBox() const override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

private:
	QString _url;
	QSize _box;
	int _size = 0;
	int _width = 0;
	int _height = 0;

};

} // namespace Images
