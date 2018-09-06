/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "old_settings/settings_widget.h"

#include "old_settings/settings_inner_widget.h"
#include "old_settings/settings_fixed_bar.h"
#include "platform/platform_specific.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/toast/toast.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "data/data_session.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "lang/lang_cloud_manager.h"
#include "messenger.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "media/media_audio_track.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "styles/style_old_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace OldSettings {

Widget::Widget(QWidget *parent) {
	refreshLang();
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	_inner = setInnerWidget(object_ptr<InnerWidget>(this));
	setCloseClickHandler([]() {
		Ui::hideSettingsAndLayer();
	});
}

void Widget::refreshLang() {
	setTitle(lang(lng_menu_settings));

	update();
}

void Widget::scrollToUpdateRow() {
	if (const auto top = _inner->getUpdateTop(); top >= 0) {
		scrollToY(top);
	}
}

void Widget::keyPressEvent(QKeyEvent *e) {
	return LayerWidget::keyPressEvent(e);
}

void Widget::parentResized() {
	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto newWidth = st::settingsMaxWidth;
	auto newContentLeft = st::settingsMaxPadding;
	if (windowWidth <= st::settingsMaxWidth) {
		newWidth = windowWidth;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	} else if (windowWidth < st::settingsMaxWidth + 2 * st::settingsMargin) {
		newWidth = windowWidth - 2 * st::settingsMargin;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	}
	resizeToWidth(newWidth, newContentLeft);
}

void Widget::resizeUsingInnerHeight(int newWidth, int innerHeight) {
	if (!parentWidget()) return;

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto maxHeight = st::settingsFixedBarHeight + innerHeight;
	auto newHeight = maxHeight + st::boxRadius;
	if (newHeight > windowHeight || newWidth >= windowWidth) {
		newHeight = windowHeight;
	}

	auto roundedCorners = newHeight < windowHeight;
	setRoundedCorners(roundedCorners);
	setAttribute(Qt::WA_OpaquePaintEvent, !roundedCorners);

	setGeometry((windowWidth - newWidth) / 2, (windowHeight - newHeight) / 2, newWidth, newHeight);
	update();
}

} // namespace Settings
