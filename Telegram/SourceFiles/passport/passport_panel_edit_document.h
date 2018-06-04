/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class InputField;
class ScrollArea;
class FadeShadow;
class PlainShadow;
class RoundButton;
} // namespace Ui

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

namespace Passport {

class PanelController;
struct ValueMap;
struct ScanInfo;
class EditScans;
class PanelDetailsRow;
enum class SpecialFile;
enum class PanelDetailsType;

struct EditDocumentScheme {
	enum class ValueClass {
		Fields,
		Scans,
	};
	struct Row {
		ValueClass valueClass = ValueClass::Fields;
		PanelDetailsType inputType = PanelDetailsType();
		QString key;
		QString label;
		Fn<base::optional<QString>(const QString &value)> error;
		Fn<QString(const QString &value)> format;
		int lengthLimit = 0;
	};
	std::vector<Row> rows;
	QString rowsHeader;
	QString scansHeader;

};

class PanelEditDocument : public Ui::RpWidget {
public:
	using Scheme = EditDocumentScheme;

	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const ValueMap &data,
		const ValueMap &scanData,
		const QString &missingScansError,
		std::vector<ScanInfo> &&files,
		std::map<SpecialFile, ScanInfo> &&specialFiles);
	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const ValueMap &data);

	bool hasUnsavedChanges() const;

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Result;
	void setupControls(
		const ValueMap &data,
		const ValueMap *scanData,
		const QString &missingScansError,
		std::vector<ScanInfo> &&files,
		std::map<SpecialFile, ScanInfo> &&specialFiles);
	not_null<Ui::RpWidget*> setupContent(
		const ValueMap &data,
		const ValueMap *scanData,
		const QString &missingScansError,
		std::vector<ScanInfo> &&files,
		std::map<SpecialFile, ScanInfo> &&specialFiles);
	void updateControlsGeometry();

	Result collect() const;
	bool validate();
	void save();

	not_null<PanelController*> _controller;
	Scheme _scheme;

	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FadeShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;

	QPointer<EditScans> _editScans;
	std::map<int, QPointer<PanelDetailsRow>> _details;

	QPointer<Info::Profile::Button> _delete;

	object_ptr<Ui::RoundButton> _done;

};

object_ptr<BoxContent> RequestIdentityType(
	Fn<void(int index)> submit,
	std::vector<QString> labels);
object_ptr<BoxContent> RequestAddressType(
	Fn<void(int index)> submit,
	std::vector<QString> labels);

object_ptr<BoxContent> ConfirmDeleteDocument(
	Fn<void(bool withDetails)> submit,
	const QString &text,
	const QString &detailsCheckbox = QString());

} // namespace Passport
