/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/download_path_box.h"

#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "storage/storage_account.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

DownloadPathBox::DownloadPathBox(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _path(Core::App().settings().downloadPath())
, _pathBookmark(Core::App().settings().downloadPathBookmark())
, _group(std::make_shared<Ui::RadioenumGroup<Directory>>(typeFromPath(_path)))
, _default(this, _group, Directory::Downloads, tr::lng_download_path_default_radio(tr::now), st::defaultBoxCheckbox)
, _temp(this, _group, Directory::Temp, tr::lng_download_path_temp_radio(tr::now), st::defaultBoxCheckbox)
, _dir(this, _group, Directory::Custom, tr::lng_download_path_dir_radio(tr::now), st::defaultBoxCheckbox)
, _pathLink(this, QString(), st::boxLinkButton) {
}

void DownloadPathBox::prepare() {
	addButton(tr::lng_connection_save(), [this] { save(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	setTitle(tr::lng_download_path_header());

	_group->setChangedCallback([this](Directory value) { radioChanged(value); });

	_pathLink->addClickHandler([=] { editPath(); });
	if (!_path.isEmpty() && _path != qsl("tmp")) {
		setPathText(QDir::toNativeSeparators(_path));
	}
	updateControlsVisibility();
}

void DownloadPathBox::updateControlsVisibility() {
	auto custom = (_group->value() == Directory::Custom);
	_pathLink->setVisible(custom);

	auto newHeight = st::boxOptionListPadding.top() + _default->getMargins().top() + _default->heightNoMargins() + st::boxOptionListSkip + _temp->heightNoMargins() + st::boxOptionListSkip + _dir->heightNoMargins();
	if (custom) {
		newHeight += st::downloadPathSkip + _pathLink->height();
	}
	newHeight += st::boxOptionListPadding.bottom() + _dir->getMargins().bottom();

	setDimensions(st::boxWideWidth, newHeight);
}

void DownloadPathBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_default->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxOptionListPadding.top() + _default->getMargins().top());
	_temp->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _default->bottomNoMargins() + st::boxOptionListSkip);
	_dir->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _temp->bottomNoMargins() + st::boxOptionListSkip);
	auto inputx = st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultCheck.diameter + st::defaultBoxCheckbox.textPosition.x();
	auto inputy = _dir->bottomNoMargins() + st::downloadPathSkip;

	_pathLink->moveToLeft(inputx, inputy);
}

void DownloadPathBox::radioChanged(Directory value) {
	if (value == Directory::Custom) {
		if (_path.isEmpty() || _path == qsl("tmp")) {
			_group->setValue(_path.isEmpty() ? Directory::Downloads : Directory::Temp);
			editPath();
		} else {
			setPathText(QDir::toNativeSeparators(_path));
		}
	} else if (value == Directory::Temp) {
		_path = qsl("tmp");
	} else {
		_path = QString();
	}
	updateControlsVisibility();
	update();
}

void DownloadPathBox::editPath() {
	const auto initialPath = [] {
		const auto path = Core::App().settings().downloadPath();
		if (!path.isEmpty() && path != qstr("tmp")) {
			return path.left(path.size() - (path.endsWith('/') ? 1 : 0));
		}
		return QString();
	}();
	const auto handleFolder = [=](const QString &result) {
		if (!result.isEmpty()) {
			_path = result.endsWith('/') ? result : (result + '/');
			_pathBookmark = psDownloadPathBookmark(_path);
			setPathText(QDir::toNativeSeparators(_path));
			_group->setValue(Directory::Custom);
		}
	};
	FileDialog::GetFolder(
		this,
		tr::lng_download_path_choose(tr::now),
		initialPath,
		crl::guard(this, handleFolder));
}

void DownloadPathBox::save() {
#ifndef OS_WIN_STORE
	auto value = _group->value();
	auto computePath = [this, value] {
		if (value == Directory::Custom) {
			return _path;
		} else if (value == Directory::Temp) {
			return qsl("tmp");
		}
		return QString();
	};
	Core::App().settings().setDownloadPathBookmark(
		(value == Directory::Custom) ? _pathBookmark : QByteArray());
	Core::App().settings().setDownloadPath(computePath());
	Core::App().saveSettings();
	closeBox();
#endif // OS_WIN_STORE
}

void DownloadPathBox::setPathText(const QString &text) {
	auto availw = st::boxWideWidth - st::boxPadding.left() - st::defaultCheck.diameter - st::defaultBoxCheckbox.textPosition.x() - st::boxPadding.right();
	_pathLink->setText(st::boxTextFont->elided(text, availw));
}
