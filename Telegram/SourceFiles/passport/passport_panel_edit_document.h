/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {
class InputField;
class ScrollArea;
class FadeShadow;
class PlainShadow;
class FlatLabel;
class RoundButton;
class VerticalLayout;
class SettingsButton;
class BoxContent;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Passport::Ui {
using namespace ::Ui;
enum class PanelDetailsType;
class PanelDetailsRow;
} // namespace Passport::Ui

namespace Passport {

class PanelController;
struct ValueMap;
struct ScanInfo;
class EditScans;
enum class FileType;
struct ScanListData;

struct EditDocumentScheme {
	enum class ValueClass {
		Fields,
		Additional,
		Scans,
	};
	enum class AdditionalVisibility {
		Hidden,
		OnlyIfError,
		Shown,
	};
	struct Row {
		using Validator = Fn<std::optional<QString>(const QString &value)>;
		using Formatter = Fn<QString(const QString &value)>;
		ValueClass valueClass = ValueClass::Fields;
		Ui::PanelDetailsType inputType = Ui::PanelDetailsType();
		QString key;
		QString label;
		Validator error;
		Formatter format;
		int lengthLimit = 0;
		QString keyForAttachmentTo; // Attach [last|middle]_name to first_*.
		QString additionalFallbackKey; // *_name_native from *_name.
	};
	std::vector<Row> rows;
	QString fieldsHeader;
	QString detailsHeader;
	QString scansHeader;

	QString additionalDependencyKey;
	Fn<AdditionalVisibility(const QString &dependency)> additionalShown;
	Fn<QString(const QString &dependency)> additionalHeader;
	Fn<QString(const QString &dependency)> additionalDescription;

};

class PanelEditDocument : public Ui::RpWidget {
public:
	using Scheme = EditDocumentScheme;

	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const QString &error,
		const ValueMap &data,
		const QString &scansError,
		const ValueMap &scansData,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations,
		std::map<FileType, ScanInfo> &&specialFiles);
	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const QString &scansError,
		const ValueMap &scansData,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations,
		std::map<FileType, ScanInfo> &&specialFiles);
	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const QString &error,
		const ValueMap &data);

	bool hasUnsavedChanges() const;

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Result;
	void setupControls(
		const QString *error,
		const ValueMap *data,
		const QString *scansError,
		const ValueMap *scansData,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations,
		std::map<FileType, ScanInfo> &&specialFiles);
	not_null<Ui::RpWidget*> setupContent(
		const QString *error,
		const ValueMap *data,
		const QString *scansError,
		const ValueMap *scansData,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations,
		std::map<FileType, ScanInfo> &&specialFiles);
	void updateControlsGeometry();
	void updateCommonError();

	Result collect() const;
	void fillAdditionalFromFallbacks(Result &result) const;
	bool validate();
	void save();

	void createDetailsRow(
		not_null<Ui::VerticalLayout*> container,
		int i,
		const Scheme::Row &row,
		const ValueMap &fields,
		int maxLabelWidth);
	not_null<Ui::PanelDetailsRow*> findRow(const QString &key) const;

	not_null<PanelController*> _controller;
	Scheme _scheme;

	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FadeShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;

	QPointer<EditScans> _editScans;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _commonError;
	std::map<int, QPointer<Ui::PanelDetailsRow>> _details;
	bool _fieldsChanged = false;
	bool _additionalShown = false;

	QPointer<Ui::SettingsButton> _delete;

	object_ptr<Ui::RoundButton> _done;

};

object_ptr<Ui::BoxContent> RequestIdentityType(
	Fn<void(int index)> submit,
	std::vector<QString> labels);
object_ptr<Ui::BoxContent> RequestAddressType(
	Fn<void(int index)> submit,
	std::vector<QString> labels);

object_ptr<Ui::BoxContent> ConfirmDeleteDocument(
	Fn<void(bool withDetails)> submit,
	const QString &text,
	const QString &detailsCheckbox = QString());

} // namespace Passport
