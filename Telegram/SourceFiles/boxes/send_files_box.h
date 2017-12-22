/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstract_box.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"

namespace Ui {
class Checkbox;
class RoundButton;
class InputArea;
struct GroupMediaLayout;
} // namespace Ui

class SendFilesBox : public BoxContent {
public:
	SendFilesBox(
		QWidget*,
		Storage::PreparedList &&list,
		CompressConfirm compressed);

	void setConfirmedCallback(
		base::lambda<void(
			Storage::PreparedList &&list,
			bool compressed,
			const QString &caption,
			bool ctrlShiftEnter)> callback) {
		_confirmedCallback = std::move(callback);
	}
	void setCancelledCallback(base::lambda<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	~SendFilesBox();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void prepareSingleFileLayout();
	void prepareDocumentLayout();
	void prepareGifPreview();
	void clipCallback(Media::Clip::Notification notification);

	void send(bool ctrlShiftEnter = false);
	void captionResized();
	void compressedChange();

	void updateTitleText();
	void updateBoxSize();
	void updateControlsGeometry();
	base::lambda<QString()> getSendButtonText() const;

	QString _titleText;
	Storage::PreparedList _list;

	CompressConfirm _compressConfirm = CompressConfirm::None;
	bool _animated = false;

	QPixmap _preview;
	int _previewLeft = 0;
	int _previewWidth = 0;
	int _previewHeight = 0;
	Media::Clip::ReaderPointer _gifPreview;

	QPixmap _fileThumb;
	Text _nameText;
	bool _fileIsAudio = false;
	bool _fileIsImage = false;
	QString _statusText;
	int _statusWidth = 0;

	base::lambda<void(
		Storage::PreparedList &&list,
		bool compressed,
		const QString &caption,
		bool ctrlShiftEnter)> _confirmedCallback;
	base::lambda<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputArea> _caption = { nullptr };
	object_ptr<Ui::Checkbox> _compressed = { nullptr };

	QPointer<Ui::RoundButton> _send;

};

class SendAlbumBox : public BoxContent {
public:
	SendAlbumBox(QWidget*, Storage::PreparedList &&list);

	void setConfirmedCallback(
		base::lambda<void(
			Storage::PreparedList &&list,
			const QString &caption,
			bool ctrlShiftEnter)> callback) {
		_confirmedCallback = std::move(callback);
	}
	void setCancelledCallback(base::lambda<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	~SendAlbumBox();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Thumb;

	void prepareThumbs();
	Thumb prepareThumb(
		const QImage &preview,
		const Ui::GroupMediaLayout &layout) const;

	void send(bool ctrlShiftEnter = false);
	void captionResized();

	void updateBoxSize();
	void updateControlsGeometry();

	Storage::PreparedList _list;

	std::vector<Thumb> _thumbs;
	int _thumbsHeight = 0;

	base::lambda<void(Storage::PreparedList &&list, const QString &caption, bool ctrlShiftEnter)> _confirmedCallback;
	base::lambda<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputArea> _caption = { nullptr };

};
