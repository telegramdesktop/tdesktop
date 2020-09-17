/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "ui/layers/generic_box.h"
#include "mtproto/sender.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class InputField;
} // namespace Ui

class ReportBox final : public Ui::BoxContent {
public:
	ReportBox(QWidget*, not_null<PeerData*> peer);
	ReportBox(QWidget*, not_null<PeerData*> peer, MessageIdsList ids);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	enum class Reason {
		Spam,
		Violence,
		ChildAbuse,
		Pornography,
		Other,
	};
	void reasonChanged(Reason reason);
	void reasonResized();
	void updateMaxHeight();
	void report();

	void reportDone(const MTPBool &result);
	void reportFail(const RPCError &error);

	const not_null<PeerData*> _peer;
	MTP::Sender _api;
	std::optional<MessageIdsList> _ids;

	std::shared_ptr<Ui::RadioenumGroup<Reason>> _reasonGroup;
	object_ptr<Ui::Radioenum<Reason>> _reasonSpam = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonViolence = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonChildAbuse = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonPornography = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonOther = { nullptr };
	object_ptr<Ui::InputField> _reasonOtherText = { nullptr };

	mtpRequestId _requestId = 0;

};

void BlockSenderFromRepliesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	MessageIdsList ids);
