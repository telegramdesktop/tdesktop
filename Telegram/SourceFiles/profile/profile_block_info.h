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
#pragma once

#include "profile/profile_block_widget.h"

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class InfoWidget : public BlockWidget, public RPCSender {
public:
	InfoWidget(QWidget *parent, PeerData *peer);

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

	void leaveEventHook(QEvent *e) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void refreshLabels();
	void refreshAbout();
	void refreshMobileNumber();
	void refreshUsername();
	void refreshChannelLink();
	void refreshVisibility();

	// labelWidget may be nullptr.
	void setLabeledText(object_ptr<Ui::FlatLabel> *labelWidget, const QString &label,
		object_ptr<Ui::FlatLabel> *textWidget, const TextWithEntities &textWithEntities,
		const style::FlatLabel &st, const QString &copyText);
	void setSingleLineLabeledText(object_ptr<Ui::FlatLabel> *labelWidget, const QString &label,
		object_ptr<Ui::FlatLabel> *textWidget, const TextWithEntities &textWithEntities, const QString &copyText);

	object_ptr<Ui::FlatLabel> _about = { nullptr };
	object_ptr<Ui::FlatLabel> _channelLinkLabel = { nullptr };
	object_ptr<Ui::FlatLabel> _channelLink = { nullptr };
	object_ptr<Ui::FlatLabel> _channelLinkShort = { nullptr };
	object_ptr<Ui::FlatLabel> _mobileNumberLabel = { nullptr };
	object_ptr<Ui::FlatLabel> _mobileNumber = { nullptr };
	object_ptr<Ui::FlatLabel> _bioLabel = { nullptr };
	object_ptr<Ui::FlatLabel> _bio = { nullptr };
	object_ptr<Ui::FlatLabel> _usernameLabel = { nullptr };
	object_ptr<Ui::FlatLabel> _username = { nullptr };

};

} // namespace Profile
