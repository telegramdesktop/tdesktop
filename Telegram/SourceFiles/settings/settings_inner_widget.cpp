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
#include "settings/settings_inner_widget.h"

#include "styles/style_settings.h"
#include "settings/settings_cover.h"
#include "settings/settings_block_widget.h"
#include "settings/settings_info_widget.h"
#include "settings/settings_notifications_widget.h"
#include "settings/settings_general_widget.h"
#include "settings/settings_chat_settings_widget.h"
#include "settings/settings_scale_widget.h"
#include "settings/settings_background_widget.h"
#include "settings/settings_privacy_widget.h"
#include "settings/settings_advanced_widget.h"

namespace Settings {

InnerWidget::InnerWidget(QWidget *parent) : TWidget(parent)
, _self(App::self()) {
	if (_self) {
		_cover = new CoverWidget(this, _self);
	}
	refreshBlocks();
}

void InnerWidget::refreshBlocks() {
	for_const (auto block, _blocks) {
		block->deleteLater();
	}
	_blocks.clear();
	if (_self) {
		_blocks.push_back(new Settings::InfoWidget(this, _self));
		_blocks.push_back(new Settings::NotificationsWidget(this, _self));
	}
	_blocks.push_back(new Settings::GeneralWidget(this, _self));
	_blocks.push_back(new Settings::ScaleWidget(this, _self));
	if (_self) {
		_blocks.push_back(new Settings::ChatSettingsWidget(this, _self));
		_blocks.push_back(new Settings::BackgroundWidget(this, _self));
		_blocks.push_back(new Settings::PrivacyWidget(this, _self));
	}
	_blocks.push_back(new Settings::AdvancedWidget(this, _self));
	for_const (auto block, _blocks) {
		connect(block, SIGNAL(heightUpdated()), this, SLOT(onBlockHeightUpdated()));
	}
}

void InnerWidget::showFinished() {
	if (_cover) {
		_cover->showFinished();
	}
}

int InnerWidget::resizeGetHeight(int newWidth) {
	if (_cover) {
		_cover->setContentLeft(_contentLeft);
		_cover->resizeToWidth(newWidth);
	}
	for_const (auto block, _blocks) {
		block->setContentLeft(_contentLeft);
		block->resizeToWidth(newWidth);
	}

	int result = refreshBlocksPositions();
	return result;
}

int InnerWidget::refreshBlocksPositions() {
	int result = (_cover ? _cover->height() : 0) + st::settingsBlocksTop;
	for_const (auto block, _blocks) {
		if (block->isHidden()) {
			continue;
		}

		block->moveToLeft(0, result);
		result += block->height();
	}
	return result;
}

void InnerWidget::onBlockHeightUpdated() {
	int newHeight = refreshBlocksPositions();
	if (newHeight != height()) {
		resize(width(), newHeight);
	}
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	for_const (auto block, _blocks) {
		int blockY = block->y();
		block->setVisibleTopBottom(visibleTop - blockY, visibleBottom - blockY);
	}
}

} // namespace Settings
