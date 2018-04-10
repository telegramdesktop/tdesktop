/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_inner_widget.h"

#include "lang/lang_instance.h"
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

InnerWidget::InnerWidget(QWidget *parent) : LayerInner(parent)
, _blocks(this)
, _self(App::self()) {
	refreshBlocks();
	subscribe(Global::RefSelfChanged(), [this] { fullRebuild(); });
	subscribe(Lang::Current().updated(), [this] { fullRebuild(); });
}

void InnerWidget::fullRebuild() {
	_self = App::self();
	refreshBlocks();
}

void InnerWidget::refreshBlocks() {
	if (App::quitting()) {
		_cover.destroy();
		_blocks.destroy();
		return;
	}
	_cover = _self
		? object_ptr<CoverWidget>(this, _self)
		: nullptr;
	_blocks = object_ptr<Ui::VerticalLayout>(this);
	resizeToWidth(width(), _contentLeft);

	if (_self) {
		_blocks->add(object_ptr<InfoWidget>(this, _self));
		_blocks->add(object_ptr<NotificationsWidget>(this, _self));
	}
	_blocks->add(object_ptr<GeneralWidget>(this, _self));
	if (!cRetina()) {
		_blocks->add(object_ptr<ScaleWidget>(this, _self));
	}
	if (_self) {
		_blocks->add(object_ptr<ChatSettingsWidget>(this, _self));
		_blocks->add(object_ptr<BackgroundWidget>(this, _self));
		_blocks->add(object_ptr<PrivacyWidget>(this, _self));
	}
	_blocks->add(object_ptr<AdvancedWidget>(this, _self));

	if (_cover) {
		_cover->show();
	}
	_blocks->show();
	_blocks->heightValue(
	) | rpl::start_with_next([this](int blocksHeight) {
		resize(width(), _blocks->y() + blocksHeight);
	}, lifetime());
}

int InnerWidget::resizeGetHeight(int newWidth) {
	if (_cover) {
		_cover->setContentLeft(_contentLeft);
		_cover->resizeToWidth(newWidth);
	}
	_blocks->resizeToWidth(newWidth - 2 * _contentLeft);
	_blocks->moveToLeft(
		_contentLeft,
		(_cover ? (_cover->y() + _cover->height()) : 0)
			+ st::settingsBlocksTop);
	return height();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(
		_cover,
		visibleTop,
		visibleBottom);
	setChildVisibleTopBottom(
		_blocks,
		visibleTop,
		visibleBottom);
}

} // namespace Settings
