/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/rate_call_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"

namespace Ui {

namespace {

constexpr auto kMaxRating = 5;
constexpr auto kRateCallCommentLengthMax = 200;

} // namespace

RateCallBox::RateCallBox(QWidget*, InputSubmitSettings sendWay)
: _sendWay(sendWay) {
}

void RateCallBox::prepare() {
	setTitle(tr::lng_call_rate_label());
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars.emplace_back(this, st::callRatingStar);
		_stars.back()->setClickedCallback([this, value = i + 1] {
			ratingChanged(value);
		});
		_stars.back()->show();
	}

	updateMaxHeight();
}

void RateCallBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	const auto starsWidth = (_stars.size() * st::callRatingStar.width);
	auto starLeft = (width() - starsWidth) / 2;
	const auto starTop = st::callRatingStarTop;
	for (auto &star : _stars) {
		star->moveToLeft(starLeft, starTop);
		starLeft += star->width();
	}
	if (_comment) {
		_comment->moveToLeft(
			st::callRatingPadding.left(),
			_stars.back()->bottomNoMargins() + st::callRatingCommentTop);
	}
}

void RateCallBox::ratingChanged(int value) {
	Expects(value > 0 && value <= kMaxRating);
	if (!_rating) {
		clearButtons();
		addButton(tr::lng_send_button(), [=] { send(); });
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}
	_rating = value;

	for (auto i = 0; i < kMaxRating; ++i) {
		_stars[i]->setIconOverride((i < value)
			? &st::callRatingStarFilled
			: nullptr);
		_stars[i]->setRippleColorOverride((i < value)
			? &st::lightButtonBgOver
			: nullptr);
	}
	if (value < kMaxRating) {
		if (!_comment) {
			_comment.create(
				this,
				st::callRatingComment,
				Ui::InputField::Mode::MultiLine,
				tr::lng_call_rate_comment());
			_comment->show();
			_comment->setSubmitSettings(_sendWay);
			_comment->setMaxLength(kRateCallCommentLengthMax);
			_comment->resize(
				width()
					- st::callRatingPadding.left()
					- st::callRatingPadding.right(),
				_comment->height());

			updateMaxHeight();
			_comment->heightChanges(
			) | rpl::start_with_next([=] {
				commentResized();
			}, _comment->lifetime());
			_comment->submits(
			) | rpl::start_with_next([=] { send(); }, _comment->lifetime());
			_comment->cancelled(
			) | rpl::start_with_next([=] {
				closeBox();
			}, _comment->lifetime());
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
		BoxContent::setInnerFocus();
	}
}

void RateCallBox::commentResized() {
	updateMaxHeight();
	update();
}

void RateCallBox::send() {
	Expects(_rating > 0 && _rating <= kMaxRating);
	_sends.fire({
		.rating = _rating,
		.comment = _comment ? _comment->getLastText().trimmed() : QString(),
	});
}

void RateCallBox::updateMaxHeight() {
	auto newHeight = st::callRatingPadding.top()
		+ st::callRatingStarTop
		+ _stars.back()->heightNoMargins()
		+ st::callRatingPadding.bottom();
	if (_comment) {
		newHeight += st::callRatingCommentTop + _comment->height();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

rpl::producer<RateCallBox::Result> RateCallBox::sends() const {
	return _sends.events();
}

} // namespace Ui
