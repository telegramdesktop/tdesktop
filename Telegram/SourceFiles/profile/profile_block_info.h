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

#include "profile/profile_block_widget.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class LeftOutlineButton;
} // namespace Ui

namespace Profile {

struct CommonGroupsEvent;
class InfoWidget : public BlockWidget, public RPCSender {
public:
	InfoWidget(QWidget *parent, PeerData *peer);

	void setShowCommonGroupsObservable(base::Observable<CommonGroupsEvent> *observable);

	void showFinished() override;

	void restoreState(const SectionMemento *memento) override;

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

	void leaveEvent(QEvent *e) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void refreshLabels();
	void refreshAbout();
	void refreshMobileNumber();
	void refreshUsername();
	void refreshChannelLink();
	void refreshCommonGroups();
	void refreshVisibility();

	int getCommonGroupsCount() const;
	void onForceHideCommonGroups();
	void onShowCommonGroups();
	void slideCommonGroupsDown();

	// labelWidget may be nullptr.
	void setLabeledText(ChildWidget<Ui::FlatLabel> *labelWidget, const QString &label,
		ChildWidget<Ui::FlatLabel> *textWidget, const TextWithEntities &textWithEntities, const QString &copyText);

	ChildWidget<Ui::FlatLabel> _about = { nullptr };
	ChildWidget<Ui::FlatLabel> _channelLinkLabel = { nullptr };
	ChildWidget<Ui::FlatLabel> _channelLink = { nullptr };
	ChildWidget<Ui::FlatLabel> _channelLinkShort = { nullptr };
	ChildWidget<Ui::FlatLabel> _mobileNumberLabel = { nullptr };
	ChildWidget<Ui::FlatLabel> _mobileNumber = { nullptr };
	ChildWidget<Ui::FlatLabel> _usernameLabel = { nullptr };
	ChildWidget<Ui::FlatLabel> _username = { nullptr };
	ChildWidget<Ui::LeftOutlineButton> _commonGroups = { nullptr };

	FloatAnimation _height;
	bool _showFinished = false;

	bool _forceHiddenCommonGroups = false;
	mtpRequestId _getCommonGroupsRequestId = 0;

	base::Observable<CommonGroupsEvent> *_showCommonGroupsObservable = nullptr;

};

} // namespace Profile
