/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

class PeerListContent;

namespace Data {
class CommunityInfo;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

class CommunityRequestableList final : public Ui::RpWidget {
public:
	CommunityRequestableList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Data::CommunityInfo*> community);
	~CommunityRequestableList();

	[[nodiscard]] rpl::producer<int> countValue() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	const not_null<Window::SessionController*> _controller;
	PeerListContent *_content = nullptr;
	rpl::variable<int> _count = 0;

};

} // namespace Dialogs
