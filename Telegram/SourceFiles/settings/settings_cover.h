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

#include "base/observer.h"
#include "settings/settings_block_widget.h"

namespace Ui {
class FlatLabel;
class RoundButton;
class IconButton;
class UserpicButton;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {
class CoverDropArea;
} // namespace Profile

namespace Settings {

class CoverWidget : public BlockWidget {
	Q_OBJECT

public:
	CoverWidget(QWidget *parent, UserData *self);

private slots:
	void onPhotoShow();
	void onPhotoUploadStatusChanged(PeerId peerId = 0);
	void onCancelPhotoUpload();

	void onSetPhoto();
	void onEditName();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;

	void paintContents(Painter &p) override;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	PhotoData *validatePhoto() const;

	void refreshButtonsGeometry(int newWidth);
	void refreshNameGeometry(int newWidth);
	void refreshNameText();
	void refreshStatusText();

	void paintDivider(Painter &p);

	void showSetPhotoBox(const QImage &img);
	void resizeDropArea();
	void dropAreaHidden(Profile::CoverDropArea *dropArea);
	bool mimeDataHasImage(const QMimeData *mimeData) const;

	UserData *_self;

	object_ptr<Ui::UserpicButton> _userpicButton;
	object_ptr<Profile::CoverDropArea> _dropArea = { nullptr };

	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::IconButton> _editNameInline;
	object_ptr<Ui::LinkButton> _cancelPhotoUpload = { nullptr };

	QPoint _statusPosition;
	QString _statusText;
	bool _statusTextIsOnline = false;

	object_ptr<Ui::RoundButton> _setPhoto;
	object_ptr<Ui::RoundButton> _editName;
	bool _editNameVisible = true;

	int _dividerTop = 0;

};

} // namespace Settings
