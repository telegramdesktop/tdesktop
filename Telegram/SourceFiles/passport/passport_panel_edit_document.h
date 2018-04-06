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

class PanelEditDocument : public Ui::RpWidget {
public:
	struct Scheme {
		enum class ValueType {
			Fields,
			Scans,
		};
		struct Row {
			ValueType type = ValueType::Fields;
			QString key;
			QString label;
			base::lambda<bool(const QString &value)> validate;
		};
		std::vector<Row> rows;
		QString rowsHeader;

	};

	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const ValueMap &data,
		const ValueMap &scanData,
		std::vector<ScanInfo> &&files);
	PanelEditDocument(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const ValueMap &data);

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupControls(
		const ValueMap &data,
		const ValueMap *scanData,
		std::vector<ScanInfo> &&files);
	not_null<Ui::RpWidget*> setupContent(
		const ValueMap &data,
		const ValueMap *scanData,
		std::vector<ScanInfo> &&files);
	void updateControlsGeometry();

	void save();

	not_null<PanelController*> _controller;
	Scheme _scheme;

	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FadeShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;

	QPointer<EditScans> _editScans;
	std::vector<QPointer<PanelDetailsRow>> _details;

	object_ptr<Ui::RoundButton> _done;

};

} // namespace Passport
