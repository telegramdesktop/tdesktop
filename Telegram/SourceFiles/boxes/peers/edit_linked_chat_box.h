/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class EditLinkedChatBox : public BoxContent {
public:
	EditLinkedChatBox(
		QWidget*,
		not_null<ChannelData*> channel,
		not_null<ChannelData*> chat,
		Fn<void(ChannelData*)> callback);
	EditLinkedChatBox(
		QWidget*,
		not_null<ChannelData*> channel,
		const std::vector<not_null<ChannelData*>> &chats,
		Fn<void(ChannelData*)> callback);

protected:
	void prepare() override;

private:
	object_ptr<Ui::RpWidget> setupContent(
		not_null<ChannelData*> channel,
		ChannelData *chat,
		const std::vector<not_null<ChannelData*>> &chats,
		Fn<void(ChannelData*)> callback);

	not_null<ChannelData*> _channel;
	object_ptr<Ui::RpWidget> _content;

};