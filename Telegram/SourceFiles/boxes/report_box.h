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

#include "abstractbox.h"

class ReportBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	ReportBox(ChannelData *channel);

private slots:
	void onReport();
	void onChange();
	void onDescriptionResized();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override {
		showChildren();
	}
	void hideAll() override {
		hideChildren();
	}
	void setInnerFocus() override;

private:
	void updateMaxHeight();

	void reportDone(const MTPBool &result);
	bool reportFail(const RPCError &error);

	ChannelData *_channel;

	ChildWidget<Radiobutton> _reasonSpam;
	ChildWidget<Radiobutton> _reasonViolence;
	ChildWidget<Radiobutton> _reasonPornography;
	ChildWidget<Radiobutton> _reasonOther;
	ChildWidget<InputArea> _reasonOtherText = { nullptr };

	ChildWidget<BoxButton> _report, _cancel;

	enum Reason {
		ReasonSpam,
		ReasonViolence,
		ReasonPornography,
		ReasonOther,
	};
	mtpRequestId _requestId = 0;

};
