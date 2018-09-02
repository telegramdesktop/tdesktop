/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "old_settings/settings_background_widget.h"

#include "styles/style_old_settings.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "boxes/background_box.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "core/file_utilities.h"

namespace OldSettings {

BackgroundRow::BackgroundRow(QWidget *parent) : RpWidget(parent)
, _chooseFromGallery(this, lang(lng_settings_bg_from_gallery), st::boxLinkButton)
, _chooseFromFile(this, lang(lng_settings_bg_from_file), st::boxLinkButton)
, _editTheme(this, lang(lng_settings_bg_edit_theme), st::boxLinkButton)
, _radial(animation(this, &BackgroundRow::step_radial)) {
	updateImage();

	connect(_chooseFromGallery, SIGNAL(clicked()), this, SIGNAL(chooseFromGallery()));
	connect(_chooseFromFile, SIGNAL(clicked()), this, SIGNAL(chooseFromFile()));
	connect(_editTheme, SIGNAL(clicked()), this, SIGNAL(editTheme()));
	checkNonDefaultTheme();
	using Update = const Window::Theme::BackgroundUpdate;
	subscribe(Window::Theme::Background(), [this](Update &update) {
		if (update.type == Update::Type::ApplyingTheme
			|| update.type == Update::Type::New) {
			checkNonDefaultTheme();
		}
	});
}

void BackgroundRow::checkNonDefaultTheme() {
	if (Window::Theme::SuggestThemeReset()) {
		if (!_useDefaultTheme) {
			_useDefaultTheme.create(this, lang(lng_settings_bg_use_default), st::boxLinkButton);
			_useDefaultTheme->show();
			connect(_useDefaultTheme, SIGNAL(clicked()), this, SIGNAL(useDefault()));
			resizeToWidth(width());
		}
	} else if (_useDefaultTheme) {
		_useDefaultTheme.destroy();
		resizeToWidth(width());
	}
}

void BackgroundRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	bool radial = false;
	float64 radialOpacity = 0;
	if (_radial.animating()) {
		_radial.step(getms());
		radial = _radial.animating();
		radialOpacity = _radial.opacity();
	}
	if (radial) {
		auto backThumb = App::main() ? App::main()->newBackgroundThumb() : ImagePtr();
		if (backThumb->isNull()) {
			p.drawPixmap(0, 0, _background);
		} else {
			const QPixmap &pix = App::main()->newBackgroundThumb()->pixBlurred(Data::FileOrigin(), st::settingsBackgroundSize);
			p.drawPixmap(0, 0, st::settingsBackgroundSize, st::settingsBackgroundSize, pix, 0, (pix.height() - st::settingsBackgroundSize * cIntRetinaFactor()) / 2, st::settingsBackgroundSize * cIntRetinaFactor(), st::settingsBackgroundSize * cIntRetinaFactor());
		}

		auto outer = radialRect();
		QRect inner(QPoint(outer.x() + (outer.width() - st::radialSize.width()) / 2, outer.y() + (outer.height() - st::radialSize.height()) / 2), st::radialSize);
		p.setPen(Qt::NoPen);
		p.setOpacity(radialOpacity);
		p.setBrush(st::radialBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(1);
		QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
		_radial.draw(p, arc, st::radialLine, st::radialFg);
	} else {
		p.drawPixmap(0, 0, _background);
	}
}

int BackgroundRow::resizeGetHeight(int newWidth) {
	auto linkTop = 0;
	auto linkLeft = st::settingsBackgroundSize + st::settingsSmallSkip;
	auto linkWidth = newWidth - linkLeft;
	_chooseFromGallery->resizeToWidth(qMin(linkWidth, _chooseFromGallery->naturalWidth()));
	_chooseFromFile->resizeToWidth(qMin(linkWidth, _chooseFromFile->naturalWidth()));
	_editTheme->resizeToWidth(qMin(linkWidth, _editTheme->naturalWidth()));
	if (_useDefaultTheme) {
		_useDefaultTheme->resizeToWidth(qMin(linkWidth, _useDefaultTheme->naturalWidth()));
		_useDefaultTheme->moveToLeft(linkLeft, linkTop, newWidth);
		linkTop += _useDefaultTheme->height() + st::settingsSmallSkip;
	}
	_chooseFromGallery->moveToLeft(linkLeft, linkTop, newWidth);
	linkTop += _chooseFromGallery->height() + st::settingsSmallSkip;
	_chooseFromFile->moveToLeft(linkLeft, linkTop, newWidth);
	linkTop += _chooseFromFile->height() + st::settingsSmallSkip;
	_editTheme->moveToLeft(linkLeft, linkTop, newWidth);

	return st::settingsBackgroundSize;
}

float64 BackgroundRow::radialProgress() const {
	if (auto m = App::main()) {
		return m->chatBackgroundProgress();
	}
	return 1.;
}

bool BackgroundRow::radialLoading() const {
	if (auto m = App::main()) {
		if (m->chatBackgroundLoading()) {
			m->checkChatBackground();
			if (m->chatBackgroundLoading()) {
				return true;
			} else {
				const_cast<BackgroundRow*>(this)->updateImage();
			}
		}
	}
	return false;
}

QRect BackgroundRow::radialRect() const {
	return QRect(0, 0, st::settingsBackgroundSize, st::settingsBackgroundSize);
}

void BackgroundRow::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (auto shift = radialTimeShift()) {
			_radial.update(radialProgress(), !radialLoading(), getms() + shift);
		}
	}
}

TimeMs BackgroundRow::radialTimeShift() const {
	return st::radialDuration;
}

void BackgroundRow::step_radial(TimeMs ms, bool timer) {
	_radial.update(radialProgress(), !radialLoading(), ms + radialTimeShift());
	if (timer && _radial.animating()) {
		rtlupdate(radialRect());
	}
}

void BackgroundRow::updateImage() {
	int32 size = st::settingsBackgroundSize * cIntRetinaFactor();
	QImage back(size, size, QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&back);
		PainterHighQualityEnabler hq(p);

		auto &pix = Window::Theme::Background()->pixmap();
		int sx = (pix.width() > pix.height()) ? ((pix.width() - pix.height()) / 2) : 0;
		int sy = (pix.height() > pix.width()) ? ((pix.height() - pix.width()) / 2) : 0;
		int s = (pix.width() > pix.height()) ? pix.height() : pix.width();
		p.drawPixmap(0, 0, st::settingsBackgroundSize, st::settingsBackgroundSize, pix, sx, sy, s, s);
	}
	Images::prepareRound(back, ImageRoundRadius::Small);
	_background = App::pixmapFromImageInPlace(std::move(back));
	_background.setDevicePixelRatio(cRetinaFactor());

	rtlupdate(radialRect());

	if (radialLoading()) {
		radialStart();
	}
}

BackgroundWidget::BackgroundWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_background)) {
	createControls();

	using Update = Window::Theme::BackgroundUpdate;
	subscribe(Window::Theme::Background(), [this](const Update &update) {
		if (update.type == Update::Type::New) {
			_background->updateImage();
		} else if (update.type == Update::Type::Start
			|| update.type == Update::Type::Changed) {
			needBackgroundUpdate(update.tiled);
		}
	});
	subscribe(Adaptive::Changed(), [this]() {
		_adaptive->toggle(
			(Global::AdaptiveChatLayout() == Adaptive::ChatLayout::Wide),
			anim::type::normal);
	});
}

void BackgroundWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSmallSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));

	createChildRow(_background, margin);
	connect(_background, SIGNAL(chooseFromGallery()), this, SLOT(onChooseFromGallery()));
	connect(_background, SIGNAL(chooseFromFile()), this, SLOT(onChooseFromFile()));
	connect(_background, SIGNAL(editTheme()), this, SLOT(onEditTheme()));
	connect(_background, SIGNAL(useDefault()), this, SLOT(onUseDefaultTheme()));

	createChildRow(_tile, margin, lang(lng_settings_bg_tile), [this](bool) { onTile(); }, Window::Theme::Background()->tile());
	createChildRow(_adaptive, margin, slidedPadding, lang(lng_settings_adaptive_wide), [this](bool) { onAdaptive(); }, Global::AdaptiveForWide());
	if (Global::AdaptiveChatLayout() != Adaptive::ChatLayout::Wide) {
		_adaptive->hide(anim::type::instant);
	}
}

void BackgroundWidget::onChooseFromGallery() {
	Ui::show(Box<BackgroundBox>());
}

void BackgroundWidget::needBackgroundUpdate(bool tile) {
	_tile->setChecked(tile);
	_background->updateImage();
}

void BackgroundWidget::onChooseFromFile() {
	auto imgExtensions = cImgExtensions();
	auto filters = QStringList(qsl("Theme files (*.tdesktop-theme *.tdesktop-palette *") + imgExtensions.join(qsl(" *")) + qsl(")"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = [=](const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.paths.isEmpty()) {
			auto filePath = result.paths.front();
			if (filePath.endsWith(qstr(".tdesktop-theme"), Qt::CaseInsensitive)
				|| filePath.endsWith(qstr(".tdesktop-palette"), Qt::CaseInsensitive)) {
				Window::Theme::Apply(filePath);
				return;
			}
		}

		QImage img;
		if (!result.remoteContent.isEmpty()) {
			img = App::readImage(result.remoteContent);
		} else {
			img = App::readImage(result.paths.front());
		}

		if (img.isNull() || img.width() <= 0 || img.height() <= 0) return;

		if (img.width() > 4096 * img.height()) {
			img = img.copy((img.width() - 4096 * img.height()) / 2, 0, 4096 * img.height(), img.height());
		} else if (img.height() > 4096 * img.width()) {
			img = img.copy(0, (img.height() - 4096 * img.width()) / 2, img.width(), 4096 * img.width());
		}

		Window::Theme::Background()->setImage(Window::Theme::kCustomBackground, std::move(img));
		_tile->setChecked(false);
		_background->updateImage();
	};
	FileDialog::GetOpenPath(
		this,
		lang(lng_choose_image),
		filters.join(qsl(";;")),
		crl::guard(this, callback));
}

void BackgroundWidget::onEditTheme() {
	Window::Theme::Editor::Start();
}

void BackgroundWidget::onUseDefaultTheme() {
	Window::Theme::ApplyDefault();
}

void BackgroundWidget::onTile() {
	Window::Theme::Background()->setTile(_tile->checked());
}

void BackgroundWidget::onAdaptive() {
	if (Global::AdaptiveForWide() != _adaptive->entity()->checked()) {
		Global::SetAdaptiveForWide(_adaptive->entity()->checked());
		Adaptive::Changed().notify();
		Local::writeUserSettings();
	}
}

} // namespace Settings
