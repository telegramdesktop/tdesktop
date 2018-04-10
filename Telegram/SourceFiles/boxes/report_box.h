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
class InputArea;
} // namespace Ui

class ReportBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	ReportBox(QWidget*, PeerData *peer);

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

	PeerData *_peer;

	std::shared_ptr<Ui::RadioenumGroup<Reason>> _reasonGroup;
	object_ptr<Ui::Radioenum<Reason>> _reasonSpam;
	object_ptr<Ui::Radioenum<Reason>> _reasonViolence;
	object_ptr<Ui::Radioenum<Reason>> _reasonPornography;
	object_ptr<Ui::Radioenum<Reason>> _reasonOther;
	object_ptr<Ui::InputArea> _reasonOtherText = { nullptr };

	mtpRequestId _requestId = 0;

};
