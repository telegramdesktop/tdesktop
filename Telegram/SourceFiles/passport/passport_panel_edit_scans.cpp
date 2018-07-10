/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_edit_scans.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_details_row.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text_options.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "boxes/abstract_box.h"
#include "storage/storage_media_prepare.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {
namespace {

constexpr auto kMaxDimensions = 2048;
constexpr auto kMaxSize = 10 * 1024 * 1024;
constexpr auto kJpegQuality = 89;

static_assert(kMaxSize <= UseBigFilesFrom);

base::variant<ReadScanError, QByteArray> ProcessImage(QByteArray &&bytes) {
	auto image = App::readImage(base::take(bytes));
	if (image.isNull()) {
		return ReadScanError::CantReadImage;
	} else if (!Storage::ValidateThumbDimensions(image.width(), image.height())) {
		return ReadScanError::BadImageSize;
	}
	if (std::max(image.width(), image.height()) > kMaxDimensions) {
		image = std::move(image).scaled(
			kMaxDimensions,
			kMaxDimensions,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	auto result = QByteArray();
	{
		QBuffer buffer(&result);
		if (!image.save(&buffer, QByteArray("JPG"), kJpegQuality)) {
			return ReadScanError::Unknown;
		}
		base::take(image);
	}
	if (result.isEmpty()) {
		return ReadScanError::Unknown;
	} else if (result.size() > kMaxSize) {
		return ReadScanError::FileTooLarge;
	}
	return result;
}

} // namespace

class ScanButton : public Ui::AbstractButton {
public:
	ScanButton(
		QWidget *parent,
		const style::PassportScanRow &st,
		const QString &name,
		const QString &status,
		bool deleted,
		bool error);

	void setImage(const QImage &image);
	void setStatus(const QString &status);
	void setDeleted(bool deleted);
	void setError(bool error);

	rpl::producer<> deleteClicks() const {
		return _delete->entity()->clicks();
	}
	rpl::producer<> restoreClicks() const {
		return _restore->entity()->clicks();
	}

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	int countAvailableWidth() const;

	const style::PassportScanRow &_st;
	Text _name;
	Text _status;
	int _nameHeight = 0;
	int _statusHeight = 0;
	bool _error = false;
	QImage _image;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _delete;
	object_ptr<Ui::FadeWrapScaled<Ui::RoundButton>> _restore;

};

struct EditScans::SpecialScan {
	SpecialScan(ScanInfo &&file);

	ScanInfo file;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> header;
	QPointer<Ui::VerticalLayout> wrap;
	base::unique_qptr<Ui::SlideWrap<ScanButton>> row;
	QPointer<Info::Profile::Button> upload;
	bool errorShown = false;
	Animation errorAnimation;
	rpl::variable<bool> rowCreated;
};

ScanButton::ScanButton(
	QWidget *parent,
	const style::PassportScanRow &st,
	const QString &name,
	const QString &status,
	bool deleted,
	bool error)
: AbstractButton(parent)
, _st(st)
, _name(
	st::passportScanNameStyle,
	name,
	Ui::NameTextOptions())
, _status(
	st::defaultTextStyle,
	status,
	Ui::NameTextOptions())
, _error(error)
, _delete(this, object_ptr<Ui::IconButton>(this, _st.remove))
, _restore(
	this,
	object_ptr<Ui::RoundButton>(
		this,
		langFactory(lng_passport_delete_scan_undo),
		_st.restore)) {
	_delete->toggle(!deleted, anim::type::instant);
	_restore->toggle(deleted, anim::type::instant);
}

void ScanButton::setImage(const QImage &image) {
	_image = image;
	update();
}

void ScanButton::setStatus(const QString &status) {
	_status.setText(
		st::defaultTextStyle,
		status,
		Ui::NameTextOptions());
	update();
}

void ScanButton::setDeleted(bool deleted) {
	_delete->toggle(!deleted, anim::type::instant);
	_restore->toggle(deleted, anim::type::instant);
	update();
}

void ScanButton::setError(bool error) {
	_error = error;
	update();
}

int ScanButton::resizeGetHeight(int newWidth) {
	_nameHeight = st::semiboldFont->height;
	_statusHeight = st::normalFont->height;
	const auto result = _st.padding.top() + _st.size + _st.padding.bottom();
	const auto right = _st.padding.right();
	_delete->moveToRight(
		right,
		(result - _delete->height()) / 2,
		newWidth);
	_restore->moveToRight(
		right,
		(result - _restore->height()) / 2,
		newWidth);
	return result + st::lineWidth;
}

int ScanButton::countAvailableWidth() const {
	return width()
		- _st.padding.left()
		- _st.textLeft
		- _st.padding.right()
		- std::max(_delete->width(), _restore->width());
}

void ScanButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto left = _st.padding.left();
	const auto top = _st.padding.top();
	p.fillRect(
		left,
		height() - _st.border,
		width() - left,
		_st.border,
		_st.borderFg);

	const auto deleted = _restore->toggled();
	if (deleted) {
		p.setOpacity(st::passportScanDeletedOpacity);
	}

	if (_image.isNull()) {
		p.fillRect(left, top, _st.size, _st.size, Qt::black);
	} else {
		PainterHighQualityEnabler hq(p);
		const auto fromRect = [&] {
			if (_image.width() > _image.height()) {
				const auto shift = (_image.width() - _image.height()) / 2;
				return QRect(shift, 0, _image.height(), _image.height());
			} else {
				const auto shift = (_image.height() - _image.width()) / 2;
				return QRect(0, shift, _image.width(), _image.width());
			}
		}();
		p.drawImage(QRect(left, top, _st.size, _st.size), _image, fromRect);
	}
	const auto availableWidth = countAvailableWidth();

	p.setPen(st::windowFg);
	_name.drawLeftElided(
		p,
		left + _st.textLeft,
		top + _st.nameTop,
		availableWidth,
		width());
	p.setPen((_error && !deleted)
		? st::boxTextFgError
		: st::windowSubTextFg);
	_status.drawLeftElided(
		p,
		left + _st.textLeft,
		top + _st.statusTop,
		availableWidth,
		width());
}

EditScans::SpecialScan::SpecialScan(ScanInfo &&file)
: file(std::move(file)) {
}

EditScans::EditScans(
	QWidget *parent,
	not_null<PanelController*> controller,
	const QString &header,
	const QString &errorMissing,
	std::vector<ScanInfo> &&files)
: RpWidget(parent)
, _controller(controller)
, _files(std::move(files))
, _initialCount(_files.size())
, _errorMissing(errorMissing)
, _content(this) {
	setupScans(header);
}

EditScans::EditScans(
	QWidget *parent,
	not_null<PanelController*> controller,
	std::map<SpecialFile, ScanInfo> &&specialFiles)
: RpWidget(parent)
, _controller(controller)
, _initialCount(-1)
, _content(this) {
	setupSpecialScans(std::move(specialFiles));
}

bool EditScans::uploadedSomeMore() const {
	const auto from = begin(_files) + _initialCount;
	const auto till = end(_files);
	return std::find_if(from, till, [](const ScanInfo &file) {
		return !file.deleted;
	}) != till;
}

base::optional<int> EditScans::validateGetErrorTop() {
	auto result = base::optional<int>();
	const auto suggestResult = [&](int value) {
		if (!result || *result > value) {
			result = value;
		}
	};

	const auto exists = ranges::find_if(
		_files,
		[](const ScanInfo &file) { return !file.deleted; }) != end(_files);
	const auto errorExists = ranges::find_if(
		_files,
		[](const ScanInfo &file) { return !file.error.isEmpty(); }
	) != end(_files);

	if (_upload && (!exists
		|| ((errorExists || _uploadMoreError) && !uploadedSomeMore()))) {
		toggleError(true);
		suggestResult((_files.size() > 5) ? _upload->y() : _header->y());
	}

	const auto nonDeletedErrorIt = ranges::find_if(
		_files,
		[](const ScanInfo &file) {
			return !file.error.isEmpty() && !file.deleted;
		});
	if (nonDeletedErrorIt != end(_files)) {
		const auto index = (nonDeletedErrorIt - begin(_files));
//		toggleError(true);
		suggestResult(_rows[index]->y());
	}
	for (const auto &[type, scan] : _specialScans) {
		if (!scan.file.key.id
			|| scan.file.deleted
			|| !scan.file.error.isEmpty()) {
			toggleSpecialScanError(type, true);
			suggestResult(scan.header->y());
		}
	}
	return result;
}

void EditScans::setupScans(const QString &header) {
	const auto inner = _content.data();
	inner->move(0, 0);

	_divider = inner->add(
		object_ptr<Ui::SlideWrap<BoxContentDivider>>(
			inner,
			object_ptr<BoxContentDivider>(
				inner,
				st::passportFormDividerHeight)));
	_divider->toggle(_files.empty(), anim::type::instant);

	_header = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				header,
				Ui::FlatLabel::InitType::Simple,
				st::passportFormHeader),
			st::passportUploadHeaderPadding));
	_header->toggle(!_files.empty(), anim::type::instant);
	if (!_errorMissing.isEmpty()) {
		_uploadMoreError = inner->add(
			object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
				inner,
				object_ptr<Ui::FlatLabel>(
					inner,
					_errorMissing,
					Ui::FlatLabel::InitType::Simple,
					st::passportVerifyErrorLabel),
				st::passportUploadErrorPadding));
		_uploadMoreError->toggle(true, anim::type::instant);
	}
	_wrap = inner->add(object_ptr<Ui::VerticalLayout>(inner));
	for (const auto &scan : _files) {
		pushScan(scan);
		_rows.back()->show(anim::type::instant);
	}

	_upload = inner->add(
		object_ptr<Info::Profile::Button>(
			inner,
			_uploadTexts.events_starting_with(
				uploadButtonText()
			) | rpl::flatten_latest(),
			st::passportUploadButton),
		st::passportUploadButtonPadding);
	_upload->addClickHandler([=] {
		chooseScan();
	});

	inner->add(object_ptr<BoxContentDivider>(
		inner,
		st::passportFormDividerHeight));

	init();
}

void EditScans::setupSpecialScans(std::map<SpecialFile, ScanInfo> &&files) {
	const auto requiresBothSides = files.find(SpecialFile::ReverseSide)
		!= end(files);
	const auto title = [&](SpecialFile type) {
		switch (type) {
		case SpecialFile::FrontSide:
			return lang(requiresBothSides
				? lng_passport_front_side_title
				: lng_passport_main_page_title);
		case SpecialFile::ReverseSide:
			return lang(lng_passport_reverse_side_title);
		case SpecialFile::Selfie:
			return lang(lng_passport_selfie_title);
		}
		Unexpected("Type in special row title.");
	};
	const auto uploadKey = [=](SpecialFile type, bool hasScan) {
		switch (type) {
		case SpecialFile::FrontSide:
			return requiresBothSides
				? (hasScan
					? lng_passport_reupload_front_side
					: lng_passport_upload_front_side)
				: (hasScan
					? lng_passport_reupload_main_page
					: lng_passport_upload_main_page);
		case SpecialFile::ReverseSide:
			return hasScan
				? lng_passport_reupload_reverse_side
				: lng_passport_upload_reverse_side;
		case SpecialFile::Selfie:
			return hasScan
				? lng_passport_reupload_selfie
				: lng_passport_upload_selfie;
		}
		Unexpected("Type in special row upload key.");
	};
	const auto description = [&](SpecialFile type) {
		switch (type) {
		case SpecialFile::FrontSide:
			return lang(requiresBothSides
				? lng_passport_front_side_description
				: lng_passport_main_page_description);
		case SpecialFile::ReverseSide:
			return lang(lng_passport_reverse_side_description);
		case SpecialFile::Selfie:
			return lang(lng_passport_selfie_description);
		}
		Unexpected("Type in special row upload key.");
	};

	const auto inner = _content.data();
	inner->move(0, 0);
	for (auto &[type, info] : files) {
		const auto i = _specialScans.emplace(
			type,
			SpecialScan(std::move(info))).first;
		auto &scan = i->second;

		scan.header = inner->add(
			object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
				inner,
				object_ptr<Ui::FlatLabel>(
					inner,
					title(type),
					Ui::FlatLabel::InitType::Simple,
					st::passportFormHeader),
				st::passportUploadHeaderPadding));
		scan.header->toggle(scan.file.key.id != 0, anim::type::instant);
		scan.wrap = inner->add(object_ptr<Ui::VerticalLayout>(inner));
		if (scan.file.key.id) {
			createSpecialScanRow(scan, scan.file, requiresBothSides);
		}
		auto label = scan.rowCreated.value(
		) | rpl::map([=, type = type](bool created) {
			return Lang::Viewer(uploadKey(type, created));
		}) | rpl::flatten_latest(
		) | Info::Profile::ToUpperValue();
		scan.upload = inner->add(
			object_ptr<Info::Profile::Button>(
				inner,
				std::move(label),
				st::passportUploadButton),
			st::passportUploadButtonPadding);
		scan.upload->addClickHandler([=, type = type] {
			chooseSpecialScan(type);
		});

		inner->add(object_ptr<Ui::DividerLabel>(
			inner,
			object_ptr<Ui::FlatLabel>(
				_content,
				description(type),
				Ui::FlatLabel::InitType::Simple,
				st::boxDividerLabel),
			st::passportFormLabelPadding));
	}

	init();
}

void EditScans::init() {
	_controller->scanUpdated(
	) | rpl::start_with_next([=](ScanInfo &&info) {
		updateScan(std::move(info));
	}, lifetime());

	widthValue(
	) | rpl::start_with_next([=](int width) {
		_content->resizeToWidth(width);
	}, _content->lifetime());

	_content->heightValue(
	) | rpl::start_with_next([=](int height) {
		resize(width(), height);
	}, _content->lifetime());
}

void EditScans::updateScan(ScanInfo &&info) {
	if (info.special) {
		updateSpecialScan(*info.special, std::move(info));
		return;
	}
	const auto i = ranges::find(_files, info.key, [](const ScanInfo &file) {
		return file.key;
	});
	if (i != _files.end()) {
		*i = std::move(info);
		const auto scan = _rows[i - _files.begin()]->entity();
		updateFileRow(scan, *i);
		if (!i->deleted) {
			hideError();
		}
	} else {
		_files.push_back(std::move(info));
		pushScan(_files.back());
		_wrap->resizeToWidth(width());
		_rows.back()->show(anim::type::normal);
		_divider->hide(anim::type::normal);
		_header->show(anim::type::normal);
		_uploadTexts.fire(uploadButtonText());
	}
	if (_uploadMoreError) {
		_uploadMoreError->toggle(!uploadedSomeMore(), anim::type::normal);
	}
}

void EditScans::updateSpecialScan(SpecialFile type, ScanInfo &&info) {
	Expects(info.key.id != 0);

	const auto i = _specialScans.find(type);
	if (i == end(_specialScans)) {
		return;
	}
	auto &scan = i->second;
	if (scan.file.key.id) {
		updateFileRow(scan.row->entity(), info);
		scan.rowCreated = !info.deleted;
		if (!info.deleted) {
			hideSpecialScanError(type);
		}
	} else {
		const auto requiresBothSides
			= (_specialScans.find(SpecialFile::ReverseSide)
				!= end(_specialScans));
		createSpecialScanRow(scan, info, requiresBothSides);
		scan.wrap->resizeToWidth(width());
		scan.row->show(anim::type::normal);
		scan.header->show(anim::type::normal);
	}
	scan.file = std::move(info);
}

void EditScans::updateFileRow(
		not_null<ScanButton*> button,
		const ScanInfo &info) {
	button->setStatus(info.status);
	button->setImage(info.thumb);
	button->setDeleted(info.deleted);
	button->setError(!info.error.isEmpty());
};


void EditScans::createSpecialScanRow(
		SpecialScan &scan,
		const ScanInfo &info,
		bool requiresBothSides) {
	Expects(scan.file.special.has_value());

	const auto type = *scan.file.special;
	const auto name = [&] {
		switch (type) {
		case SpecialFile::FrontSide:
			return lang(requiresBothSides
				? lng_passport_front_side_name
				: lng_passport_main_page_name);
		case SpecialFile::ReverseSide:
			return lang(lng_passport_reverse_side_name);
		case SpecialFile::Selfie:
			return lang(lng_passport_selfie_name);
		}
		Unexpected("Type in special file name.");
	}();
	scan.row = createScan(scan.wrap, info, name);
	const auto row = scan.row->entity();

	row->deleteClicks(
	) | rpl::start_with_next([=] {
		_controller->deleteSpecialScan(type);
	}, row->lifetime());

	row->restoreClicks(
	) | rpl::start_with_next([=] {
		_controller->restoreSpecialScan(type);
	}, row->lifetime());

	scan.rowCreated = !info.deleted;
	hideSpecialScanError(type);
}

void EditScans::pushScan(const ScanInfo &info) {
	const auto index = _rows.size();
	_rows.push_back(createScan(
		_wrap,
		info,
		lng_passport_scan_index(lt_index, QString::number(index + 1))));
	_rows.back()->hide(anim::type::instant);

	const auto scan = _rows.back()->entity();

	scan->deleteClicks(
	) | rpl::start_with_next([=] {
		_controller->deleteScan(index);
	}, scan->lifetime());

	scan->restoreClicks(
	) | rpl::start_with_next([=] {
		_controller->restoreScan(index);
	}, scan->lifetime());

	hideError();
}

base::unique_qptr<Ui::SlideWrap<ScanButton>> EditScans::createScan(
		not_null<Ui::VerticalLayout*> parent,
		const ScanInfo &info,
		const QString &name) {
	auto result = base::unique_qptr<Ui::SlideWrap<ScanButton>>(
		parent->add(object_ptr<Ui::SlideWrap<ScanButton>>(
			parent,
			object_ptr<ScanButton>(
				parent,
				st::passportScanRow,
				name,
				info.status,
				info.deleted,
				!info.error.isEmpty()))));
	result->entity()->setImage(info.thumb);
	return result;
}

void EditScans::chooseScan() {
	if (!_controller->canAddScan()) {
		_controller->showToast(lang(lng_passport_scans_limit_reached));
		return;
	}
	ChooseScan(this, [=](QByteArray &&content) {
		_controller->uploadScan(std::move(content));
	}, [=](ReadScanError error) {
		_controller->readScanError(error);
	}, true);
}

void EditScans::chooseSpecialScan(SpecialFile type) {
	ChooseScan(this, [=](QByteArray &&content) {
		_controller->uploadSpecialScan(type, std::move(content));
	}, [=](ReadScanError error) {
		_controller->readScanError(error);
	}, false);
}

void EditScans::ChooseScan(
		QPointer<QWidget> parent,
		Fn<void(QByteArray&&)> doneCallback,
		Fn<void(ReadScanError)> errorCallback,
		bool allowMany) {
	Expects(parent != nullptr);

	const auto processFiles = std::make_shared<Fn<void(QStringList&&)>>();
	const auto filter = FileDialog::AllFilesFilter()
		+ qsl(";;Image files (*")
		+ cImgExtensions().join(qsl(" *"))
		+ qsl(")");
	const auto guardedCallback = crl::guard(parent, doneCallback);
	const auto guardedError = crl::guard(parent, errorCallback);
	const auto onMainCallback = [=](
			QByteArray &&content,
			QStringList &&remainingFiles) {
		crl::on_main([
			=,
			bytes = std::move(content),
			remainingFiles = std::move(remainingFiles)
		]() mutable {
			guardedCallback(std::move(bytes));
			(*processFiles)(std::move(remainingFiles));
		});
	};
	const auto onMainError = [=](ReadScanError error) {
		crl::on_main([=] {
			guardedError(error);
		});
	};
	const auto processImage = [=](
			QByteArray &&content,
			QStringList &&remainingFiles) {
		crl::async([
			=,
			bytes = std::move(content),
			remainingFiles = std::move(remainingFiles)
		]() mutable {
			auto result = ProcessImage(std::move(bytes));
			if (const auto error = base::get_if<ReadScanError>(&result)) {
				onMainError(*error);
			} else {
				auto content = base::get_if<QByteArray>(&result);
				Assert(content != nullptr);
				onMainCallback(std::move(*content), std::move(remainingFiles));
			}
		});
	};
	const auto processOpened = [=](FileDialog::OpenResult &&result) {
		if (result.paths.size() > 0) {
			(*processFiles)(std::move(result.paths));
		} else if (!result.remoteContent.isEmpty()) {
			processImage(std::move(result.remoteContent), {});
		}
	};
	*processFiles = [=](QStringList &&files) {
		while (!files.isEmpty()) {
			auto file = files.front();
			files.removeAt(0);

			auto content = [&] {
				QFile f(file);
				if (f.size() > App::kImageSizeLimit) {
					guardedError(ReadScanError::FileTooLarge);
					return QByteArray();
				} else if (!f.open(QIODevice::ReadOnly)) {
					guardedError(ReadScanError::CantReadImage);
					return QByteArray();
				}
				return f.readAll();
			}();
			if (!content.isEmpty()) {
				processImage(std::move(content), std::move(files));
				return;
			}
		}
	};
	(allowMany ? FileDialog::GetOpenPaths : FileDialog::GetOpenPath)(
		parent,
		lang(lng_passport_choose_image),
		filter,
		processOpened,
		nullptr);
}

rpl::producer<QString> EditScans::uploadButtonText() const {
	return Lang::Viewer(_files.empty()
		? lng_passport_upload_scans
		: lng_passport_upload_more) | Info::Profile::ToUpperValue();
}

void EditScans::hideError() {
	toggleError(false);
}

void EditScans::toggleError(bool shown) {
	if (_errorShown != shown) {
		_errorShown = shown;
		_errorAnimation.start(
			[=] { errorAnimationCallback(); },
			_errorShown ? 0. : 1.,
			_errorShown ? 1. : 0.,
			st::passportDetailsField.duration);
	}
}

void EditScans::errorAnimationCallback() {
	const auto error = _errorAnimation.current(_errorShown ? 1. : 0.);
	if (error == 0.) {
		_upload->setColorOverride(base::none);
	} else {
		_upload->setColorOverride(anim::color(
			st::passportUploadButton.textFg,
			st::boxTextFgError,
			error));
	}
}

void EditScans::hideSpecialScanError(SpecialFile type) {
	toggleSpecialScanError(type, false);
}

auto EditScans::findSpecialScan(SpecialFile type) -> SpecialScan& {
	const auto i = _specialScans.find(type);
	Assert(i != end(_specialScans));
	return i->second;
}

void EditScans::toggleSpecialScanError(SpecialFile type, bool shown) {
	auto &scan = findSpecialScan(type);
	if (scan.errorShown != shown) {
		scan.errorShown = shown;
		scan.errorAnimation.start(
			[=] { specialScanErrorAnimationCallback(type); },
			scan.errorShown ? 0. : 1.,
			scan.errorShown ? 1. : 0.,
			st::passportDetailsField.duration);
	}
}

void EditScans::specialScanErrorAnimationCallback(SpecialFile type) {
	auto &scan = findSpecialScan(type);
	const auto error = scan.errorAnimation.current(
		scan.errorShown ? 1. : 0.);
	if (error == 0.) {
		scan.upload->setColorOverride(base::none);
	} else {
		scan.upload->setColorOverride(anim::color(
			st::passportUploadButton.textFg,
			st::boxTextFgError,
			error));
	}
}

EditScans::~EditScans() = default;

} // namespace Passport
