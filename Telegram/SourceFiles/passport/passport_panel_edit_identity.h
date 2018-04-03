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
class InputField;
class VerticalLayout;
class ScrollArea;
class FadeShadow;
class PlainShadow;
class FlatLabel;
class RoundButton;
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
struct ValueMap;
struct ScanInfo;
class ScanButton;

class PanelEditIdentity : public Ui::RpWidget {
public:
	PanelEditIdentity(
		QWidget *parent,
		not_null<PanelController*> controller,
		const ValueMap &data,
		const ValueMap &scanData,
		std::vector<ScanInfo> &&files);

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupControls(const ValueMap &data, const ValueMap &scanData);
	not_null<Ui::RpWidget*> setupContent(
		const ValueMap &data,
		const ValueMap &scanData);
	void updateControlsGeometry();

	void chooseScan();
	void encryptScan(const QString &path);
	void encryptScanContent(QByteArray &&content);
	void updateScan(ScanInfo &&info);
	void pushScan(const ScanInfo &info);

	rpl::producer<QString> uploadButtonText() const;
	void save();

	not_null<PanelController*> _controller;
	std::vector<ScanInfo> _files;

	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FadeShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;

	QPointer<Ui::SlideWrap<BoxContentDivider>> _scansDivider;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _scansHeader;
	QPointer<Ui::VerticalLayout> _scansWrap;
	std::vector<base::unique_qptr<Ui::SlideWrap<ScanButton>>> _scans;
	QPointer<Info::Profile::Button> _scansUpload;
	rpl::event_stream<rpl::producer<QString>> _scansUploadTexts;

	QPointer<Ui::InputField> _firstName;
	QPointer<Ui::InputField> _lastName;

	object_ptr<Ui::RoundButton> _done;

};

} // namespace Passport
