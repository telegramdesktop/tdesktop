/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/rate_call_box.h"

#include "lang/lang_keys.h"
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
, _callAccessHash(callAccessHash) {
}

void RateCallBox::prepare() {
	setTitle(langFactory(lng_call_rate_label));
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars.push_back(object_ptr<Ui::IconButton>(this, st::callRatingStar));
		_stars.back()->setClickedCallback([this, value = i + 1] { ratingChanged(value); });
		_stars.back()->show();
	}

	updateMaxHeight();
}

void RateCallBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	auto starsWidth = (_stars.size() * st::callRatingStar.width);
	auto starLeft = (width() - starsWidth) / 2;
	auto starTop = st::callRatingStarTop;
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
		addButton(langFactory(lng_send_button), [this] { onSend(); });
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	}
	_rating = value;

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars[i]->setIconOverride((i < value) ? &st::callRatingStarFilled : nullptr);
		_stars[i]->setRippleColorOverride((i < value) ? &st::lightButtonBgOver : nullptr);
	}
	if (value < kMaxRating) {
		if (!_comment) {
			_comment.create(
				this,
				st::callRatingComment,
				Ui::InputField::Mode::MultiLine,
				langFactory(lng_call_rate_comment));
			_comment->show();
			_comment->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
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
	auto newHeight = st::callRatingPadding.top() + st::callRatingStarTop + _stars.back()->heightNoMargins() + st::callRatingPadding.bottom();
	if (_comment) {
		newHeight += st::callRatingCommentTop + _comment->height();
	}
	setDimensions(st::boxWideWidth, newHeight);
}
