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
class InputField;
class FlatLabel;
class IconButton;
} // namespace Ui

class RateCallBox : public BoxContent, private MTP::Sender {
public:
	RateCallBox(QWidget*, uint64 callId, uint64 callAccessHash);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void updateMaxHeight();
	void ratingChanged(int value);
	void send();
	void commentResized();

	uint64 _callId = 0;
	uint64 _callAccessHash = 0;
	int _rating = 0;

	std::vector<object_ptr<Ui::IconButton>> _stars;
	object_ptr<Ui::InputField> _comment = { nullptr };

	mtpRequestId _requestId = 0;

};
