/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
public:
	CoverWidget(QWidget *parent, UserData *self);

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

	void showPhoto();
	void cancelPhotoUpload();
	void chooseNewPhoto();
	void editName();

	void onPhotoUploadStatusChanged(PeerId peerId = 0);

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
