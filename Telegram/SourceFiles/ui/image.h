/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Images {

void ClearRemote();
void ClearAll();
void CheckCacheSize();

ImagePtr Create(const QString &file, QByteArray format);
ImagePtr Create(const QString &url, QSize box);
ImagePtr Create(const QString &url, int width, int height);
ImagePtr Create(const QByteArray &filecontent, QByteArray format);
ImagePtr Create(QImage &&data, QByteArray format);
ImagePtr Create(
	const QByteArray &filecontent,
	QByteArray format,
	QImage &&data);
ImagePtr Create(int width, int height);
ImagePtr Create(const StorageImageLocation &location, int size = 0);
ImagePtr Create( // photoCachedSize
	const StorageImageLocation &location,
	const QByteArray &bytes);
ImagePtr Create(const MTPWebDocument &location);
ImagePtr Create(const MTPWebDocument &location, QSize box);
ImagePtr Create(
	const WebFileLocation &location,
	int width,
	int height,
	int size = 0);
ImagePtr Create(
	const WebFileLocation &location,
	QSize box,
	int size = 0);
ImagePtr Create(const GeoPointLocation &location);

class Source {
public:
	virtual ~Source();

	virtual void load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) = 0;
	virtual void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) = 0;
	virtual QImage takeLoaded() = 0;
	virtual void forget() = 0;

	virtual void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) = 0;
	virtual void automaticLoadSettingsChanged() = 0;

	virtual bool loading() = 0;
	virtual bool displayLoading() = 0;
	virtual void cancel() = 0;
	virtual float64 progress() = 0;
	virtual int loadOffset() = 0;

	virtual const StorageImageLocation &location() = 0;
	virtual void refreshFileReference(const QByteArray &data) = 0;
	virtual std::optional<Storage::Cache::Key> cacheKey() = 0;
	virtual void setDelayedStorageLocation(
		const StorageImageLocation &location) = 0;
	virtual void performDelayedLoad(Data::FileOrigin origin) = 0;
	virtual bool isDelayedStorageImage() const = 0;
	virtual void setImageBytes(const QByteArray &bytes) = 0;

	virtual int width() = 0;
	virtual int height() = 0;
	virtual void setInformation(int size, int width, int height) = 0;

	virtual QByteArray bytesForCache() = 0;

};

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
	void forget() override;

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
	void setInformation(int size, int width, int height) override;

	QByteArray bytesForCache() override;

private:
	QImage _data;
	QByteArray _format;

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
	void forget() override;

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
	void forget() override;

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

class HistoryItem;

class Image final {
public:
	explicit Image(std::unique_ptr<Images::Source> &&source);

	void replaceSource(std::unique_ptr<Images::Source> &&source);

	static ImagePtr Blank();

	const QPixmap &pix(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixRounded(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0,
		ImageRoundRadius radius = ImageRoundRadius::None,
		RectParts corners = RectPart::AllCorners) const;
	const QPixmap &pixBlurred(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixBlurredColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners,
		const style::color *colored = nullptr) const;
	const QPixmap &pixBlurredSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners) const;
	const QPixmap &pixCircled(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixBlurredCircled(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	QPixmap pixNoCache(
		Data::FileOrigin origin,
		int w = 0,
		int h = 0,
		Images::Options options = 0,
		int outerw = -1,
		int outerh = -1,
		const style::color *colored = nullptr) const;
	QPixmap pixColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0,
		bool smooth = false) const;
	QPixmap pixBlurredColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h = 0) const;

	void automaticLoad(Data::FileOrigin origin, const HistoryItem *item) {
		if (!loaded()) {
			_source->automaticLoad(origin, item);
		}
	}
	void automaticLoadSettingsChanged() {
		_source->automaticLoadSettingsChanged();
	}
	bool loading() const {
		return _source->loading();
	}
	bool displayLoading() const {
		return _source->displayLoading();
	}
	void cancel() {
		_source->cancel();
	}
	float64 progress() const {
		return loaded() ? 1. : _source->progress();
	}
	int loadOffset() const {
		return _source->loadOffset();
	}
	int width() const {
		return _source->width();
	}
	int height() const {
		return _source->height();
	}
	void setInformation(int size, int width, int height) {
		_source->setInformation(size, width, height);
	}
	void load(
			Data::FileOrigin origin,
			bool loadFirst = false,
			bool prior = true) {
		if (!loaded()) {
			_source->load(origin, loadFirst, prior);
		}
	}
	void loadEvenCancelled(
			Data::FileOrigin origin,
			bool loadFirst = false,
			bool prior = true) {
		if (!loaded()) {
			_source->loadEvenCancelled(origin, loadFirst, prior);
		}
	}
	const StorageImageLocation &location() const {
		return _source->location();
	}
	void refreshFileReference(const QByteArray &data) {
		_source->refreshFileReference(data);
	}
	std::optional<Storage::Cache::Key> cacheKey() const;
	QByteArray bytesForCache() const {
		return _source->bytesForCache();
	}
	bool isDelayedStorageImage() const {
		return _source->isDelayedStorageImage();
	}

	bool loaded() const;
	bool isNull() const;
	void forget() const;
	void setDelayedStorageLocation(
		Data::FileOrigin origin,
		const StorageImageLocation &location);
	void setImageBytes(const QByteArray &bytes);

	~Image();

private:
	void checkSource() const;
	void invalidateSizeCache() const;

	std::unique_ptr<Images::Source> _source;
	mutable QMap<uint64, QPixmap> _sizesCache;
	mutable QImage _data;

};
