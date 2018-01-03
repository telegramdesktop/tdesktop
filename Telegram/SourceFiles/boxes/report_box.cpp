/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/report_box.h"

#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "mainwindow.h"

ReportBox::ReportBox(QWidget*, PeerData *peer) : _peer(peer)
, _reasonGroup(std::make_shared<Ui::RadioenumGroup<Reason>>(Reason::Spam))
, _reasonSpam(this, _reasonGroup, Reason::Spam, lang(lng_report_reason_spam), st::defaultBoxCheckbox)
, _reasonViolence(this, _reasonGroup, Reason::Violence, lang(lng_report_reason_violence), st::defaultBoxCheckbox)
, _reasonPornography(this, _reasonGroup, Reason::Pornography, lang(lng_report_reason_pornography), st::defaultBoxCheckbox)
, _reasonOther(this, _reasonGroup, Reason::Other, lang(lng_report_reason_other), st::defaultBoxCheckbox) {
}

void ReportBox::prepare() {
	setTitle(langFactory(_peer->isUser() ? lng_report_bot_title : (_peer->isMegagroup() ? lng_report_group_title : lng_report_title)));

	addButton(langFactory(lng_report_button), [this] { onReport(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_reasonGroup->setChangedCallback([this](Reason value) { reasonChanged(value); });

	updateMaxHeight();
}

void ReportBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_reasonSpam->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxOptionListPadding.top() + _reasonSpam->getMargins().top());
	_reasonViolence->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonSpam->bottomNoMargins() + st::boxOptionListSkip);
	_reasonPornography->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonViolence->bottomNoMargins() + st::boxOptionListSkip);
	_reasonOther->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _reasonPornography->bottomNoMargins() + st::boxOptionListSkip);

	if (_reasonOtherText) {
		_reasonOtherText->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() - st::defaultInputField.textMargins.left(), _reasonOther->bottomNoMargins() + st::newGroupDescriptionPadding.top());
	}
}

void ReportBox::reasonChanged(Reason reason) {
	if (reason == Reason::Other) {
		if (!_reasonOtherText) {
			_reasonOtherText.create(this, st::profileReportReasonOther, langFactory(lng_report_reason_description));
			_reasonOtherText->show();
			_reasonOtherText->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
			_reasonOtherText->setMaxLength(MaxPhotoCaption);
			_reasonOtherText->resize(width() - (st::boxPadding.left() + st::boxOptionListPadding.left() + st::boxPadding.right()), _reasonOtherText->height());

			updateMaxHeight();
			connect(_reasonOtherText, SIGNAL(resized()), this, SLOT(onReasonResized()));
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

void ReportBox::onReasonResized() {
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
		switch (_reasonGroup->value()) {
		case Reason::Spam: return MTP_inputReportReasonSpam();
		case Reason::Violence: return MTP_inputReportReasonViolence();
		case Reason::Pornography: return MTP_inputReportReasonPornography();
		case Reason::Other: return MTP_inputReportReasonOther(MTP_string(_reasonOtherText->getLastText()));
		}
		Unexpected("Bad reason group value.");
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
	auto newHeight = st::boxOptionListPadding.top() + _reasonSpam->getMargins().top() + 4 * _reasonSpam->heightNoMargins() + 3 * st::boxOptionListSkip + _reasonSpam->getMargins().bottom() + st::boxOptionListPadding.bottom();
	if (_reasonOtherText) {
		newHeight += st::newGroupDescriptionPadding.top() + _reasonOtherText->height() + st::newGroupDescriptionPadding.bottom();
	}
	setDimensions(st::boxWidth, newHeight);
}
