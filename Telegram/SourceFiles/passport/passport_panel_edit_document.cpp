/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_edit_document.h"

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

PanelEditDocument::PanelEditDocument(
	QWidget*,
	not_null<PanelController*> controller,
	Scheme scheme,
	const ValueMap &data,
	const ValueMap &scanData,
	std::vector<ScanInfo> &&files)
: _controller(controller)
, _scheme(std::move(scheme))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		langFactory(lng_passport_save_value),
		st::passportPanelSaveValue) {
	setupControls(data, &scanData, std::move(files));
}

PanelEditDocument::PanelEditDocument(
	QWidget*,
	not_null<PanelController*> controller,
	Scheme scheme,
	const ValueMap &data)
: _controller(controller)
, _scheme(std::move(scheme))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		langFactory(lng_passport_save_value),
		st::passportPanelSaveValue) {
	setupControls(data, nullptr, {});
}

void PanelEditDocument::setupControls(
		const ValueMap &data,
		const ValueMap *scanData,
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

not_null<Ui::RpWidget*> PanelEditDocument::setupContent(
		const ValueMap &data,
		const ValueMap *scanData,
		std::vector<ScanInfo> &&files) {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	if (scanData) {
		_editScans = inner->add(
			object_ptr<EditScans>(inner, _controller, std::move(files)));
	}

	inner->add(object_ptr<BoxContentDivider>(
		inner,
		st::passportFormDividerHeight));
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			_scheme.rowsHeader,
			Ui::FlatLabel::InitType::Simple,
			st::passportFormHeader),
		st::passportDetailsHeaderPadding);

	const auto valueOrEmpty = [&](
			const ValueMap &values,
			const QString &key) {
		const auto &fields = values.fields;
		if (const auto i = fields.find(key); i != fields.end()) {
			return i->second;
		}
		return QString();
	};

	for (const auto &row : _scheme.rows) {
		auto fields = (row.type == Scheme::ValueType::Fields)
			? &data
			: scanData;
		if (!fields) {
			continue;
		}
		_details.push_back(inner->add(object_ptr<PanelDetailsRow>(
			inner,
			row.label,
			valueOrEmpty(*fields, row.key))));
	}

	return inner;
}

void PanelEditDocument::focusInEvent(QFocusEvent *e) {
	for (const auto row : _details) {
		if (row->setFocusFast()) {
			return;
		}
	}
}

void PanelEditDocument::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelEditDocument::updateControlsGeometry() {
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

void PanelEditDocument::save() {
	Expects(_details.size() == _scheme.rows.size());

	auto data = ValueMap();
	auto scanData = ValueMap();
	for (auto i = 0, count = int(_details.size()); i != count; ++i) {
		const auto &row = _scheme.rows[i];
		auto &fields = (row.type == Scheme::ValueType::Fields)
			? data
			: scanData;
		fields.fields[row.key] = _details[i]->getValue();
	}
	_controller->saveScope(std::move(data), std::move(scanData));
}

} // namespace Passport
