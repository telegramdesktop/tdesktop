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

} // namespace Images

class DelayedStorageImage;
class HistoryItem;

class Image {
public:
	Image(const QString &file, QByteArray format = QByteArray());
	Image(const QByteArray &filecontent, QByteArray format = QByteArray());
	Image(const QPixmap &pixmap, QByteArray format = QByteArray());
	Image(const QByteArray &filecontent, QByteArray format, const QPixmap &pixmap);

	static Image *Blank();

	virtual void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	}
	virtual void automaticLoadSettingsChanged() {
	}

	virtual bool loaded() const {
		return true;
	}
	virtual bool loading() const {
		return false;
	}
	virtual bool displayLoading() const {
		return false;
	}
	virtual void cancel() {
	}
	virtual float64 progress() const {
		return 1;
	}
	virtual int32 loadOffset() const {
		return 0;
	}

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

	int32 width() const {
		return qMax(countWidth(), 1);
	}

	int32 height() const {
		return qMax(countHeight(), 1);
	}

	virtual void load(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) {
	}

	virtual void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) {
	}

	virtual const StorageImageLocation &location() const {
		return StorageImageLocation::Null;
	}
	virtual std::optional<Storage::Cache::Key> cacheKey() const;

	bool isNull() const;

	void forget() const;

	QByteArray savedFormat() const {
		return _format;
	}
	QByteArray savedData() const {
		return _saved;
	}

	virtual DelayedStorageImage *toDelayedStorageImage() {
		return 0;
	}
	virtual const DelayedStorageImage *toDelayedStorageImage() const {
		return 0;
	}

	virtual ~Image();

protected:
	Image(QByteArray format = "PNG") : _format(format) {
	}

	void restore() const;
	virtual void checkload() const {
	}
	void invalidateSizeCache() const;

	virtual int32 countWidth() const {
		restore();
		return _data.width();
	}

	virtual int32 countHeight() const {
		restore();
		return _data.height();
	}

	mutable QByteArray _saved, _format;
	mutable bool _forgot = false;
	mutable QPixmap _data;

private:
	using Sizes = QMap<uint64, QPixmap>;
	mutable Sizes _sizesCache;

};

class RemoteImage : public Image {
public:
	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override; // auto load photo
	void automaticLoadSettingsChanged() override;

	bool loaded() const override;
	bool loading() const override {
		return amLoading();
	}
	bool displayLoading() const override;
	void cancel() override;
	float64 progress() const override;
	int32 loadOffset() const override;

	void setImageBytes(
		const QByteArray &bytes,
		const QByteArray &format = QByteArray());

	void load(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;

	~RemoteImage();

protected:
	// If after loading the image we need to shrink it to fit into a
	// specific size, you can return this size here.
	virtual QSize shrinkBox() const {
		return QSize();
	}
	virtual void setInformation(int32 size, int32 width, int32 height) = 0;
	virtual FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) = 0;

	void checkload() const override {
		doCheckload();
	}
	void loadLocal();

private:
	mutable FileLoader *_loader = nullptr;
	bool amLoading() const;
	void doCheckload() const;

	void destroyLoaderDelayed(FileLoader *newValue = nullptr) const;

};

class StorageImage : public RemoteImage {
public:
	explicit StorageImage(const StorageImageLocation &location, int32 size = 0);
	StorageImage(const StorageImageLocation &location, const QByteArray &bytes);

	const StorageImageLocation &location() const override {
		return _location;
	}
	std::optional<Storage::Cache::Key> cacheKey() const override;
	void refreshFileReference(const QByteArray &data) {
		_location.refreshFileReference(data);
	}

protected:
	void setInformation(int32 size, int32 width, int32 height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	int32 countWidth() const override;
	int32 countHeight() const override;

	StorageImageLocation _location;
	int32 _size;

};

class WebFileImage : public RemoteImage {
public:
	WebFileImage(const WebFileLocation &location, QSize box, int size = 0);
	WebFileImage(
		const WebFileLocation &location,
		int width,
		int height,
		int size = 0);

	std::optional<Storage::Cache::Key> cacheKey() const override;

protected:
	void setInformation(int size, int width, int height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	QSize shrinkBox() const override {
		return _box;
	}

	int countWidth() const override;
	int countHeight() const override;

	WebFileLocation _location;
	QSize _box;
	int _width = 0;
	int _height = 0;
	int _size = 0;

};

class GeoPointImage : public RemoteImage {
public:
	GeoPointImage(const GeoPointLocation &location);

	std::optional<Storage::Cache::Key> cacheKey() const override;

protected:
	void setInformation(int size, int width, int height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	int countWidth() const override;
	int countHeight() const override;

	GeoPointLocation _location;
	int _size = 0;

};

class DelayedStorageImage : public StorageImage {
public:
	DelayedStorageImage();
	DelayedStorageImage(int32 w, int32 h);
	//DelayedStorageImage(QByteArray &bytes);

	void setStorageLocation(
		Data::FileOrigin origin,
		const StorageImageLocation location);

	virtual DelayedStorageImage *toDelayedStorageImage() override {
		return this;
	}
	virtual const DelayedStorageImage *toDelayedStorageImage() const override {
		return this;
	}

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override; // auto load photo
	void automaticLoadSettingsChanged() override;

	bool loading() const override {
		return _location.isNull() ? _loadRequested : StorageImage::loading();
	}
	bool displayLoading() const override;
	void cancel() override;

	void load(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;

private:
	bool _loadRequested, _loadCancelled, _loadFromCloud;

};

class WebImage : public RemoteImage {
public:
	// If !box.isEmpty() then resize the image to fit in this box.
	WebImage(const QString &url, QSize box = QSize());
	WebImage(const QString &url, int width, int height);

	void setSize(int width, int height);

	std::optional<Storage::Cache::Key> cacheKey() const override;

protected:
	QSize shrinkBox() const override {
		return _box;
	}
	void setInformation(int32 size, int32 width, int32 height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	int32 countWidth() const override;
	int32 countHeight() const override;

private:
	QString _url;
	QSize _box;
	int32 _size, _width, _height;

};

namespace Images {
namespace details {

Image *Create(const QString &file, QByteArray format);
Image *Create(const QString &url, QSize box);
Image *Create(const QString &url, int width, int height);
Image *Create(const QByteArray &filecontent, QByteArray format);
Image *Create(const QPixmap &pixmap, QByteArray format);
Image *Create(
	const QByteArray &filecontent,
	QByteArray format,
	const QPixmap &pixmap);
Image *Create(int32 width, int32 height);
StorageImage *Create(const StorageImageLocation &location, int size = 0);
StorageImage *Create( // photoCachedSize
	const StorageImageLocation &location,
	const QByteArray &bytes);
Image *Create(const MTPWebDocument &location);
Image *Create(const MTPWebDocument &location, QSize box);
WebFileImage *Create(
	const WebFileLocation &location,
	int width,
	int height,
	int size = 0);
WebFileImage *Create(
	const WebFileLocation &location,
	QSize box,
	int size = 0);
GeoPointImage *Create(
	const GeoPointLocation &location);

} // namespace details
} // namespace Images
