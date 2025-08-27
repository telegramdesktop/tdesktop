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
#include "ui/controls/userpic_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/scroll_content_shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
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

	SetupShadowsToScrollContent(this, _scroll, inner->heightValue());
}

not_null<Ui::RpWidget*> PanelForm::setupContent() {
	const auto bot = _controller->bot();

	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	_userpic = inner->add(
		object_ptr<Ui::UserpicButton>(
			inner,
			bot,
			st::passportFormUserpic),
		st::passportFormUserpicPadding,
		style::al_top);

	_about1 = inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_passport_request1(tr::now, lt_bot, bot->name()),
			st::passportPasswordLabelBold),
		st::passportFormAbout1Padding,
		style::al_top);

	_about2 = inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_passport_request2(tr::now),
			st::passportPasswordLabel),
		st::passportFormAbout2Padding,
		style::al_top);

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
	auto policyLink = tr::lng_passport_policy(
		lt_bot,
		rpl::single(bot->name())
	) | Ui::Text::ToLink(
		policyUrl
	) | rpl::map([=](TextWithEntities &&text) {
		return Ui::Text::Wrapped(std::move(text), EntityType::Bold);
	});
	auto text = policyUrl.isEmpty()
		? tr::lng_passport_allow(
			lt_bot,
			rpl::single('@' + bot->username())
		) | Ui::Text::ToWithEntities()
		: tr::lng_passport_accept_allow(
			lt_policy,
			std::move(policyLink),
			lt_bot,
			rpl::single('@' + bot->username()) | Ui::Text::ToWithEntities(),
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
	_submit->setFullWidth(width());
	_submit->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

} // namespace Passport
