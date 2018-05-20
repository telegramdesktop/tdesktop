/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class InputField;
} // namespace Ui

class ReportBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	ReportBox(QWidget*, not_null<PeerData*> peer);
	ReportBox(QWidget*, not_null<PeerData*> peer, MessageIdsList ids);

private slots:
	void onReport();
	void onReasonResized();
	void onClose() {
		closeBox();
	}

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	enum class Reason {
		Spam,
		Violence,
		Pornography,
		Other,
	};
	void reasonChanged(Reason reason);
	void updateMaxHeight();

	void reportDone(const MTPBool &result);
	bool reportFail(const RPCError &error);

	not_null<PeerData*> _peer;
	base::optional<MessageIdsList> _ids;

	std::shared_ptr<Ui::RadioenumGroup<Reason>> _reasonGroup;
	object_ptr<Ui::Radioenum<Reason>> _reasonSpam = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonViolence = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonPornography = { nullptr };
	object_ptr<Ui::Radioenum<Reason>> _reasonOther = { nullptr };
	object_ptr<Ui::InputField> _reasonOtherText = { nullptr };

	mtpRequestId _requestId = 0;

};
