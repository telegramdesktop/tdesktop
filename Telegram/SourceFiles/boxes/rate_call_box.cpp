/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/rate_call_box.h"

#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "apiwrap.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"

namespace {

constexpr auto kMaxRating = 5;
constexpr auto kRateCallCommentLengthMax = 200;

} // namespace

RateCallBox::RateCallBox(
	QWidget*,
	not_null<Main::Session*> session,
	uint64 callId,
	uint64 callAccessHash)
: _session(session)
, _api(&_session->mtp())
, _callId(callId)
, _callAccessHash(callAccessHash) {
}

void RateCallBox::prepare() {
	setTitle(tr::lng_call_rate_label());
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars.emplace_back(this, st::callRatingStar);
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
		addButton(tr::lng_send_button(), [this] { send(); });
		addButton(tr::lng_cancel(), [this] { closeBox(); });
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
				tr::lng_call_rate_comment());
			_comment->show();
			_comment->setSubmitSettings(Core::App().settings().sendSubmitWay());
			_comment->setMaxLength(kRateCallCommentLengthMax);
			_comment->resize(width() - (st::callRatingPadding.left() + st::callRatingPadding.right()), _comment->height());

			updateMaxHeight();
			connect(_comment, &Ui::InputField::resized, [=] { commentResized(); });
			connect(_comment, &Ui::InputField::submitted, [=] { send(); });
			connect(_comment, &Ui::InputField::cancelled, [=] { closeBox(); });
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

void RateCallBox::commentResized() {
	updateMaxHeight();
	update();
}

void RateCallBox::send() {
	Expects(_rating > 0 && _rating <= kMaxRating);

	if (_requestId) {
		return;
	}
	auto comment = _comment ? _comment->getLastText().trimmed() : QString();
	_requestId = _api.request(MTPphone_SetCallRating(
		MTP_flags(0),
		MTP_inputPhoneCall(MTP_long(_callId), MTP_long(_callAccessHash)),
		MTP_int(_rating),
		MTP_string(comment)
	)).done([=](const MTPUpdates &updates) {
		_session->api().applyUpdates(updates);
		closeBox();
	}).fail([=](const MTP::Error &error) { closeBox(); }).send();
}

void RateCallBox::updateMaxHeight() {
	auto newHeight = st::callRatingPadding.top() + st::callRatingStarTop + _stars.back()->heightNoMargins() + st::callRatingPadding.bottom();
	if (_comment) {
		newHeight += st::callRatingCommentTop + _comment->height();
	}
	setDimensions(st::boxWideWidth, newHeight);
}
