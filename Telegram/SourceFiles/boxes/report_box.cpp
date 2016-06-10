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
#include "stdafx.h"
#include "report_box.h"

#include "lang.h"
#include "styles/style_profile.h"
#include "boxes/confirmbox.h"

ReportBox::ReportBox(ChannelData *channel) : AbstractBox(st::boxWidth)
, _channel(channel)
, _reasonSpam(this, qsl("report_reason"), ReasonSpam, lang(lng_report_reason_spam), true)
, _reasonViolence(this, qsl("report_reason"), ReasonViolence, lang(lng_report_reason_violence))
, _reasonPornography(this, qsl("report_reason"), ReasonPornography, lang(lng_report_reason_pornography))
, _reasonOther(this, qsl("report_reason"), ReasonOther, lang(lng_report_reason_other))
, _report(this, lang(lng_report_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	connect(_report, SIGNAL(clicked()), this, SLOT(onReport()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(_reasonSpam, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_reasonViolence, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_reasonPornography, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_reasonOther, SIGNAL(changed()), this, SLOT(onChange()));

	updateMaxHeight();

	prepare();
}

void ReportBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(_channel->isMegagroup() ? lng_report_group_title : lng_report_title));
}

void ReportBox::resizeEvent(QResizeEvent *e) {
	_reasonSpam->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxTitleHeight + st::boxOptionListPadding.top());
	_reasonViolence->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonSpam->y() + _reasonSpam->height() + st::boxOptionListPadding.top());
	_reasonPornography->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonViolence->y() + _reasonViolence->height() + st::boxOptionListPadding.top());
	_reasonOther->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonPornography->y() + _reasonPornography->height() + st::boxOptionListPadding.top());

	if (_reasonOtherText) {
		_reasonOtherText->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() - st::defaultInputField.textMargins.left(), _reasonOther->y() + _reasonOther->height() + st::newGroupDescriptionPadding.top());
	}

	_report->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _report->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _report->width() + st::boxButtonPadding.left(), _report->y());
}

void ReportBox::onChange() {
	if (_reasonOther->checked()) {
		if (!_reasonOtherText) {
			_reasonOtherText = new InputArea(this, st::profileReportReasonOther, lang(lng_report_reason_description));
			_reasonOtherText->show();
			_reasonOtherText->setCtrlEnterSubmit(CtrlEnterSubmitBoth);
			_reasonOtherText->setMaxLength(MaxPhotoCaption);
			_reasonOtherText->resize(width() - (st::boxPadding.left() + st::boxOptionListPadding.left() + st::boxPadding.right()), _reasonOtherText->height());

			updateMaxHeight();
			connect(_reasonOtherText, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
			connect(_reasonOtherText, SIGNAL(submitted(bool)), this, SLOT(onReport()));
			connect(_reasonOtherText, SIGNAL(cancelled()), this, SLOT(onClose()));
		}
	} else if (_reasonOtherText) {
		_reasonOtherText.destroy();
		updateMaxHeight();
	}
	setInnerFocus();
}

void ReportBox::setInnerFocus() {
	if (_reasonOtherText) {
		_reasonOtherText->setFocus();
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
	_requestId = MTP::send(MTPaccount_ReportPeer(_channel->input, getReason()), rpcDone(&ReportBox::reportDone), rpcFail(&ReportBox::reportFail));
}

void ReportBox::reportDone(const MTPBool &result) {
	_requestId = 0;
	Ui::showLayer(new InformBox(lang(lng_report_thanks)));
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
	int32 h = st::boxTitleHeight + 4 * (st::boxOptionListPadding.top() + _reasonSpam->height()) + st::boxButtonPadding.top() + _report->height() + st::boxButtonPadding.bottom();
	if (_reasonOtherText) {
		h += st::newGroupDescriptionPadding.top() + _reasonOtherText->height() + st::newGroupDescriptionPadding.bottom();
	}
	setMaxHeight(h);
}
