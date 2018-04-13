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
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "boxes/abstract_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {
namespace {

class RequestTypeBox : public BoxContent {
public:
	RequestTypeBox(
		QWidget*,
		const QString &title,
		const QString &about,
		std::vector<QString> labels,
		base::lambda<void(int index)> submit);

protected:
	void prepare() override;

private:
	void setupControls(
		const QString &about,
		std::vector<QString> labels,
		base::lambda<void(int index)> submit);

	QString _title;
	base::lambda<void()> _submit;
	int _height = 0;

};

RequestTypeBox::RequestTypeBox(
	QWidget*,
	const QString &title,
	const QString &about,
	std::vector<QString> labels,
	base::lambda<void(int index)> submit)
: _title(title) {
	setupControls(about, std::move(labels), submit);
}

void RequestTypeBox::prepare() {
	setTitle([=] { return _title; });
	addButton(langFactory(lng_passport_upload_document), _submit);
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
	setDimensions(st::boxWidth, _height);
}

void RequestTypeBox::setupControls(
		const QString &about,
		std::vector<QString> labels,
		base::lambda<void(int index)> submit) {
	const auto header = Ui::CreateChild<Ui::FlatLabel>(
		this,
		lang(lng_passport_document_type),
		Ui::FlatLabel::InitType::Simple,
		st::passportFormLabel);

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(0);
	auto buttons = std::vector<QPointer<Ui::Radiobutton>>();
	auto index = 0;
	for (const auto &label : labels) {
		buttons.push_back(Ui::CreateChild<Ui::Radiobutton>(
			this,
			group,
			index++,
			label,
			st::defaultBoxCheckbox));
	}

	const auto description = Ui::CreateChild<Ui::FlatLabel>(
		this,
		about,
		Ui::FlatLabel::InitType::Simple,
		st::passportFormLabel);

	auto y = 0;
	const auto innerWidth = st::boxWidth
		- st::boxPadding.left()
		- st::boxPadding.right();
	header->resizeToWidth(innerWidth);
	header->moveToLeft(st::boxPadding.left(), y);
	y += header->height() + st::passportRequestTypeSkip;
	for (const auto &button : buttons) {
		button->resizeToNaturalWidth(innerWidth);
		button->moveToLeft(st::boxPadding.left(), y);
		y += button->heightNoMargins() + st::passportRequestTypeSkip;
	}
	description->resizeToWidth(innerWidth);
	description->moveToLeft(st::boxPadding.left(), y);
	y += description->height() + st::passportRequestTypeSkip;
	_height = y;

	_submit = [=] {
		const auto value = group->hasValue() ? group->value() : -1;
		if (value >= 0) {
			submit(value);
		}
	};
}

} // namespace

struct PanelEditDocument::Result {
	ValueMap data;
	ValueMap filesData;
};

PanelEditDocument::PanelEditDocument(
	QWidget*,
	not_null<PanelController*> controller,
	Scheme scheme,
	const ValueMap &data,
	const ValueMap &scanData,
	std::vector<ScanInfo> &&files,
	std::unique_ptr<ScanInfo> &&selfie)
: _controller(controller)
, _scheme(std::move(scheme))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		langFactory(lng_passport_save_value),
		st::passportPanelSaveValue) {
	setupControls(data, &scanData, std::move(files), std::move(selfie));
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
	setupControls(data, nullptr, {}, nullptr);
}

void PanelEditDocument::setupControls(
		const ValueMap &data,
		const ValueMap *scanData,
		std::vector<ScanInfo> &&files,
		std::unique_ptr<ScanInfo> &&selfie) {
	const auto inner = setupContent(
		data,
		scanData,
		std::move(files),
		std::move(selfie));

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
		std::vector<ScanInfo> &&files,
		std::unique_ptr<ScanInfo> &&selfie) {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	if (scanData) {
		_editScans = inner->add(
			object_ptr<EditScans>(
				inner,
				_controller,
				_scheme.scansHeader,
				std::move(files),
				std::move(selfie)));
	} else {
		inner->add(object_ptr<BoxContentDivider>(
			inner,
			st::passportFormDividerHeight));
	}

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

	for (auto i = 0, count = int(_scheme.rows.size()); i != count; ++i) {
		const auto &row = _scheme.rows[i];
		auto fields = (row.valueClass == Scheme::ValueClass::Fields)
			? &data
			: scanData;
		if (!fields) {
			continue;
		}
		_details.emplace(i, inner->add(PanelDetailsRow::Create(
			inner,
			row.inputType,
			_controller,
			row.label,
			valueOrEmpty(*fields, row.key),
			QString())));
	}

	inner->add(
		object_ptr<Ui::FixedHeightWidget>(inner, st::passportDetailsSkip));

	return inner;
}

void PanelEditDocument::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		for (const auto [index, row] : _details) {
			if (row->setFocusFast()) {
				return;
			}
		}
	});
}

void PanelEditDocument::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

bool PanelEditDocument::hasUnsavedChanges() const {
	const auto result = collect();
	return _controller->editScopeChanged(result.data, result.filesData);
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

PanelEditDocument::Result PanelEditDocument::collect() const {
	auto result = Result();
	for (const auto [i, field] : _details) {
		const auto &row = _scheme.rows[i];
		auto &fields = (row.valueClass == Scheme::ValueClass::Fields)
			? result.data
			: result.filesData;
		fields.fields[row.key] = field->valueCurrent();
	}
	return result;
}

bool PanelEditDocument::validate() {
	auto first = QPointer<PanelDetailsRow>();
	for (const auto [i, field] : base::reversed(_details)) {
		const auto &row = _scheme.rows[i];
		if (row.validate && !row.validate(field->valueCurrent())) {
			field->showError(QString());
			first = field;
		}
	}
	if (!first) {
		return true;
	}
	const auto firsttop = first->mapToGlobal(QPoint(0, 0));
	const auto scrolltop = _scroll->mapToGlobal(QPoint(0, 0));
	const auto scrolldelta = firsttop.y() - scrolltop.y();
	_scroll->scrollToY(_scroll->scrollTop() + scrolldelta);
	return false;
}

void PanelEditDocument::save() {
	if (!validate()) {
		return;
	}
	auto result = collect();
	_controller->saveScope(
		std::move(result.data),
		std::move(result.filesData));
}

object_ptr<BoxContent> RequestIdentityType(
		base::lambda<void(int index)> submit,
		std::vector<QString> labels) {
	return Box<RequestTypeBox>(
		lang(lng_passport_identity_title),
		lang(lng_passport_identity_about),
		std::move(labels),
		submit);
}

object_ptr<BoxContent> RequestAddressType(
		base::lambda<void(int index)> submit,
		std::vector<QString> labels) {
	return Box<RequestTypeBox>(
		lang(lng_passport_address_title),
		lang(lng_passport_address_about),
		std::move(labels),
		submit);
}

} // namespace Passport
