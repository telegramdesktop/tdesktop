/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

namespace Ui {
class BoxContentDivider;
class VerticalLayout;
class SettingsButton;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Passport {

enum class FileType;
class PanelController;
class ScanButton;
struct ScanInfo;

enum class ReadScanError {
	FileTooLarge,
	CantReadImage,
	BadImageSize,
	Unknown,
};

struct ScanListData {
	std::vector<ScanInfo> files;
	QString errorMissing;
};

class EditScans : public Ui::RpWidget {
public:
	EditScans(
		QWidget *parent,
		not_null<PanelController*> controller,
		const QString &header,
		const QString &error,
		ScanListData &&scans,
		std::optional<ScanListData> &&translations);
	EditScans(
		QWidget *parent,
		not_null<PanelController*> controller,
		const QString &header,
		const QString &error,
		std::map<FileType, ScanInfo> &&specialFiles,
		std::optional<ScanListData> &&translations);

	std::optional<int> validateGetErrorTop();

	void scanFieldsChanged(bool changed);

	static void ChooseScan(
		QPointer<QWidget> parent,
		FileType type,
		Fn<void(QByteArray&&)> doneCallback,
		Fn<void(ReadScanError)> errorCallback);

	~EditScans();

private:
	struct SpecialScan;
	struct List {
		List(not_null<PanelController*> controller, ScanListData &&data);
		List(not_null<PanelController*> controller);
		List(
			not_null<PanelController*> controller,
			std::optional<ScanListData> &&data);

		bool uploadedSomeMore() const;
		bool uploadMoreRequired() const;
		Ui::SlideWrap<ScanButton> *nonDeletedErrorRow() const;
		rpl::producer<QString> uploadButtonText() const;
		void toggleError(bool shown);
		void hideError();
		void errorAnimationCallback();
		void updateScan(ScanInfo &&info, int width);
		void pushScan(const ScanInfo &info);

		not_null<PanelController*> controller;
		std::vector<ScanInfo> files;
		std::optional<int> initialCount;
		QString errorMissing;
		QPointer<Ui::SlideWrap<Ui::BoxContentDivider>> divider;
		QPointer<Ui::SlideWrap<Ui::FlatLabel>> header;
		QPointer<Ui::SlideWrap<Ui::FlatLabel>> uploadMoreError;
		QPointer<Ui::VerticalLayout> wrap;
		std::vector<base::unique_qptr<Ui::SlideWrap<ScanButton>>> rows;
		QPointer<Ui::SettingsButton> upload;
		rpl::event_stream<rpl::producer<QString>> uploadTexts;
		bool errorShown = false;
		Ui::Animations::Simple errorAnimation;
	};

	List &list(FileType type);
	const List &list(FileType type) const;

	void setupScans(const QString &header);
	void setupList(
		not_null<Ui::VerticalLayout*> container,
		FileType type,
		const QString &header);
	void setupSpecialScans(
		const QString &header,
		std::map<FileType, ScanInfo> &&files);
	void init();

	void chooseScan(FileType type);
	void updateScan(ScanInfo &&info);
	void updateSpecialScan(ScanInfo &&info);
	void createSpecialScanRow(
		SpecialScan &scan,
		const ScanInfo &info,
		bool requiresBothSides);
	base::unique_qptr<Ui::SlideWrap<ScanButton>> createScan(
		not_null<Ui::VerticalLayout*> parent,
		const ScanInfo &info,
		const QString &name);
	SpecialScan &findSpecialScan(FileType type);

	void updateErrorLabels();
	bool somethingChanged() const;

	void toggleSpecialScanError(FileType type, bool shown);
	void hideSpecialScanError(FileType type);
	void specialScanErrorAnimationCallback(FileType type);
	void specialScanChanged(FileType type, bool changed);

	not_null<PanelController*> _controller;
	QString _error;
	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> _commonError;
	bool _scanFieldsChanged = false;
	bool _specialScanChanged = false;

	List _scansList;
	std::map<FileType, SpecialScan> _specialScans;
	List _translationsList;


};

} // namespace Passport
