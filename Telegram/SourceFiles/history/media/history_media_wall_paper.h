/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media_file.h"

class HistoryWallPaper : public HistoryFileMedia {
public:
	HistoryWallPaper(
		not_null<Element*> parent,
		not_null<DocumentData*> document,
		const QString &url = QString());

	void draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	DocumentData *getDocument() const override {
		return _data;
	}

	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return false;
	}
	bool skipBubbleTail() const override {
		return true;
	}
	bool isReadyForOpen() const override;
	QString additionalInfoString() const override;

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void fillPatternFieldsFrom(const QString &url);
	void validateThumbnail() const;
	void prepareThumbnailFrom(not_null<Image*> image, int good) const;

	not_null<DocumentData*> _data;
	int _pixw = 1;
	int _pixh = 1;
	mutable QPixmap _thumbnail;
	mutable int _thumbnailGood = -1; // -1 inline, 0 thumbnail, 1 good
	QColor _background;
	int _intensity = 0;

};
