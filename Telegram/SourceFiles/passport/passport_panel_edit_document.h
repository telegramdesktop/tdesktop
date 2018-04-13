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

namespace Passport {

class PanelController;
struct ValueMap;
struct ScanInfo;
class EditScans;
class PanelDetailsRow;
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
		base::lambda<bool(const QString &value)> validate;
		base::lambda<QString(const QString &value)> format;
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
		std::vector<ScanInfo> &&files,
		std::unique_ptr<ScanInfo> &&selfie);
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
		std::vector<ScanInfo> &&files,
		std::unique_ptr<ScanInfo> &&selfie);
	not_null<Ui::RpWidget*> setupContent(
		const ValueMap &data,
		const ValueMap *scanData,
		std::vector<ScanInfo> &&files,
		std::unique_ptr<ScanInfo> &&selfie);
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

	object_ptr<Ui::RoundButton> _done;

};

object_ptr<BoxContent> RequestIdentityType(
	base::lambda<void(int index)> submit,
	std::vector<QString> labels);
object_ptr<BoxContent> RequestAddressType(
	base::lambda<void(int index)> submit,
	std::vector<QString> labels);

} // namespace Passport
