/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"

namespace Ui {
class Checkbox;
class FlatLabel;
} // namespace Ui

class PinMessageBox final : public Ui::BoxContent {
public:
	PinMessageBox(QWidget*, not_null<PeerData*> peer, MsgId msgId);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void pinMessage();

	const not_null<PeerData*> _peer;
	MTP::Sender _api;
	MsgId _msgId = 0;
	bool _pinningOld = false;

	object_ptr<Ui::FlatLabel> _text;
	object_ptr<Ui::Checkbox> _notify = { nullptr };
	object_ptr<Ui::Checkbox> _pinForPeer = { nullptr };
	QPointer<Ui::Checkbox> _checkbox;

	mtpRequestId _requestId = 0;

};
