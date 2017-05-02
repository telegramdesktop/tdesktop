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
#include "boxes/rate_call_box.h"

#include "lang.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "mainwindow.h"
#include "mainwidget.h"

namespace {

constexpr auto kMaxRating = 5;

} // namespace

RateCallBox::RateCallBox(QWidget*, uint64 callId, uint64 callAccessHash)
: _callId(callId)
, _callAccessHash(callAccessHash)
, _label(this, lang(lng_call_rate_label), Ui::FlatLabel::InitType::Simple, st::boxLabel) {
}

void RateCallBox::prepare() {
	addButton(lang(lng_cancel), [this] { closeBox(); });

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars.push_back(object_ptr<Ui::IconButton>(this, st::callRatingStar));
		_stars.back()->setClickedCallback([this, value = i + 1] { ratingChanged(value); });
		_stars.back()->show();
	}

	updateMaxHeight();
}

void RateCallBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_label->moveToLeft(st::callRatingPadding.left(), st::callRatingPadding.top());
	auto starLeft = st::callRatingPadding.left() + st::callRatingStarLeft;
	auto starTop = _label->bottomNoMargins() + st::callRatingStarTop;
	for (auto &star : _stars) {
		star->moveToLeft(starLeft, starTop);
		starLeft += star->width();
	}
	if (_comment) {
		_comment->moveToLeft(st::callRatingPadding.left(), _stars.back()->bottomNoMargins() + st::callRatingCommentTop);
	}
}

void RateCallBox::ratingChanged(int value) {
	Expects(value > 0 && value <= kMaxRating);
	if (!_rating) {
		clearButtons();
		addButton(lang(lng_send_button), [this] { onSend(); });
		addButton(lang(lng_cancel), [this] { closeBox(); });
	}
	_rating = value;

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars[i]->setIconOverride((i < value) ? &st::callRatingStarFilled : nullptr);
		_stars[i]->setRippleColorOverride((i < value) ? &st::lightButtonBgOver : nullptr);
	}
	if (value < kMaxRating) {
		if (!_comment) {
			_comment.create(this, st::callRatingComment, lang(lng_call_rate_comment));
			_comment->show();
			_comment->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
			_comment->setMaxLength(MaxPhotoCaption);
			_comment->resize(width() - (st::callRatingPadding.left() + st::callRatingPadding.right()), _comment->height());

			updateMaxHeight();
			connect(_comment, SIGNAL(resized()), this, SLOT(onCommentResized()));
			connect(_comment, SIGNAL(submitted(bool)), this, SLOT(onSend()));
			connect(_comment, SIGNAL(cancelled()), this, SLOT(onClose()));
		}
		_comment->setFocusFast();
	} else if (_comment) {
		_comment.destroy();
		updateMaxHeight();
	}
}

void RateCallBox::setInnerFocus() {
	if (_comment) {
		_comment->setFocusFast();
	} else {
		setFocus();
	}
}

void RateCallBox::onCommentResized() {
	updateMaxHeight();
	update();
}

void RateCallBox::onSend() {
	Expects(_rating > 0 && _rating <= kMaxRating);
	if (_requestId) {
		return;
	}
	auto comment = _comment ? _comment->getLastText().trimmed() : QString();
	_requestId = request(MTPphone_SetCallRating(MTP_inputPhoneCall(MTP_long(_callId), MTP_long(_callAccessHash)), MTP_int(_rating), MTP_string(comment))).done([this](const MTPUpdates &updates) {
		App::main()->sentUpdatesReceived(updates);
		closeBox();
	}).fail([this](const RPCError &error) { closeBox(); }).send();
}

void RateCallBox::updateMaxHeight() {
	auto newHeight = st::callRatingPadding.top() + _label->heightNoMargins() + st::callRatingStarTop + _stars.back()->heightNoMargins() + st::callRatingPadding.bottom();
	if (_comment) {
		newHeight += st::callRatingCommentTop + _comment->height();
	}
	setDimensions(st::boxWidth, newHeight);
}
