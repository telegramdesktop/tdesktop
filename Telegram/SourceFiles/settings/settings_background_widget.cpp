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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "settings/settings_background_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "mainwidget.h"
#include "boxes/backgroundbox.h"
#include "ui/widgets/widget_slide_wrap.h"
#include "localstorage.h"
#include "mainwindow.h"
#include "window/chat_background.h"

namespace Settings {

BackgroundRow::BackgroundRow(QWidget *parent) : TWidget(parent)
, _chooseFromGallery(this, lang(lng_settings_bg_from_gallery), st::defaultBoxLinkButton)
, _chooseFromFile(this, lang(lng_settings_bg_from_file), st::defaultBoxLinkButton)
, _radial(animation(this, &BackgroundRow::step_radial)) {
	Window::chatBackground()->initIfEmpty();

	updateImage();

	connect(_chooseFromGallery, SIGNAL(clicked()), this, SIGNAL(chooseFromGallery()));
	connect(_chooseFromFile, SIGNAL(clicked()), this, SIGNAL(chooseFromFile()));
}

void BackgroundRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	bool radial = false;
	float64 radialOpacity = 0;
	if (_radial.animating()) {
		_radial.step(getms());
		radial = _radial.animating();
		radialOpacity = _radial.opacity();
	}
	if (radial) {
		auto backThumb = App::main() ? App::main()->newBackgroundThumb() : ImagePtr();
		if (backThumb->isNull()) {
			p.drawPixmap(0, 0, _background);
		} else {
			const QPixmap &pix = App::main()->newBackgroundThumb()->pixBlurred(st::setBackgroundSize);
			p.drawPixmap(0, 0, st::setBackgroundSize, st::setBackgroundSize, pix, 0, (pix.height() - st::setBackgroundSize) / 2, st::setBackgroundSize, st::setBackgroundSize);
		}

		auto outer = radialRect();
		QRect inner(QPoint(outer.x() + (outer.width() - st::radialSize.width()) / 2, outer.y() + (outer.height() - st::radialSize.height()) / 2), st::radialSize);
		p.setPen(Qt::NoPen);
		p.setBrush(st::black);
		p.setOpacity(radialOpacity * st::radialBgOpacity);

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity(1);
		QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
		_radial.draw(p, arc, st::radialLine, st::white);
	} else {
		p.drawPixmap(0, 0, _background);
	}
}

int BackgroundRow::resizeGetHeight(int newWidth) {
	int linkLeft = st::settingsBackgroundSize + st::settingsSmallSkip;
	int linkWidth = newWidth - linkLeft;
	_chooseFromGallery->resizeToWidth(qMin(linkWidth, _chooseFromGallery->naturalWidth()));
	_chooseFromFile->resizeToWidth(qMin(linkWidth, _chooseFromFile->naturalWidth()));

	_chooseFromGallery->moveToLeft(linkLeft, 0, newWidth);
	_chooseFromFile->moveToLeft(linkLeft, _chooseFromGallery->height() + st::settingsSmallSkip, newWidth);

	return st::settingsBackgroundSize;
}

float64 BackgroundRow::radialProgress() const {
	if (auto m = App::main()) {
		return m->chatBackgroundProgress();
	}
	return 1.;
}

bool BackgroundRow::radialLoading() const {
	if (auto m = App::main()) {
		if (m->chatBackgroundLoading()) {
			m->checkChatBackground();
			if (m->chatBackgroundLoading()) {
				return true;
			} else {
				const_cast<BackgroundRow*>(this)->updateImage();
			}
		}
	}
	return false;
}

QRect BackgroundRow::radialRect() const {
	return QRect(0, 0, st::setBackgroundSize, st::setBackgroundSize);
}

void BackgroundRow::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (auto shift = radialTimeShift()) {
			_radial.update(radialProgress(), !radialLoading(), getms() + shift);
		}
	}
}

uint64 BackgroundRow::radialTimeShift() const {
	return st::radialDuration;
}

void BackgroundRow::step_radial(uint64 ms, bool timer) {
	_radial.update(radialProgress(), !radialLoading(), ms + radialTimeShift());
	if (timer && _radial.animating()) {
		rtlupdate(radialRect());
	}
}

void BackgroundRow::updateImage() {
	int32 size = st::setBackgroundSize * cIntRetinaFactor();
	QImage back(size, size, QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		QPainter p(&back);
		auto &pix = Window::chatBackground()->image();
		int sx = (pix.width() > pix.height()) ? ((pix.width() - pix.height()) / 2) : 0;
		int sy = (pix.height() > pix.width()) ? ((pix.height() - pix.width()) / 2) : 0;
		int s = (pix.width() > pix.height()) ? pix.height() : pix.width();
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawPixmap(0, 0, st::setBackgroundSize, st::setBackgroundSize, pix, sx, sy, s, s);
	}
	imageRound(back, ImageRoundRadius::Small);
	_background = App::pixmapFromImageInPlace(std_::move(back));
	_background.setDevicePixelRatio(cRetinaFactor());

	rtlupdate(radialRect());

	if (radialLoading()) {
		radialStart();
	}
}

BackgroundWidget::BackgroundWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_background)) {
	FileDialog::registerObserver(this, &BackgroundWidget::notifyFileQueryUpdated);
	createControls();

	subscribe(Window::chatBackground(), [this](const Window::ChatBackgroundUpdate &update) {
		using Update = Window::ChatBackgroundUpdate;
		if (update.type == Update::Type::New) {
			_background->updateImage();
		} else if (update.type == Update::Type::Start) {
			needBackgroundUpdate(update.tiled);
		}
	});
	subscribe(Adaptive::Changed(), [this]() {
		if (Global::AdaptiveLayout() == Adaptive::WideLayout) {
			_adaptive->slideDown();
		} else {
			_adaptive->slideUp();
		}
	});
}

void BackgroundWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSmallSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));

	addChildRow(_background, margin);
	connect(_background, SIGNAL(chooseFromGallery()), this, SLOT(onChooseFromGallery()));
	connect(_background, SIGNAL(chooseFromFile()), this, SLOT(onChooseFromFile()));

	addChildRow(_tile, margin, lang(lng_settings_bg_tile), SLOT(onTile()), Window::chatBackground()->tile());
	addChildRow(_adaptive, margin, slidedPadding, lang(lng_settings_adaptive_wide), SLOT(onAdaptive()), Global::AdaptiveForWide());
	if (Global::AdaptiveLayout() != Adaptive::WideLayout) {
		_adaptive->hideFast();
	}
}

void BackgroundWidget::onChooseFromGallery() {
	Ui::showLayer(new BackgroundBox());
}

void BackgroundWidget::needBackgroundUpdate(bool tile) {
	_tile->setChecked(tile);
	_background->updateImage();
}

void BackgroundWidget::onChooseFromFile() {
	QStringList imgExtensions(cImgExtensions());
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;") + filedialogAllFilesFilter());

	_chooseFromFileQueryId = FileDialog::queryReadFile(lang(lng_choose_images), filter);
}

void BackgroundWidget::notifyFileQueryUpdated(const FileDialog::QueryUpdate &update) {
	if (_chooseFromFileQueryId != update.queryId) {
		return;
	}
	_chooseFromFileQueryId = 0;

	if (update.filePaths.isEmpty() && update.remoteContent.isEmpty()) {
		return;
	}

	QImage img;
	if (!update.remoteContent.isEmpty()) {
		img = App::readImage(update.remoteContent);
	} else {
		img = App::readImage(update.filePaths.front());
	}

	if (img.isNull() || img.width() <= 0 || img.height() <= 0) return;

	if (img.width() > 4096 * img.height()) {
		img = img.copy((img.width() - 4096 * img.height()) / 2, 0, 4096 * img.height(), img.height());
	} else if (img.height() > 4096 * img.width()) {
		img = img.copy(0, (img.height() - 4096 * img.width()) / 2, img.width(), 4096 * img.width());
	}

	App::initBackground(-1, img);
	_tile->setChecked(false);
	_background->updateImage();
}

void BackgroundWidget::onTile() {
	Window::chatBackground()->setTile(_tile->checked());
}

void BackgroundWidget::onAdaptive() {
	if (Global::AdaptiveForWide() != _adaptive->entity()->checked()) {
		Global::SetAdaptiveForWide(_adaptive->entity()->checked());
		Adaptive::Changed().notify();
		Local::writeUserSettings();
	}
}

} // namespace Settings
