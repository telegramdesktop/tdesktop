/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_edit_identity.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_details_row.h"
#include "passport/passport_panel_edit_scans.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {

PanelEditIdentity::PanelEditIdentity(
	QWidget*,
	not_null<PanelController*> controller,
	const ValueMap &data,
	const ValueMap &scanData,
	std::vector<ScanInfo> &&files)
: _controller(controller)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		langFactory(lng_passport_save_value),
		st::passportPanelSaveValue) {
	setupControls(data, scanData, std::move(files));
}

void PanelEditIdentity::setupControls(
		const ValueMap &data,
		const ValueMap &scanData,
		std::vector<ScanInfo> &&files) {
	const auto inner = setupContent(data, scanData, std::move(files));

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_done->addClickHandler([=] {
		crl::on_main(this, [=] {
			save();
		});
	});
}

not_null<Ui::RpWidget*> PanelEditIdentity::setupContent(
		const ValueMap &data,
		const ValueMap &scanData,
		std::vector<ScanInfo> &&files) {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	_editScans = inner->add(
		object_ptr<EditScans>(inner, _controller, std::move(files)));

	inner->add(object_ptr<BoxContentDivider>(
		inner,
		st::passportFormDividerHeight));
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			lang(lng_passport_personal_details),
			Ui::FlatLabel::InitType::Simple,
			st::passportFormHeader),
		st::passportDetailsHeaderPadding);

	const auto valueOrEmpty = [&](const QString &key) {
		if (const auto i = data.fields.find(key); i != data.fields.end()) {
			return i->second;
		}
		return QString();
	};

	_firstName = inner->add(object_ptr<PanelDetailsRow>(
		inner,
		lang(lng_passport_first_name),
		valueOrEmpty("first_name")))->field();
	_lastName = inner->add(object_ptr<PanelDetailsRow>(
		inner,
		lang(lng_passport_last_name),
		valueOrEmpty("last_name")))->field();

	return inner;
}

void PanelEditIdentity::focusInEvent(QFocusEvent *e) {
	_firstName->setFocusFast();
}

void PanelEditIdentity::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelEditIdentity::updateControlsGeometry() {
	const auto submitTop = height() - _done->height();
	_scroll->setGeometry(0, 0, width(), submitTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_done->resizeToWidth(width());
	_done->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

void PanelEditIdentity::save() {
	auto data = ValueMap();
	data.fields["first_name"] = _firstName->getLastText();
	data.fields["last_name"] = _lastName->getLastText();
	auto scanData = ValueMap();
	_controller->saveScope(std::move(data), std::move(scanData));
}

} // namespace Passport
