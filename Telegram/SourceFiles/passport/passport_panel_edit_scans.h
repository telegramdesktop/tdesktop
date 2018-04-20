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

enum class ReadScanError {
	FileTooLarge,
	CantReadImage,
	BadImageSize,
	Unknown,
};

class EditScans : public Ui::RpWidget {
public:
	EditScans(
		QWidget *parent,
		not_null<PanelController*> controller,
		const QString &header,
		const QString &errorMissing,
		std::vector<ScanInfo> &&files,
		std::unique_ptr<ScanInfo> &&selfie);

	base::optional<int> validateGetErrorTop();

	static void ChooseScan(
		QPointer<QWidget> parent,
		base::lambda<void(QByteArray&&)> doneCallback,
		base::lambda<void(ReadScanError)> errorCallback);

private:
	void setupContent(const QString &header);
	void chooseScan();
	void chooseSelfie();
	void updateScan(ScanInfo &&info);
	void updateSelfie(ScanInfo &&info);
	void updateFileRow(
		not_null<ScanButton*> button,
		const ScanInfo &info);
	void pushScan(const ScanInfo &info);
	void createSelfieRow(const ScanInfo &info);
	base::unique_qptr<Ui::SlideWrap<ScanButton>> createScan(
		not_null<Ui::VerticalLayout*> parent,
		const ScanInfo &info,
		const QString &name);

	rpl::producer<QString> uploadButtonText() const;

	void toggleError(bool shown);
	void hideError();
	void errorAnimationCallback();
	bool uploadedSomeMore() const;

	void toggleSelfieError(bool shown);
	void hideSelfieError();
	void selfieErrorAnimationCallback();

	not_null<PanelController*> _controller;
	std::vector<ScanInfo> _files;
	std::unique_ptr<ScanInfo> _selfie;
	int _initialCount = 0;
	QString _errorMissing;

	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::SlideWrap<BoxContentDivider>> _divider;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _header;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _uploadMoreError;
	QPointer<Ui::VerticalLayout> _wrap;
	std::vector<base::unique_qptr<Ui::SlideWrap<ScanButton>>> _rows;
	QPointer<Info::Profile::Button> _upload;
	rpl::event_stream<rpl::producer<QString>> _uploadTexts;
	bool _errorShown = false;
	Animation _errorAnimation;

	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _selfieHeader;
	QPointer<Ui::VerticalLayout> _selfieWrap;
	base::unique_qptr<Ui::SlideWrap<ScanButton>> _selfieRow;
	QPointer<Info::Profile::Button> _selfieUpload;
	bool _selfieErrorShown = false;
	Animation _selfieErrorAnimation;

};

} // namespace Passport
