/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_form.h"

#include "passport/passport_panel_controller.h"
#include "passport/ui/passport_form_row.h"
#include "lang/lang_keys.h"
#include "boxes/abstract_box.h"
#include "core/click_handler_types.h"
#include "data/data_user.h"
#include "ui/effects/animations.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/special_buttons.h"
#include "styles/style_passport.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Passport {

PanelForm::PanelForm(
	QWidget *parent,
	not_null<PanelController*> controller)
: RpWidget(parent)
, _controller(controller)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _submit(
		this,
		tr::lng_passport_authorize(),
		st::passportPanelAuthorize) {
	setupControls();
}

void PanelForm::setupControls() {
	const auto inner = setupContent();

	_submit->addClickHandler([=] {
		_controller->submitForm();
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

not_null<Ui::RpWidget*> PanelForm::setupContent() {
	const auto bot = _controller->bot();

	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	const auto userpicWrap = inner->add(
		object_ptr<Ui::FixedHeightWidget>(
			inner,
			st::passportFormUserpic.size.height()),
		st::passportFormUserpicPadding);
	_userpic = Ui::AttachParentChild(
		userpicWrap,
		object_ptr<Ui::UserpicButton>(
			userpicWrap,
			bot,
			Ui::UserpicButton::Role::Custom,
			st::passportFormUserpic));
	userpicWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		_userpic->move((width - _userpic->width()) / 2, _userpic->y());
	}, _userpic->lifetime());

	_about1 = inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_passport_request1(tr::now, lt_bot, bot->name),
				st::passportPasswordLabelBold)),
		st::passportFormAbout1Padding)->entity();

	_about2 = inner->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_passport_request2(tr::now),
				st::passportPasswordLabel)),
		st::passportFormAbout2Padding)->entity();

	inner->add(object_ptr<Ui::BoxContentDivider>(
		inner,
		st::passportFormDividerHeight));
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_passport_header(tr::now),
			st::passportFormHeader),
		st::passportFormHeaderPadding);

	auto index = 0;
	_controller->fillRows([&](
			QString title,
			QString description,
			bool ready,
			bool error) {
		_rows.push_back(inner->add(object_ptr<Row>(this)));
		_rows.back()->addClickHandler([=] {
			_controller->editScope(index);
		});
		_rows.back()->updateContent(
			title,
			description,
			ready,
			error,
			anim::type::instant);
		++index;
	});
	_controller->refillRows(
	) | rpl::start_with_next([=] {
		auto index = 0;
		_controller->fillRows([&](
				QString title,
				QString description,
				bool ready,
				bool error) {
			Expects(index < _rows.size());

			_rows[index++]->updateContent(
				title,
				description,
				ready,
				error,
				anim::type::normal);
		});
	}, lifetime());
	const auto policyUrl = _controller->privacyPolicyUrl();
	auto text = policyUrl.isEmpty()
		? tr::lng_passport_allow(
			lt_bot,
			rpl::single('@' + bot->username)
		) | Ui::Text::ToWithEntities()
		: tr::lng_passport_accept_allow(
			lt_policy,
			tr::lng_passport_policy(
				lt_bot,
				rpl::single(bot->name)
			) | Ui::Text::ToLink(policyUrl),
			lt_bot,
			rpl::single('@' + bot->username) | Ui::Text::ToWithEntities(),
			Ui::Text::WithEntities);
	const auto policy = inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			std::move(text),
			st::passportFormPolicy),
		st::passportFormPolicyPadding);
	policy->setLinksTrusted();

	return inner;
}

void PanelForm::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelForm::updateControlsGeometry() {
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

} // namespace Passport
