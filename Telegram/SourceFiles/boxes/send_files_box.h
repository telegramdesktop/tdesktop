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

#include <rpl/variable.h>
#include "boxes/abstract_box.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"

namespace Ui {
template <typename Enum>
class Radioenum;
template <typename Enum>
class RadioenumGroup;
class RoundButton;
class InputArea;
struct GroupMediaLayout;
} // namespace Ui

enum class SendFilesWay {
	Album,
	Photos,
	Files,
};

class SendFilesBox : public BoxContent {
public:
	SendFilesBox(
		QWidget*,
		Storage::PreparedList &&list,
		CompressConfirm compressed);

	void setConfirmedCallback(
		base::lambda<void(
			Storage::PreparedList &&list,
			SendFilesWay way,
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
	class AlbumPreview;

	void initSendWay();
	void initPreview(rpl::producer<int> desiredPreviewHeight);

	void setupControls();
	void setupSendWayControls();
	void setupCaption();
	void setupShadows(
		not_null<Ui::ScrollArea*> wrap,
		not_null<AlbumPreview*> content);

	void prepareSingleFilePreview();
	void prepareAlbumPreview();
	void applyAlbumOrder();

	void send(bool ctrlShiftEnter = false);
	void captionResized();

	void setupTitleText();
	void updateBoxSize();
	void updateControlsGeometry();

	QString _titleText;
	int _titleHeight = 0;

	Storage::PreparedList _list;

	CompressConfirm _compressConfirm = CompressConfirm::None;

	base::lambda<void(
		Storage::PreparedList &&list,
		SendFilesWay way,
		const QString &caption,
		bool ctrlShiftEnter)> _confirmedCallback;
	base::lambda<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputArea> _caption = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendAlbum = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendPhotos = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendFiles = { nullptr };
	std::shared_ptr<Ui::RadioenumGroup<SendFilesWay>> _sendWay;

	rpl::variable<int> _footerHeight = 0;

	QWidget *_preview = nullptr;
	AlbumPreview *_albumPreview = nullptr;
	int _albumVideosCount = 0;
	int _albumPhotosCount = 0;

	QPointer<Ui::RoundButton> _send;

};
