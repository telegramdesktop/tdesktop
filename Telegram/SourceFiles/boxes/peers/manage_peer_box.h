/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class ManagePeerBox : public BoxContent {
public:
	ManagePeerBox(QWidget*, not_null<ChannelData*> channel);

	static bool Available(not_null<ChannelData*> channel);

protected:
	void prepare() override;

private:
	void setupContent();

	not_null<ChannelData*> _channel;

};
