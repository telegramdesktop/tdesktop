/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_edit_identity.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_details_row.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/text_options.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {

class ScanButton : public Ui::AbstractButton {
public:
	ScanButton(
		QWidget *parent,
		const style::PassportScanRow &st,
		const QString &name,
		const QString &status,
		bool deleted);

	void setImage(const QImage &image);
	void setStatus(const QString &status);
	void setDeleted(bool deleted);

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
	QImage _image;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _delete;
	object_ptr<Ui::FadeWrapScaled<Ui::RoundButton>> _restore;

};

ScanButton::ScanButton(
	QWidget *parent,
	const style::PassportScanRow &st,
	const QString &name,
	const QString &status,
	bool deleted)
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

	if (_restore->toggled()) {
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
	p.setPen(st::windowSubTextFg);
	_status.drawLeftElided(
		p,
		left + _st.textLeft,
		top + _st.statusTop,
		availableWidth,
		width());
}

PanelEditIdentity::PanelEditIdentity(
	QWidget*,
	not_null<PanelController*> controller,
	const ValueMap &data,
	const ValueMap &scanData,
	std::vector<ScanInfo> &&files)
: _controller(controller)
, _files(std::move(files))
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		langFactory(lng_passport_save_value),
		st::passportPanelSaveValue) {
	setupControls(data, scanData);
}

void PanelEditIdentity::setupControls(
		const ValueMap &data,
		const ValueMap &scanData) {
	const auto inner = setupContent(data, scanData);

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_done->addClickHandler([=] {
		crl::on_main(this, [=] {
			save();
		});
	});
	_controller->scanUpdated(
	) | rpl::start_with_next([=](ScanInfo &&info) {
		updateScan(std::move(info));
	}, lifetime());
}

not_null<Ui::RpWidget*> PanelEditIdentity::setupContent(
		const ValueMap &data,
		const ValueMap &scanData) {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	_scansDivider = inner->add(
		object_ptr<Ui::SlideWrap<BoxContentDivider>>(
			inner,
			object_ptr<BoxContentDivider>(
				inner,
				st::passportFormDividerHeight)));
	_scansDivider->toggle(_files.empty(), anim::type::instant);

	_scansHeader = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				lang(lng_passport_upload_header),
				Ui::FlatLabel::InitType::Simple,
				st::passportFormHeader),
			st::passportUploadHeaderPadding));
	_scansHeader->toggle(!_files.empty(), anim::type::instant);

	_scansWrap = inner->add(object_ptr<Ui::VerticalLayout>(inner));
	for (const auto &scan : _files) {
		pushScan(scan);
		_scans.back()->show(anim::type::instant);
	}

	_scansUpload = inner->add(
		object_ptr<Info::Profile::Button>(
			inner,
			_scansUploadTexts.events_starting_with(
				uploadButtonText()
			) | rpl::flatten_latest(),
			st::passportUploadButton),
		st::passportUploadButtonPadding);
	_scansUpload->addClickHandler([=] {
		chooseScan();
	});

	inner->add(object_ptr<BoxContentDivider>(
		inner,
		st::passportFormDividerHeight));
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			lang(lng_passport_personal_details),
			Ui::FlatLabel::InitType::Simple,
			st::passportFormHeader),
		st::passportDetailsHeaderPadding);

	const auto valueOrEmpty = [&](const QString &key) {
		if (const auto i = data.fields.find(key); i != data.fields.end()) {
			return i->second;
		}
		return QString();
	};

	_firstName = inner->add(object_ptr<PanelDetailsRow>(
		inner,
		lang(lng_passport_first_name),
		valueOrEmpty("first_name")))->field();
	_lastName = inner->add(object_ptr<PanelDetailsRow>(
		inner,
		lang(lng_passport_last_name),
		valueOrEmpty("last_name")))->field();

	return inner;
}

void PanelEditIdentity::updateScan(ScanInfo &&info) {
	const auto i = ranges::find(_files, info.key, [](const ScanInfo &file) {
		return file.key;
	});
	if (i != _files.end()) {
		*i = info;
		const auto scan = _scans[i - _files.begin()]->entity();
		scan->setStatus(i->status);
		scan->setImage(i->thumb);
		scan->setDeleted(i->deleted);
	} else {
		_files.push_back(std::move(info));
		pushScan(_files.back());
		_scansWrap->resizeToWidth(width());
		_scans.back()->show(anim::type::normal);
		_scansDivider->hide(anim::type::normal);
		_scansHeader->show(anim::type::normal);
		_scansUploadTexts.fire(uploadButtonText());
	}
}

void PanelEditIdentity::pushScan(const ScanInfo &info) {
	const auto index = _scans.size();
	_scans.push_back(base::unique_qptr<Ui::SlideWrap<ScanButton>>(
		_scansWrap->add(object_ptr<Ui::SlideWrap<ScanButton>>(
			_scansWrap,
			object_ptr<ScanButton>(
				_scansWrap,
				st::passportScanRow,
				lng_passport_scan_index(lt_index, QString::number(index + 1)),
				info.status,
				info.deleted)))));
	_scans.back()->hide(anim::type::instant);
	const auto scan = _scans.back()->entity();
	scan->setImage(info.thumb);

	scan->deleteClicks(
	) | rpl::start_with_next([=] {
		_controller->deleteScan(index);
	}, scan->lifetime());

	scan->restoreClicks(
	) | rpl::start_with_next([=] {
		_controller->restoreScan(index);
	}, scan->lifetime());
}

void PanelEditIdentity::focusInEvent(QFocusEvent *e) {
	_firstName->setFocusFast();
}

void PanelEditIdentity::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelEditIdentity::updateControlsGeometry() {
	const auto submitTop = height() - _done->height();
	_scroll->setGeometry(0, 0, width(), submitTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_done->resizeToWidth(width());
	_done->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

void PanelEditIdentity::chooseScan() {
	const auto filter = FileDialog::AllFilesFilter()
		+ qsl(";;Image files (*")
		+ cImgExtensions().join(qsl(" *"))
		+ qsl(")");
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.size() == 1) {
			encryptScan(result.paths.front());
		} else if (!result.remoteContent.isEmpty()) {
			encryptScanContent(std::move(result.remoteContent));
		}
	};
	FileDialog::GetOpenPath(
		lang(lng_passport_choose_image),
		filter,
		base::lambda_guarded(this, callback));
}

void PanelEditIdentity::encryptScan(const QString &path) {
	encryptScanContent([&] {
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly)) {
			return QByteArray();
		}
		return f.readAll();
	}());
}

void PanelEditIdentity::encryptScanContent(QByteArray &&content) {
	_controller->uploadScan(std::move(content));
}

void PanelEditIdentity::save() {
	auto data = ValueMap();
	data.fields["first_name"] = _firstName->getLastText();
	data.fields["last_name"] = _lastName->getLastText();
	auto scanData = ValueMap();
	_controller->saveScope(std::move(data), std::move(scanData));
}

rpl::producer<QString> PanelEditIdentity::uploadButtonText() const {
	return Lang::Viewer(_files.empty()
		? lng_passport_upload_scans
		: lng_passport_upload_more) | Info::Profile::ToUpperValue();
}

} // namespace Passport
