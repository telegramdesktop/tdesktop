/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Ui {
class LinkButton;
class InputField;
} // namespace Ui

namespace Passport {

class PanelController;
struct ScanInfo;
class ScanButton;

struct IdentityData {
	QString name;
	QString surname;
};

class IdentityBox : public BoxContent {
public:
	IdentityBox(
		QWidget*,
		not_null<PanelController*> controller,
		int valueIndex,
		const IdentityData &data,
		std::vector<ScanInfo> &&files);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void chooseScan();
	void encryptScan(const QString &path);
	void encryptScanContent(QByteArray &&content);
	void updateScan(ScanInfo &&info);
	int countHeight() const;
	void updateControlsPosition();
	void save();

	not_null<PanelController*> _controller;
	int _valueIndex = -1;

	std::vector<ScanInfo> _files;

	std::vector<object_ptr<ScanButton>> _scans;
	object_ptr<Ui::LinkButton> _uploadScan;
	object_ptr<Ui::InputField> _name;
	object_ptr<Ui::InputField> _surname;

};

} // namespace Passport
