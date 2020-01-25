/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image.h"
#include "base/binary_guard.h"

class DocumentData;

namespace Data {

class GoodThumbSource : public Images::Source {
public:
	explicit GoodThumbSource(not_null<DocumentData*> document);

	void load(FileOrigin origin) override;
	void loadEvenCancelled(FileOrigin origin) override;
	QImage takeLoaded() override;
	void unload() override;

	void automaticLoad(
		FileOrigin origin,
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
	void performDelayedLoad(FileOrigin origin) override;
	bool isDelayedStorageImage() const override;
	void setImageBytes(const QByteArray &bytes) override;

	int width() override;
	int height() override;
	int bytesSize() override;
	void setInformation(int size, int width, int height) override;

	QByteArray bytesForCache() override;

private:
	void generate(base::binary_guard &&guard);

	// NB: This method is called from crl::async(), 'this' is unreliable.
	void ready(
		base::binary_guard &&guard,
		QImage &&image,
		int bytesSize,
		QByteArray &&bytesForCache = {});

	not_null<DocumentData*> _document;
	QImage _loaded;
	base::binary_guard _loading;
	int _width = 0;
	int _height = 0;
	int _bytesSize = 0;
	bool _empty = false;

};

} // namespace Data
