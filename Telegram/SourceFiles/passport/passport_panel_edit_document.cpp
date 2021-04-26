/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_edit_document.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_edit_scans.h"
#include "passport/ui/passport_details_row.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "data/data_countries.h"
#include "data/data_user.h" // ->bot()->session()
#include "main/main_session.h" // ->session().user()
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "boxes/abstract_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"
#include "styles/style_passport.h"

namespace Passport {
namespace {

class RequestTypeBox : public Ui::BoxContent {
public:
	RequestTypeBox(
		QWidget*,
		rpl::producer<QString> title,
		const QString &about,
		std::vector<QString> labels,
		Fn<void(int index)> submit);

protected:
	void prepare() override;

private:
	void setupControls(
		const QString &about,
		std::vector<QString> labels,
		Fn<void(int index)> submit);

	rpl::producer<QString> _title;
	Fn<void()> _submit;
	int _height = 0;

};

class DeleteDocumentBox : public Ui::BoxContent {
public:
	DeleteDocumentBox(
		QWidget*,
		const QString &text,
		const QString &detailsCheckbox,
		Fn<void(bool withDetails)> submit);

protected:
	void prepare() override;

private:
	void setupControls(
		const QString &text,
		const QString &detailsCheckbox,
		Fn<void(bool withDetails)> submit);

	Fn<void()> _submit;
	int _height = 0;

};

RequestTypeBox::RequestTypeBox(
	QWidget*,
	rpl::producer<QString> title,
	const QString &about,
	std::vector<QString> labels,
	Fn<void(int index)> submit)
: _title(std::move(title)) {
	setupControls(about, std::move(labels), submit);
}

void RequestTypeBox::prepare() {
	setTitle(std::move(_title));
	addButton(tr::lng_passport_upload_document(), _submit);
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	setDimensions(st::boxWidth, _height);
}

void RequestTypeBox::setupControls(
		const QString &about,
		std::vector<QString> labels,
		Fn<void(int index)> submit) {
	const auto header = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_passport_document_type(tr::now),
		st::boxDividerLabel);

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(0);
	auto buttons = std::vector<QPointer<Ui::Radiobutton>>();
	auto index = 0;
	for (const auto &label : labels) {
		buttons.emplace_back(Ui::CreateChild<Ui::Radiobutton>(
			this,
			group,
			index++,
			label,
			st::defaultBoxCheckbox));
	}

	const auto description = Ui::CreateChild<Ui::FlatLabel>(
		this,
		about,
		st::boxDividerLabel);

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

DeleteDocumentBox::DeleteDocumentBox(
		QWidget*,
		const QString &text,
		const QString &detailsCheckbox,
		Fn<void(bool withDetails)> submit) {
	setupControls(text, detailsCheckbox, submit);
}

void DeleteDocumentBox::prepare() {
	addButton(tr::lng_box_delete(), _submit);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensions(st::boxWidth, _height);
}

void DeleteDocumentBox::setupControls(
		const QString &text,
		const QString &detailsCheckbox,
		Fn<void(bool withDetails)> submit) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		this,
		text,
		st::boxLabel);
	const auto details = !detailsCheckbox.isEmpty()
		? Ui::CreateChild<Ui::Checkbox>(
			this,
			detailsCheckbox,
			false,
			st::defaultBoxCheckbox)
		: nullptr;

	_height = st::boxPadding.top();
	const auto availableWidth = st::boxWidth
		- st::boxPadding.left()
		- st::boxPadding.right();
	label->resizeToWidth(availableWidth);
	label->moveToLeft(st::boxPadding.left(), _height);
	_height += label->height();

	if (details) {
		_height += st::boxPadding.bottom();
		details->moveToLeft(st::boxPadding.left(), _height);
		_height += details->heightNoMargins();
	}
	_height += st::boxPadding.bottom();

	_submit = [=] {
		submit(details ? details->checked() : false);
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
	const QString &error,
	const ValueMap &data,
	const QString &scansError,
	const ValueMap &scansData,
	ScanListData &&scans,
	std::optional<ScanListData> &&translations,
	std::map<FileType, ScanInfo> &&specialFiles)
: _controller(controller)
, _scheme(std::move(scheme))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		tr::lng_passport_save_value(),
		st::passportPanelSaveValue) {
	setupControls(
		&error,
		&data,
		&scansError,
		&scansData,
		std::move(scans),
		std::move(translations),
		std::move(specialFiles));
}

PanelEditDocument::PanelEditDocument(
	QWidget*,
	not_null<PanelController*> controller,
	Scheme scheme,
	const QString &scansError,
	const ValueMap &scansData,
	ScanListData &&scans,
	std::optional<ScanListData> &&translations,
	std::map<FileType, ScanInfo> &&specialFiles)
: _controller(controller)
, _scheme(std::move(scheme))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		tr::lng_passport_save_value(),
		st::passportPanelSaveValue) {
	setupControls(
		nullptr,
		nullptr,
		&scansError,
		&scansData,
		std::move(scans),
		std::move(translations),
		std::move(specialFiles));
}

PanelEditDocument::PanelEditDocument(
	QWidget*,
	not_null<PanelController*> controller,
	Scheme scheme,
	const QString &error,
	const ValueMap &data)
: _controller(controller)
, _scheme(std::move(scheme))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		tr::lng_passport_save_value(),
		st::passportPanelSaveValue) {
	setupControls(&error, &data, nullptr, nullptr, {}, {}, {});
}

void PanelEditDocument::setupControls(
		const QString *error,
		const ValueMap *data,
		const QString *scansError,
		const ValueMap *scansData,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations,
		std::map<FileType, ScanInfo> &&specialFiles) {
	const auto inner = setupContent(
		error,
		data,
		scansError,
		scansData,
		std::move(scans),
		std::move(translations),
		std::move(specialFiles));

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
		const QString *error,
		const ValueMap *data,
		const QString *scansError,
		const ValueMap *scansData,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations,
		std::map<FileType, ScanInfo> &&specialFiles) {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	if (!specialFiles.empty()) {
		_editScans = inner->add(
			object_ptr<EditScans>(
				inner,
				_controller,
				_scheme.scansHeader,
				*scansError,
				std::move(specialFiles),
				std::move(translations)));
	} else if (scansData) {
		_editScans = inner->add(
			object_ptr<EditScans>(
				inner,
				_controller,
				_scheme.scansHeader,
				*scansError,
				std::move(scans),
				std::move(translations)));
	}

	const auto enumerateRows = [&](auto &&callback) {
		for (auto i = 0, count = int(_scheme.rows.size()); i != count; ++i) {
			const auto &row = _scheme.rows[i];

			Assert(row.valueClass != Scheme::ValueClass::Additional
				|| !_scheme.additionalDependencyKey.isEmpty());
			auto fields = (row.valueClass == Scheme::ValueClass::Scans)
				? scansData
				: data;
			if (!fields) {
				continue;
			}
			callback(i, row, *fields);
		}
	};
	auto maxLabelWidth = 0;
	enumerateRows([&](
			int i,
			const EditDocumentScheme::Row &row,
			const ValueMap &fields) {
		accumulate_max(
			maxLabelWidth,
			Ui::PanelDetailsRow::LabelWidth(row.label));
	});
	if (maxLabelWidth > 0) {
		if (error && !error->isEmpty()) {
			_commonError = inner->add(
				object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
					inner,
					object_ptr<Ui::FlatLabel>(
						inner,
						*error,
						st::passportVerifyErrorLabel),
					st::passportValueErrorPadding));
			_commonError->toggle(true, anim::type::instant);
		}
		inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				data ? _scheme.detailsHeader : _scheme.fieldsHeader,
				st::passportFormHeader),
			st::passportDetailsHeaderPadding);
		enumerateRows([&](
				int i,
				const Scheme::Row &row,
				const ValueMap &fields) {
			if (row.valueClass != Scheme::ValueClass::Additional) {
				createDetailsRow(inner, i, row, fields, maxLabelWidth);
			}
		});
		if (data && !_scheme.additionalDependencyKey.isEmpty()) {
			const auto row = findRow(_scheme.additionalDependencyKey);
			const auto wrap = inner->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					inner,
					object_ptr<Ui::VerticalLayout>(inner)));
			const auto added = wrap->entity();

			auto showIfError = false;
			enumerateRows([&](
					int i,
					const Scheme::Row &row,
					const ValueMap &fields) {
				if (row.valueClass != Scheme::ValueClass::Additional) {
					return;
				}
				const auto it = fields.fields.find(row.key);
				if (it == end(fields.fields)) {
					return;
				} else if (!it->second.error.isEmpty()) {
					showIfError = true;
				} else if (it->second.text.isEmpty()) {
					return;
				}
				const auto fallbackIt = fields.fields.find(
					row.additionalFallbackKey);
				if (fallbackIt != end(fields.fields)
					&& fallbackIt->second.text != it->second.text) {
					showIfError = true;
				}
			});
			const auto shown = [=](const QString &code) {
				using Result = Scheme::AdditionalVisibility;
				const auto value = _scheme.additionalShown(code);
				return (value == Result::Shown)
					|| (value == Result::OnlyIfError && showIfError);
			};

			auto title = row->value(
			) | rpl::filter(
				shown
			) | rpl::map([=](const QString &code) {
				return _scheme.additionalHeader(code);
			});
			added->add(
				object_ptr<Ui::FlatLabel>(
					added,
					std::move(title),
					st::passportFormHeader),
				st::passportNativeNameHeaderPadding);

			enumerateRows([&](
					int i,
					const Scheme::Row &row,
					const ValueMap &fields) {
				if (row.valueClass == Scheme::ValueClass::Additional) {
					createDetailsRow(added, i, row, fields, maxLabelWidth);
				}
			});

			auto description = row->value(
			) | rpl::filter(
				shown
			) | rpl::map([=](const QString &code) {
				return _scheme.additionalDescription(code);
			});
			added->add(
				object_ptr<Ui::DividerLabel>(
					added,
					object_ptr<Ui::FlatLabel>(
						added,
						std::move(description),
						st::boxDividerLabel),
					st::passportFormLabelPadding),
				st::passportNativeNameAboutMargin);

			wrap->toggleOn(row->value() | rpl::map(shown));
			wrap->finishAnimating();

			row->value(
			) | rpl::map(
				shown
			) | rpl::start_with_next([=](bool visible) {
				_additionalShown = visible;
			}, lifetime());
		}

		inner->add(
			object_ptr<Ui::FixedHeightWidget>(inner, st::passportDetailsSkip));
	}
	if (auto text = _controller->deleteValueLabel()) {
		inner->add(
			object_ptr<Ui::SettingsButton>(
				inner,
				std::move(*text) | Ui::Text::ToUpper(),
				st::passportDeleteButton),
			st::passportUploadButtonPadding
		)->addClickHandler([=] {
			_controller->deleteValue();
		});
	}

	return inner;
}

void PanelEditDocument::createDetailsRow(
		not_null<Ui::VerticalLayout*> container,
		int i,
		const Scheme::Row &row,
		const ValueMap &fields,
		int maxLabelWidth) {
	const auto valueOrEmpty = [&](
			const ValueMap &values,
			const QString &key) {
		const auto &fields = values.fields;
		if (const auto i = fields.find(key); i != fields.end()) {
			return i->second;
		}
		return ValueField();
	};

	const auto current = valueOrEmpty(fields, row.key);
	const auto showBox = [controller = _controller](
			object_ptr<Ui::BoxContent> box) {
		controller->show(std::move(box));
	};
	const auto isoByPhone = Data::CountryISO2ByPhone(
		_controller->bot()->session().user()->phone());

	const auto [it, ok] = _details.emplace(
		i,
		container->add(Ui::PanelDetailsRow::Create(
			container,
			showBox,
			isoByPhone,
			row.inputType,
			row.label,
			maxLabelWidth,
			current.text,
			current.error,
			row.lengthLimit)));
	const bool details = (row.valueClass != Scheme::ValueClass::Scans);
	it->second->value(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		if (details) {
			_fieldsChanged = true;
			updateCommonError();
		} else {
			Assert(_editScans != nullptr);
			_editScans->scanFieldsChanged(true);
		}
	}, it->second->lifetime());
}

not_null<Ui::PanelDetailsRow*> PanelEditDocument::findRow(
		const QString &key) const {
	for (auto i = 0, count = int(_scheme.rows.size()); i != count; ++i) {
		const auto &row = _scheme.rows[i];
		if (row.key == key) {
			const auto it = _details.find(i);
			Assert(it != end(_details));
			return it->second.data();
		}
	}
	Unexpected("Row not found in PanelEditDocument::findRow.");
}

void PanelEditDocument::updateCommonError() {
	if (_commonError) {
		_commonError->toggle(!_fieldsChanged, anim::type::normal);
	}
}

void PanelEditDocument::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		for (const auto &[index, row] : _details) {
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
	for (const auto &[i, field] : _details) {
		const auto &row = _scheme.rows[i];
		auto &fields = (row.valueClass == Scheme::ValueClass::Scans)
			? result.filesData
			: result.data;
		if (row.valueClass == Scheme::ValueClass::Additional
			&& !_additionalShown) {
			continue;
		}
		fields.fields[row.key].text = field->valueCurrent();
	}
	if (!_additionalShown) {
		fillAdditionalFromFallbacks(result);
	}
	return result;
}

void PanelEditDocument::fillAdditionalFromFallbacks(Result &result) const {
	for (const auto &row : _scheme.rows) {
		if (row.valueClass != Scheme::ValueClass::Additional) {
			continue;
		}
		Assert(!row.additionalFallbackKey.isEmpty());
		auto &fields = result.data;
		const auto j = fields.fields.find(row.additionalFallbackKey);
		Assert(j != end(fields.fields));
		fields.fields[row.key] = j->second;
	}
}

bool PanelEditDocument::validate() {
	auto error = _editScans
		? _editScans->validateGetErrorTop()
		: std::nullopt;
	if (error) {
		const auto errortop = _editScans->mapToGlobal(QPoint(0, *error));
		const auto scrolltop = _scroll->mapToGlobal(QPoint(0, 0));
		const auto scrolldelta = errortop.y() - scrolltop.y();
		_scroll->scrollToY(_scroll->scrollTop() + scrolldelta);
	} else if (_commonError && !_fieldsChanged) {
		const auto firsttop = _commonError->mapToGlobal(QPoint(0, 0));
		const auto scrolltop = _scroll->mapToGlobal(QPoint(0, 0));
		const auto scrolldelta = firsttop.y() - scrolltop.y();
		_scroll->scrollToY(_scroll->scrollTop() + scrolldelta);
		error = firsttop.y();
	}
	auto first = QPointer<Ui::PanelDetailsRow>();
	for (const auto &[i, field] : ranges::views::reverse(_details)) {
		const auto &row = _scheme.rows[i];
		if (row.valueClass == Scheme::ValueClass::Additional
			&& !_additionalShown) {
			continue;
		}
		if (field->errorShown()) {
			field->showError();
			first = field;
		} else if (row.error) {
			if (const auto error = row.error(field->valueCurrent())) {
				field->showError(error);
				first = field;
			}
		}
	}
	if (error) {
		return false;
	} else if (!first) {
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

object_ptr<Ui::BoxContent> RequestIdentityType(
		Fn<void(int index)> submit,
		std::vector<QString> labels) {
	return Box<RequestTypeBox>(
		tr::lng_passport_identity_title(),
		tr::lng_passport_identity_about(tr::now),
		std::move(labels),
		submit);
}

object_ptr<Ui::BoxContent> RequestAddressType(
		Fn<void(int index)> submit,
		std::vector<QString> labels) {
	return Box<RequestTypeBox>(
		tr::lng_passport_address_title(),
		tr::lng_passport_address_about(tr::now),
		std::move(labels),
		submit);
}

object_ptr<Ui::BoxContent> ConfirmDeleteDocument(
		Fn<void(bool withDetails)> submit,
		const QString &text,
		const QString &detailsCheckbox) {
	return Box<DeleteDocumentBox>(text, detailsCheckbox, submit);
}

} // namespace Passport
