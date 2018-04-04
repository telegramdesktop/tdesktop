/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

class BoxContentDivider;

namespace Ui {
class VerticalLayout;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

namespace Passport {

class PanelController;
class ScanButton;
struct ScanInfo;

class EditScans : public Ui::RpWidget {
public:
	EditScans(
		QWidget *parent,
		not_null<PanelController*> controller,
		std::vector<ScanInfo> &&files);

private:
	void setupContent();
	void chooseScan();
	void encryptScan(const QString &path);
	void encryptScanContent(QByteArray &&content);
	void updateScan(ScanInfo &&info);
	void pushScan(const ScanInfo &info);

	rpl::producer<QString> uploadButtonText() const;

	not_null<PanelController*> _controller;
	std::vector<ScanInfo> _files;

	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::SlideWrap<BoxContentDivider>> _divider;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _header;
	QPointer<Ui::VerticalLayout> _wrap;
	std::vector<base::unique_qptr<Ui::SlideWrap<ScanButton>>> _rows;
	QPointer<Info::Profile::Button> _upload;
	rpl::event_stream<rpl::producer<QString>> _uploadTexts;

};

} // namespace Passport
