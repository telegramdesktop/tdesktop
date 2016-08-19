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
#pragma once

#include "settings/settings_block_widget.h"

class FlatLabel;

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Settings {

class InfoWidget : public BlockWidget {
public:
	InfoWidget(QWidget *parent, UserData *self);

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void refreshControls();
	void refreshMobileNumber();
	void refreshUsername();
	void refreshLink();

	// labelWidget may be nullptr.
	void setLabeledText(ChildWidget<FlatLabel> *labelWidget, const QString &label,
		ChildWidget<FlatLabel> *textWidget, const TextWithEntities &textWithEntities, const QString &copyText);

	ChildWidget<FlatLabel> _mobileNumberLabel = { nullptr };
	ChildWidget<FlatLabel> _mobileNumber = { nullptr };
	ChildWidget<FlatLabel> _usernameLabel = { nullptr };
	ChildWidget<FlatLabel> _username = { nullptr };
	ChildWidget<FlatLabel> _linkLabel = { nullptr };
	ChildWidget<FlatLabel> _link = { nullptr };
	ChildWidget<FlatLabel> _linkShort = { nullptr };

};

} // namespace Settings
