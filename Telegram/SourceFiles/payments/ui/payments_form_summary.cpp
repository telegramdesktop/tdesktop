/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_form_summary.h"

#include "payments/ui/payments_panel_delegate.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;

FormSummary::FormSummary(
	QWidget *parent,
	const Invoice &invoice,
	not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _submit(
		this,
		tr::lng_payments_pay_amount(lt_amount, rpl::single(QString("much"))),
		st::passportPanelAuthorize) {
	setupControls();
}

void FormSummary::setupControls() {
	const auto inner = setupContent();

	_submit->addClickHandler([=] {
		_delegate->panelSubmit();
	});

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		inner->heightValue(),
		_1 + _2 < _3));
}

not_null<Ui::RpWidget*> FormSummary::setupContent() {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	return inner;
}

void FormSummary::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void FormSummary::updateControlsGeometry() {
	const auto submitTop = height() - _submit->height();
	_scroll->setGeometry(0, 0, width(), submitTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_submit->setFullWidth(width());
	_submit->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

} // namespace Payments::Ui
