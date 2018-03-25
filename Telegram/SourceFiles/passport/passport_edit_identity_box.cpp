/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_edit_identity_box.h"

#include "passport/passport_form_controller.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/buttons.h"
#include "ui/text_options.h"
#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {

class ScanButton : public Ui::RippleButton {
public:
	ScanButton(
		QWidget *parent,
		const QString &title,
		const QString &description);

	void setImage(const QImage &image);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	int countAvailableWidth() const;
	int countAvailableWidth(int newWidth) const;

	Text _title;
	Text _description;
	int _titleHeight = 0;
	int _descriptionHeight = 0;
	QImage _image;
	object_ptr<Ui::IconButton> _delete = { nullptr };

};

ScanButton::ScanButton(
	QWidget *parent,
	const QString &title,
	const QString &description)
: RippleButton(parent, st::passportRowRipple)
, _title(
	st::semiboldTextStyle,
	title,
	Ui::NameTextOptions(),
	st::boxWideWidth / 2)
, _description(
	st::defaultTextStyle,
	description,
	Ui::NameTextOptions(),
	st::boxWideWidth / 2)
, _delete(this, st::passportRowCheckbox) {
}

void ScanButton::setImage(const QImage &image) {
	_image = image;
	update();
}

int ScanButton::resizeGetHeight(int newWidth) {
	const auto availableWidth = countAvailableWidth(newWidth);
	_titleHeight = _title.countHeight(availableWidth);
	_descriptionHeight = _description.countHeight(availableWidth);
	const auto result = st::passportRowPadding.top()
		+ _titleHeight
		+ st::passportRowSkip
		+ _descriptionHeight
		+ st::passportRowPadding.bottom();
	const auto right = st::passportRowPadding.right();
	_delete->moveToRight(
		right,
		(result - _delete->height()) / 2,
		newWidth);
	return result;
}

int ScanButton::countAvailableWidth(int newWidth) const {
	return newWidth
		- st::passportRowPadding.left()
		- st::passportRowPadding.right()
		- _delete->width();
}

int ScanButton::countAvailableWidth() const {
	return countAvailableWidth(width());
}

void ScanButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	paintRipple(p, 0, 0, ms);

	auto left = st::passportRowPadding.left();
	auto availableWidth = countAvailableWidth();
	auto top = st::passportRowPadding.top();
	const auto size = height() - top - st::passportRowPadding.bottom();
	if (_image.isNull()) {
		p.fillRect(left, top, size, size, Qt::black);
	} else {
		PainterHighQualityEnabler hq(p);
		if (_image.width() > _image.height()) {
			auto newheight = size * _image.height() / _image.width();
			p.drawImage(QRect(left, top + (size - newheight) / 2, size, newheight), _image);
		} else {
			auto newwidth = size * _image.width() / _image.height();
			p.drawImage(QRect(left + (size - newwidth) / 2, top, newwidth, size), _image);
		}
	}
	left += size + st::passportRowPadding.left();
	availableWidth -= size + st::passportRowPadding.left();

	_title.drawLeft(p, left, top, availableWidth, width());
	top += _titleHeight + st::passportRowSkip;

	_description.drawLeft(p, left, top, availableWidth, width());
	top += _descriptionHeight + st::passportRowPadding.bottom();
}

IdentityBox::IdentityBox(
	QWidget*,
	not_null<FormController*> controller,
	int fieldIndex,
	const IdentityData &data,
	std::vector<ScanInfo> &&files)
: _controller(controller)
, _fieldIndex(fieldIndex)
, _files(std::move(files))
, _uploadScan(this, "Upload scans") // #TODO langs
, _name(
	this,
	st::defaultInputField,
	langFactory(lng_signup_firstname),
	data.name)
, _surname(
	this,
	st::defaultInputField,
	langFactory(lng_signup_lastname),
	data.surname) {
}

void IdentityBox::prepare() {
	setTitle(langFactory(lng_passport_identity_title));

	auto index = 0;
	auto height = st::contactPadding.top();
	for (const auto &scan : _files) {
		_scans.push_back(object_ptr<ScanButton>(this, QString("Scan %1").arg(++index), scan.date));
		_scans.back()->setImage(scan.thumb);
		_scans.back()->resizeToWidth(st::boxWideWidth);
		height += _scans.back()->height();
	}
	height += st::contactPadding.top()
		+ _uploadScan->height()
		+ st::contactSkip
		+ _name->height()
		+ st::contactSkip
		+ _surname->height()
		+ st::contactPadding.bottom()
		+ st::boxPadding.bottom();

	addButton(langFactory(lng_settings_save), [=] {
		save();
	});
	addButton(langFactory(lng_cancel), [=] {
		closeBox();
	});
	_controller->scanUpdated(
	) | rpl::start_with_next([=](ScanInfo &&info) {
		updateScan(std::move(info));
	}, lifetime());

	_uploadScan->addClickHandler([=] {
		chooseScan();
	});
	setDimensions(st::boxWideWidth, height);
}

void IdentityBox::updateScan(ScanInfo &&info) {
	const auto i = ranges::find(_files, info.key, [](const ScanInfo &file) {
		return file.key;
	});
	if (i != _files.end()) {
		*i = info;
		_scans[i - _files.begin()]->setImage(i->thumb);
	}
}

void IdentityBox::setInnerFocus() {
	_name->setFocusFast();
}

void IdentityBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_name->resize((width()
			- st::boxPadding.left()
			- st::boxPadding.right()),
		_name->height());
	_surname->resize(_name->width(), _surname->height());

	auto top = st::contactPadding.top();
	for (const auto &scan : _scans) {
		scan->moveToLeft(0, top);
		top += scan->height();
	}
	top += st::contactPadding.top();
	_uploadScan->moveToLeft(st::boxPadding.left(), top);
	top += _uploadScan->height() + st::contactSkip;
	_name->moveToLeft(st::boxPadding.left(), top);
	top += _name->height() + st::contactSkip;
	_surname->moveToLeft(st::boxPadding.left(), top);
}

void IdentityBox::chooseScan() {
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
		"Choose scan image",
		filter,
		base::lambda_guarded(this, callback));
}

void IdentityBox::encryptScan(const QString &path) {
	encryptScanContent([&] {
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly)) {
			return QByteArray();
		}
		return f.readAll();
	}());
}

void IdentityBox::encryptScanContent(QByteArray &&content) {
	_uploadScan->hide();
	_controller->uploadScan(_fieldIndex, std::move(content));
}

void IdentityBox::save() {
	auto data = IdentityData();
	data.name = _name->getLastText();
	data.surname = _surname->getLastText();
	_controller->saveFieldIdentity(_fieldIndex, data);
}

} // namespace Passport
