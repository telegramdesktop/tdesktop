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
#include "ui/toast/toast.h"
#include "mainwindow.h"

ReportBox::ReportBox(QWidget*, not_null<PeerData*> peer)
: _peer(peer) {
}

ReportBox::ReportBox(QWidget*, not_null<PeerData*> peer, MessageIdsList ids)
: _peer(peer)
, _ids(std::move(ids)) {
}

void ReportBox::prepare() {
	setTitle(langFactory([&] {
		if (_ids) {
			return lng_report_message_title;
		} else if (_peer->isUser()) {
			return lng_report_bot_title;
		} else if (_peer->isMegagroup()) {
			return lng_report_group_title;
		} else {
			return lng_report_title;
		}
	}()));

	addButton(langFactory(lng_report_button), [this] { onReport(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_reasonGroup = std::make_shared<Ui::RadioenumGroup<Reason>>(
		Reason::Spam);
	const auto createButton = [&](
			object_ptr<Ui::Radioenum<Reason>> &button,
			Reason reason,
			LangKey key) {
		button.create(
			this,
			_reasonGroup,
			reason,
			lang(key),
			st::defaultBoxCheckbox);
	};
	createButton(_reasonSpam, Reason::Spam, lng_report_reason_spam);
	createButton(_reasonViolence, Reason::Violence, lng_report_reason_violence);
	createButton(_reasonPornography, Reason::Pornography, lng_report_reason_pornography);
	createButton(_reasonOther, Reason::Other, lng_report_reason_other);
	_reasonGroup->setChangedCallback([=](Reason value) {
		reasonChanged(value);
	});

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
			_reasonOtherText.create(
				this,
				st::profileReportReasonOther,
				Ui::InputField::Mode::MultiLine,
				langFactory(lng_report_reason_description));
			_reasonOtherText->show();
			_reasonOtherText->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
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

	const auto reason = [&] {
		switch (_reasonGroup->value()) {
		case Reason::Spam: return MTP_inputReportReasonSpam();
		case Reason::Violence: return MTP_inputReportReasonViolence();
		case Reason::Pornography: return MTP_inputReportReasonPornography();
		case Reason::Other: return MTP_inputReportReasonOther(MTP_string(_reasonOtherText->getLastText()));
		}
		Unexpected("Bad reason group value.");
	}();
	if (_ids) {
		auto ids = QVector<MTPint>();
		for (const auto &fullId : *_ids) {
			ids.push_back(MTP_int(fullId.msg));
		}
		_requestId = MTP::send(
			MTPmessages_Report(
				_peer->input,
				MTP_vector<MTPint>(ids),
				reason),
			rpcDone(&ReportBox::reportDone),
			rpcFail(&ReportBox::reportFail));
	} else {
		_requestId = MTP::send(
			MTPaccount_ReportPeer(_peer->input, reason),
			rpcDone(&ReportBox::reportDone),
			rpcFail(&ReportBox::reportFail));
	}
}

void ReportBox::reportDone(const MTPBool &result) {
	_requestId = 0;
	Ui::Toast::Show(lang(lng_report_thanks));
	closeBox();
}

bool ReportBox::reportFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}

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
