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

namespace Ui {
class Checkbox;
class RoundButton;
class InputArea;
} // namespace Ui

class SendFilesBox : public BoxContent {
	Q_OBJECT

public:
	SendFilesBox(QWidget*, QImage image, CompressConfirm compressed);
	SendFilesBox(QWidget*, const QStringList &files, CompressConfirm compressed);
	SendFilesBox(QWidget*, const QString &phone, const QString &firstname, const QString &lastname);

	void setConfirmedCallback(base::lambda<void(const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, bool ctrlShiftEnter)> callback) {
		_confirmedCallback = std::move(callback);
	}
	void setCancelledCallback(base::lambda<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onCompressedChange();
	void onSend(bool ctrlShiftEnter = false);
	void onCaptionResized();
	void onClose() {
		closeBox();
	}

private:
	void prepareSingleFileLayout();
	void prepareDocumentLayout();
	void tryToReadSingleFile();
	void prepareGifPreview();
	void clipCallback(Media::Clip::Notification notification);

	void updateTitleText();
	void updateBoxSize();
	void updateControlsGeometry();
	base::lambda<QString()> getSendButtonText() const;

	QString _titleText;
	QStringList _files;
	QImage _image;
	std::unique_ptr<FileLoadTask::MediaInformation> _information;

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

	QString _contactPhone;
	QString _contactFirstName;
	QString _contactLastName;
	EmptyUserpic _contactPhotoEmpty;

	base::lambda<void(const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, bool ctrlShiftEnter)> _confirmedCallback;
	base::lambda<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputArea> _caption = { nullptr };
	object_ptr<Ui::Checkbox> _compressed = { nullptr };

	QPointer<Ui::RoundButton> _send;

};

class EditCaptionBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	EditCaptionBox(QWidget*, HistoryMedia *media, FullMsgId msgId);

public slots:
	void onCaptionResized();
	void onSave(bool ctrlShiftEnter = false);
	void onClose() {
		closeBox();
	}

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateBoxSize();
	void prepareGifPreview(DocumentData *document);
	void clipCallback(Media::Clip::Notification notification);

	void saveDone(const MTPUpdates &updates);
	bool saveFail(const RPCError &error);

	int errorTopSkip() const;

	FullMsgId _msgId;
	bool _animated = false;
	bool _photo = false;
	bool _doc = false;

	QPixmap _thumb;
	Media::Clip::ReaderPointer _gifPreview;

	object_ptr<Ui::InputArea> _field = { nullptr };

	int _thumbx = 0;
	int _thumby = 0;
	int _thumbw = 0;
	int _thumbh = 0;
	Text _name;
	QString _status;
	int _statusw = 0;
	bool _isAudio = false;
	bool _isImage = false;

	bool _previewCancelled = false;
	mtpRequestId _saveRequestId = 0;

	QString _error;

};
