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

enum class SpecialFile;
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
		const QString &error,
		const QString &errorMissing,
		std::vector<ScanInfo> &&files);
	EditScans(
		QWidget *parent,
		not_null<PanelController*> controller,
		const QString &error,
		std::map<SpecialFile, ScanInfo> &&specialFiles);

	base::optional<int> validateGetErrorTop();

	void scanFieldsChanged(bool changed);

	static void ChooseScan(
		QPointer<QWidget> parent,
		Fn<void(QByteArray&&)> doneCallback,
		Fn<void(ReadScanError)> errorCallback,
		bool allowMany);

	~EditScans();

private:
	struct SpecialScan;

	void setupScans(const QString &header);
	void setupSpecialScans(std::map<SpecialFile, ScanInfo> &&files);
	void init();

	void chooseScan();
	void chooseSpecialScan(SpecialFile type);
	void updateScan(ScanInfo &&info);
	void updateSpecialScan(SpecialFile type, ScanInfo &&info);
	void updateFileRow(
		not_null<ScanButton*> button,
		const ScanInfo &info);
	void pushScan(const ScanInfo &info);
	void createSpecialScanRow(
		SpecialScan &scan,
		const ScanInfo &info,
		bool requiresBothSides);
	base::unique_qptr<Ui::SlideWrap<ScanButton>> createScan(
		not_null<Ui::VerticalLayout*> parent,
		const ScanInfo &info,
		const QString &name);
	SpecialScan &findSpecialScan(SpecialFile type);

	rpl::producer<QString> uploadButtonText() const;

	void updateErrorLabels();
	void toggleError(bool shown);
	void hideError();
	void errorAnimationCallback();
	bool uploadedSomeMore() const;
	bool somethingChanged() const;

	void toggleSpecialScanError(SpecialFile type, bool shown);
	void hideSpecialScanError(SpecialFile type);
	void specialScanErrorAnimationCallback(SpecialFile type);
	void specialScanChanged(SpecialFile type, bool changed);

	not_null<PanelController*> _controller;
	std::vector<ScanInfo> _files;
	int _initialCount = 0;
	QString _error;
	QString _errorMissing;

	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::SlideWrap<BoxContentDivider>> _divider;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _header;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _commonError;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _uploadMoreError;
	QPointer<Ui::VerticalLayout> _wrap;
	std::vector<base::unique_qptr<Ui::SlideWrap<ScanButton>>> _rows;
	QPointer<Info::Profile::Button> _upload;
	rpl::event_stream<rpl::producer<QString>> _uploadTexts;
	bool _scanFieldsChanged = false;
	bool _specialScanChanged = false;
	bool _errorShown = false;
	Animation _errorAnimation;

	std::map<SpecialFile, SpecialScan> _specialScans;

};

} // namespace Passport
