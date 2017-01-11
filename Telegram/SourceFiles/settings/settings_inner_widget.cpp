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
	refreshBlocks();
	subscribe(Global::RefSelfChanged(), [this]() { selfUpdated(); });
}

void InnerWidget::selfUpdated() {
	_self = App::self();
	refreshBlocks();

	if (_cover) {
		_cover->setContentLeft(_contentLeft);
		_cover->resizeToWidth(width());
	}
	for_const (auto block, _blocks) {
		block->setContentLeft(_contentLeft);
		block->resizeToWidth(width());
	}
	onBlockHeightUpdated();
}

void InnerWidget::refreshBlocks() {
	_cover.destroyDelayed();
	for_const (auto block, _blocks) {
		block->hide();
		block->deleteLater();
	}
	_blocks.clear();

	if (App::quitting()) {
		return;
	}
	if (_self) {
		_cover.create(this, _self);
		_blocks.push_back(new InfoWidget(this, _self));
		_blocks.push_back(new NotificationsWidget(this, _self));
	}
	_blocks.push_back(new GeneralWidget(this, _self));
	if (!cRetina()) {
		_blocks.push_back(new ScaleWidget(this, _self));
	}
	if (_self) {
		_blocks.push_back(new ChatSettingsWidget(this, _self));
		_blocks.push_back(new BackgroundWidget(this, _self));
		_blocks.push_back(new PrivacyWidget(this, _self));
	}
	_blocks.push_back(new AdvancedWidget(this, _self));

	if (_cover) {
		_cover->show();
		if (_showFinished) {
			_cover->showFinished();
		}
	}
	for_const (auto block, _blocks) {
		block->show();
		connect(block, SIGNAL(heightUpdated()), this, SLOT(onBlockHeightUpdated()));
	}
}

void InnerWidget::showFinished() {
	_showFinished = true;
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

	int result = refreshBlocksPositions(newWidth);
	return result;
}

int InnerWidget::refreshBlocksPositions(int newWidth) {
	int result = (_cover ? _cover->height() : 0) + st::settingsBlocksTop;
	for_const (auto block, _blocks) {
		if (block->isHidden()) {
			continue;
		}

		block->moveToLeft(0, result, newWidth);
		result += block->height();
	}
	return result;
}

void InnerWidget::onBlockHeightUpdated() {
	int newHeight = refreshBlocksPositions(width());
	if (newHeight != height()) {
		resize(width(), newHeight);
		emit heightUpdated();
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
