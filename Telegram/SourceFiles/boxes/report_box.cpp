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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/report_box.h"

#include "lang.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "boxes/confirmbox.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "mainwindow.h"

ReportBox::ReportBox(QWidget*, PeerData *peer) : _peer(peer)
, _reasonSpam(this, qsl("report_reason"), ReasonSpam, lang(lng_report_reason_spam), true, st::defaultBoxCheckbox)
, _reasonViolence(this, qsl("report_reason"), ReasonViolence, lang(lng_report_reason_violence), false, st::defaultBoxCheckbox)
, _reasonPornography(this, qsl("report_reason"), ReasonPornography, lang(lng_report_reason_pornography), false, st::defaultBoxCheckbox)
, _reasonOther(this, qsl("report_reason"), ReasonOther, lang(lng_report_reason_other), false, st::defaultBoxCheckbox) {
}

void ReportBox::prepare() {
	setTitle(lang(_peer->isUser() ? lng_report_bot_title : (_peer->isMegagroup() ? lng_report_group_title : lng_report_title)));

	addButton(lang(lng_report_button), [this] { onReport(); });
	addButton(lang(lng_cancel), [this] { closeBox(); });

	connect(_reasonSpam, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_reasonViolence, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_reasonPornography, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_reasonOther, SIGNAL(changed()), this, SLOT(onChange()));

	updateMaxHeight();
}

void ReportBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_reasonSpam->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxOptionListPadding.top());
	_reasonViolence->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonSpam->bottomNoMargins() + st::boxOptionListSkip);
	_reasonPornography->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonViolence->bottomNoMargins() + st::boxOptionListSkip);
	_reasonOther->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonPornography->bottomNoMargins() + st::boxOptionListSkip);

	if (_reasonOtherText) {
		_reasonOtherText->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() - st::defaultInputField.textMargins.left(), _reasonOther->bottomNoMargins() + st::newGroupDescriptionPadding.top());
	}
}

void ReportBox::onChange() {
	if (_reasonOther->checked()) {
		if (!_reasonOtherText) {
			_reasonOtherText.create(this, st::profileReportReasonOther, lang(lng_report_reason_description));
			_reasonOtherText->show();
			_reasonOtherText->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
			_reasonOtherText->setMaxLength(MaxPhotoCaption);
			_reasonOtherText->resize(width() - (st::boxPadding.left() + st::boxOptionListPadding.left() + st::boxPadding.right()), _reasonOtherText->height());

			updateMaxHeight();
			connect(_reasonOtherText, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
			connect(_reasonOtherText, SIGNAL(submitted(bool)), this, SLOT(onReport()));
			connect(_reasonOtherText, SIGNAL(cancelled()), this, SLOT(onClose()));
		}
		_reasonOtherText->setFocusFast();
	} else if (_reasonOtherText) {
		_reasonOtherText.destroy();
		updateMaxHeight();
	}
}

void ReportBox::setInnerFocus() {
	if (_reasonOtherText) {
		_reasonOtherText->setFocusFast();
	} else {
		setFocus();
	}
}

void ReportBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

void ReportBox::onReport() {
	if (_requestId) return;

	if (_reasonOtherText && _reasonOtherText->getLastText().trimmed().isEmpty()) {
		_reasonOtherText->showError();
		return;
	}

	auto getReason = [this]() {
		if (_reasonViolence->checked()) {
			return MTP_inputReportReasonViolence();
		} else if (_reasonPornography->checked()) {
			return MTP_inputReportReasonPornography();
		} else if (_reasonOtherText) {
			return MTP_inputReportReasonOther(MTP_string(_reasonOtherText->getLastText()));
		} else {
			return MTP_inputReportReasonSpam();
		}
	};
	_requestId = MTP::send(MTPaccount_ReportPeer(_peer->input, getReason()), rpcDone(&ReportBox::reportDone), rpcFail(&ReportBox::reportFail));
}

void ReportBox::reportDone(const MTPBool &result) {
	_requestId = 0;
	Ui::show(Box<InformBox>(lang(lng_report_thanks)));
}

bool ReportBox::reportFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_requestId = 0;
	if (_reasonOtherText) {
		_reasonOtherText->showError();
	}
	return true;
}

void ReportBox::updateMaxHeight() {
	auto newHeight = st::boxOptionListPadding.top() + 4 * _reasonSpam->heightNoMargins() + 3 * st::boxOptionListSkip + st::boxOptionListPadding.bottom();
	if (_reasonOtherText) {
		newHeight += st::newGroupDescriptionPadding.top() + _reasonOtherText->height() + st::newGroupDescriptionPadding.bottom();
	}
	setDimensions(st::boxWidth, newHeight);
}
