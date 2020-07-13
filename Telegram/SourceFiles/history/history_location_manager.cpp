/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_location_manager.h"

#include "mainwidget.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/image/image.h"
#include "data/data_file_origin.h"
#include "platform/platform_specific.h"

QString LocationClickHandler::copyToClipboardText() const {
	return _text;
}

QString LocationClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_link(tr::now);
}

void LocationClickHandler::onClick(ClickContext context) const {
	if (!psLaunchMaps(_point)) {
		File::OpenUrl(_text);
	}
}

void LocationClickHandler::setup() {
	_text = Url(_point);
}

QString LocationClickHandler::Url(const Data::LocationPoint &point) {
	const auto latlon = point.latAsString() + ',' + point.lonAsString();
	return qsl("https://maps.google.com/maps?q=") + latlon + qsl("&ll=") + latlon + qsl("&z=16");
}
