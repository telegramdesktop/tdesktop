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
#include "settings/settings_cover.h"

#include "data/data_photo.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/special_buttons.h"
#include "observer_peer.h"
#include "lang/lang_keys.h"
#include "messenger.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "profile/profile_cover_drop_area.h"
#include "boxes/confirm_box.h"
#include "boxes/photo_crop_box.h"
#include "boxes/add_contact_box.h"
#include "styles/style_settings.h"
#include "styles/style_profile.h" // for divider
#include "platform/platform_file_utilities.h"

namespace Settings {

CoverWidget::CoverWidget(QWidget *parent, UserData *self)
: BlockWidget(parent, self, QString())
, _self(App::self())
, _userpicButton(
	this,
	App::wnd()->controller(),
	_self,
	Ui::UserpicButton::Role::OpenPhoto,
	st::settingsPhoto)
, _name(this, st::settingsNameLabel)
, _editNameInline(this, st::settingsEditButton)
, _setPhoto(this, langFactory(lng_settings_upload), st::settingsPrimaryButton)
, _editName(this, langFactory(lng_settings_edit), st::settingsSecondaryButton) {
	if (_self) {
		_self->updateFull();
	}
	setAcceptDrops(true);

	_name->setSelectable(true);
	_name->setContextCopyText(lang(lng_profile_copy_fullname));

	_setPhoto->setClickedCallback(App::LambdaDelayed(
		st::settingsPrimaryButton.ripple.hideDuration,
		this,
		[this] { chooseNewPhoto(); }));
	_editName->addClickHandler([this] { editName(); });
	_editNameInline->addClickHandler([this] { editName(); });

	auto observeEvents = Notify::PeerUpdate::Flag::NameChanged | Notify::PeerUpdate::Flag::PhotoChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	connect(
		&Messenger::Instance(),
		&Messenger::peerPhotoDone,
		this,
		&CoverWidget::onPhotoUploadStatusChanged);
	connect(
		&Messenger::Instance(),
		&Messenger::peerPhotoFail,
		this,
		&CoverWidget::onPhotoUploadStatusChanged);

	_userpicButton->addClickHandler([this] { showPhoto(); });
	validatePhoto();

	refreshNameText();

	subscribe(Global::RefConnectionTypeChanged(), [this] { refreshStatusText(); });
	refreshStatusText();
}

PhotoData *CoverWidget::validatePhoto() const {
	auto photo = (_self->photoId && _self->photoId != UnknownPeerPhotoId) ? App::photo(_self->photoId) : nullptr;
	_userpicButton->setPointerCursor(photo != nullptr && photo->date != 0);
	if ((_self->photoId == UnknownPeerPhotoId) || (_self->photoId && (!photo || !photo->date))) {
		Auth().api().requestFullPeer(_self);
		return nullptr;
	}
	return photo;
}

void CoverWidget::showPhoto() {
	if (auto photo = validatePhoto()) {
		Messenger::Instance().showPhoto(photo, _self);
	}
}

void CoverWidget::cancelPhotoUpload() {
	Messenger::Instance().cancelPhotoUpdate(_self->id);
	refreshStatusText();
}

int CoverWidget::resizeGetHeight(int newWidth) {
	int newHeight = 0;

	newHeight += st::settingsMarginTop;

	auto margins = getMargins();
	_userpicButton->moveToLeft(
		margins.left() + contentLeft() + st::settingsPhotoLeft,
		margins.top() + newHeight,
		newWidth);

	int infoLeft = _userpicButton->x() + _userpicButton->width();
	_statusPosition = QPoint(infoLeft + st::settingsStatusLeft, _userpicButton->y() + st::settingsStatusTop);
	if (_cancelPhotoUpload) {
		_cancelPhotoUpload->moveToLeft(
			margins.left()
				+ _statusPosition.x()
				+ st::settingsStatusFont->width(_statusText)
				+ st::settingsStatusFont->spacew,
			margins.top() + _statusPosition.y(),
			newWidth);
	}

	refreshButtonsGeometry(newWidth);
	refreshNameGeometry(newWidth);

	newHeight += st::settingsPhoto.size.height();
	newHeight += st::settingsMarginBottom;

	_dividerTop = newHeight;
	newHeight += st::profileDividerLeft.height();

	newHeight += st::settingsBlocksTop;

	resizeDropArea();
	return newHeight;
}

void CoverWidget::refreshButtonsGeometry(int newWidth) {
	auto margins = getMargins();
	auto buttonLeft = margins.left() + _userpicButton->x() + _userpicButton->width() + st::settingsButtonLeft;
	_setPhoto->moveToLeft(
		buttonLeft,
		margins.top() + _userpicButton->y() + st::settingsButtonTop,
		newWidth);
	buttonLeft += _setPhoto->width() + st::settingsButtonSkip;
	_editName->moveToLeft(
		buttonLeft,
		margins.top() + _setPhoto->y(),
		newWidth);
	_editNameVisible = (buttonLeft + _editName->width() + st::settingsButtonSkip <= newWidth);
	_editName->setVisible(_editNameVisible);
}

void CoverWidget::refreshNameGeometry(int newWidth) {
	auto margins = getMargins();
	auto infoLeft = _userpicButton->x() + _userpicButton->width();
	auto nameLeft = infoLeft + st::settingsNameLeft;
	auto nameTop = _userpicButton->y() + st::settingsNameTop;
	auto nameWidth = newWidth - infoLeft - st::settingsNameLeft;
	auto editNameInlineVisible = !_editNameVisible;
	if (editNameInlineVisible) {
		nameWidth -= _editNameInline->width();
	}

	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(
		margins.left() + nameLeft,
		margins.top() + nameTop,
		newWidth);

	_editNameInline->moveToLeft(
		margins.left() + nameLeft + _name->widthNoMargins() + st::settingsNameLabel.margin.right(),
		margins.top() + nameTop - st::settingsNameLabel.margin.top(),
		newWidth);
	_editNameInline->setVisible(editNameInlineVisible);
}

void CoverWidget::paintContents(Painter &p) {
	p.setFont(st::settingsStatusFont);
	p.setPen(_statusTextIsOnline ? st::settingsStatusFgActive : st::settingsStatusFg);
	p.drawTextLeft(_statusPosition.x(), _statusPosition.y(), width(), _statusText);

	paintDivider(p);
}

void CoverWidget::resizeDropArea() {
	if (_dropArea) {
		_dropArea->setGeometry(0, 0, width(), _dividerTop);
	}
}

void CoverWidget::dropAreaHidden(Profile::CoverDropArea *dropArea) {
	if (_dropArea == dropArea) {
		_dropArea.destroyDelayed();
	}
}

bool CoverWidget::mimeDataHasImage(const QMimeData *mimeData) const {
	if (!mimeData) return false;

	if (mimeData->hasImage()) return true;

	auto uriListFormat = qsl("text/uri-list");
	if (!mimeData->hasFormat(uriListFormat)) return false;

	const auto &urls = mimeData->urls();
	if (urls.size() != 1) return false;

	auto &url = urls.at(0);
	if (!url.isLocalFile()) return false;

	auto file = Platform::File::UrlToLocal(url);

	QFileInfo info(file);
	if (info.isDir()) return false;

	if (info.size() > App::kImageSizeLimit) return false;

	for (auto &ext : cImgExtensions()) {
		if (file.endsWith(ext, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void CoverWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!mimeDataHasImage(e->mimeData())) {
		e->ignore();
		return;
	}
	if (!_dropArea) {
		auto title = lang(lng_profile_drop_area_title);
		auto subtitle = lang(lng_settings_drop_area_subtitle);
		_dropArea.create(this, title, subtitle);
		resizeDropArea();
	}
	_dropArea->showAnimated();
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void CoverWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dropArea && !_dropArea->hiding()) {
		_dropArea->hideAnimated([this](Profile::CoverDropArea *area) { dropAreaHidden(area); });
	}
}

void CoverWidget::dropEvent(QDropEvent *e) {
	auto mimeData = e->mimeData();

	QImage img;
	if (mimeData->hasImage()) {
		img = qvariant_cast<QImage>(mimeData->imageData());
	} else {
		auto urls = mimeData->urls();
		if (urls.size() == 1) {
			auto &url = urls.at(0);
			if (url.isLocalFile()) {
				img = App::readImage(Platform::File::UrlToLocal(url));
			}
		}
	}

	if (!_dropArea->hiding()) {
		_dropArea->hideAnimated([this](Profile::CoverDropArea *area) { dropAreaHidden(area); });
	}
	e->acceptProposedAction();

	showSetPhotoBox(img);
}

void CoverWidget::paintDivider(Painter &p) {
	auto dividerHeight = st::profileDividerLeft.height();
	auto divider = rtlrect(0, _dividerTop, width(), dividerHeight, width());
	p.fillRect(divider, st::profileDividerBg);
	auto dividerFillTop = rtlrect(0, _dividerTop, width(), st::profileDividerTop.height(), width());
	st::profileDividerTop.fill(p, dividerFillTop);
	auto dividerFillBottom = rtlrect(0, _dividerTop + dividerHeight - st::profileDividerBottom.height(), width(), st::profileDividerBottom.height(), width());
	st::profileDividerBottom.fill(p, dividerFillBottom);
}

void CoverWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != _self) {
		return;
	}
	if (update.flags & Notify::PeerUpdate::Flag::NameChanged) {
		refreshNameText();
	}
	if (update.flags & Notify::PeerUpdate::Flag::PhotoChanged) {
		validatePhoto();
	}
}

void CoverWidget::refreshNameText() {
	_name->setText(App::peerName(_self));
	refreshNameGeometry(width());
}

void CoverWidget::refreshStatusText() {
	if (Messenger::Instance().isPhotoUpdating(_self->id)) {
		_statusText = lang(lng_settings_uploading_photo);
		_statusTextIsOnline = false;
		if (!_cancelPhotoUpload) {
			auto margins = getMargins();
			_cancelPhotoUpload.create(this, lang(lng_cancel), st::defaultLinkButton);
			_cancelPhotoUpload->addClickHandler([this] { cancelPhotoUpload(); });
			_cancelPhotoUpload->show();
			_cancelPhotoUpload->moveToLeft(
				margins.left()
					+ _statusPosition.x()
					+ st::settingsStatusFont->width(_statusText)
					+ st::settingsStatusFont->spacew,
				margins.top() + _statusPosition.y());
		}
		update();
		return;
	}

	_cancelPhotoUpload.destroy();
	auto state = MTP::dcstate();
	if (state == MTP::ConnectingState || state == MTP::DisconnectedState || state < 0) {
		_statusText = lang(lng_status_connecting);
		_statusTextIsOnline = false;
	} else {
		_statusText = lang(lng_status_online);
		_statusTextIsOnline = true;
	}
	update();
}

void CoverWidget::chooseNewPhoto() {
	auto imageExtensions = cImgExtensions();
	auto filter = qsl("Image files (*") + imageExtensions.join(qsl(" *")) + qsl(");;") + FileDialog::AllFilesFilter();
	FileDialog::GetOpenPath(lang(lng_choose_image), filter, base::lambda_guarded(this, [this](const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		QImage img;
		if (!result.remoteContent.isEmpty()) {
			img = App::readImage(result.remoteContent);
		} else {
			img = App::readImage(result.paths.front());
		}

		showSetPhotoBox(img);
	}));
}

void CoverWidget::editName() {
	Ui::show(Box<EditNameTitleBox>(self()));
}

void CoverWidget::showSetPhotoBox(const QImage &img) {
	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		Ui::show(Box<InformBox>(lang(lng_bad_photo)));
		return;
	}

	auto peer = _self;
	auto box = Ui::show(Box<PhotoCropBox>(img, peer));
	box->ready()
		| rpl::start_with_next([=](QImage &&image) {
			Messenger::Instance().uploadProfilePhoto(
				std::move(image),
				peer->id);
		}, box->lifetime());
	subscribe(box->boxClosing, [this] { onPhotoUploadStatusChanged(); });
}

void CoverWidget::onPhotoUploadStatusChanged(PeerId peerId) {
	if (!peerId || peerId == _self->id) {
		refreshStatusText();
	}
}

} // namespace Settings
