/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_chat.h"

#include "settings/settings_common.h"
#include "boxes/connection_box.h"
#include "boxes/stickers_box.h"
#include "boxes/background_box.h"
#include "boxes/download_path_box.h"
#include "boxes/local_storage_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/effects/radial_animation.h"
#include "lang/lang_keys.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Settings {

class BackgroundRow : public Ui::RpWidget {
public:
	BackgroundRow(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	void updateImage();

	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	TimeMs radialTimeShift() const;
	void step_radial(TimeMs ms, bool timer);

	QPixmap _background;
	object_ptr<Ui::LinkButton> _chooseFromGallery;
	object_ptr<Ui::LinkButton> _chooseFromFile;

	Ui::RadialAnimation _radial;

};

void ChooseFromFile(not_null<QWidget*> parent);

BackgroundRow::BackgroundRow(QWidget *parent) : RpWidget(parent)
, _chooseFromGallery(
	this,
	lang(lng_settings_bg_from_gallery),
	st::settingsLink)
, _chooseFromFile(this, lang(lng_settings_bg_from_file), st::settingsLink)
, _radial(animation(this, &BackgroundRow::step_radial)) {
	updateImage();

	_chooseFromGallery->addClickHandler([] {
		Ui::show(Box<BackgroundBox>());
	});
	_chooseFromFile->addClickHandler([=] {
		ChooseFromFile(this);
	});

	using Update = const Window::Theme::BackgroundUpdate;
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::New
			|| update.type == Update::Type::Start
			|| update.type == Update::Type::Changed);
	}) | rpl::start_with_next([=] {
		updateImage();
	}, lifetime());
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
		const auto backThumb = App::main()->newBackgroundThumb();
		if (backThumb->isNull()) {
			p.drawPixmap(0, 0, _background);
		} else {
			const auto &pix = backThumb->pixBlurred(
				Data::FileOrigin(),
				st::settingsBackgroundThumb);
			const auto factor = cIntRetinaFactor();
			p.drawPixmap(
				0,
				0,
				st::settingsBackgroundThumb,
				st::settingsBackgroundThumb,
				pix,
				0,
				(pix.height() - st::settingsBackgroundThumb * factor) / 2,
				st::settingsBackgroundThumb * factor,
				st::settingsBackgroundThumb * factor);
		}

		const auto outer = radialRect();
		const auto inner = QRect(
			QPoint(
				outer.x() + (outer.width() - st::radialSize.width()) / 2,
				outer.y() + (outer.height() - st::radialSize.height()) / 2),
			st::radialSize);
		p.setPen(Qt::NoPen);
		p.setOpacity(radialOpacity);
		p.setBrush(st::radialBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(1);
		const auto arc = inner.marginsRemoved(QMargins(
			st::radialLine,
			st::radialLine,
			st::radialLine,
			st::radialLine));
		_radial.draw(p, arc, st::radialLine, st::radialFg);
	} else {
		p.drawPixmap(0, 0, _background);
	}
}

int BackgroundRow::resizeGetHeight(int newWidth) {
	auto linkTop = st::settingsFromGalleryTop;
	auto linkLeft = st::settingsBackgroundThumb + st::settingsThumbSkip;
	auto linkWidth = newWidth - linkLeft;
	_chooseFromGallery->resizeToWidth(
		qMin(linkWidth, _chooseFromGallery->naturalWidth()));
	_chooseFromFile->resizeToWidth(
		qMin(linkWidth, _chooseFromFile->naturalWidth()));
	_chooseFromGallery->moveToLeft(linkLeft, linkTop, newWidth);
	linkTop += _chooseFromGallery->height() + st::settingsFromFileTop;
	_chooseFromFile->moveToLeft(linkLeft, linkTop, newWidth);
	return st::settingsBackgroundThumb;
}

float64 BackgroundRow::radialProgress() const {
	return App::main()->chatBackgroundProgress();
}

bool BackgroundRow::radialLoading() const {
	const auto main = App::main();
	if (main->chatBackgroundLoading()) {
		main->checkChatBackground();
		if (main->chatBackgroundLoading()) {
			return true;
		} else {
			const_cast<BackgroundRow*>(this)->updateImage();
		}
	}
	return false;
}

QRect BackgroundRow::radialRect() const {
	return QRect(
		0,
		0,
		st::settingsBackgroundThumb,
		st::settingsBackgroundThumb);
}

void BackgroundRow::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (const auto shift = radialTimeShift()) {
			_radial.update(
				radialProgress(),
				!radialLoading(),
				getms() + shift);
		}
	}
}

TimeMs BackgroundRow::radialTimeShift() const {
	return st::radialDuration;
}

void BackgroundRow::step_radial(TimeMs ms, bool timer) {
	_radial.update(
		radialProgress(),
		!radialLoading(),
		ms + radialTimeShift());
	if (timer && _radial.animating()) {
		rtlupdate(radialRect());
	}
}

void BackgroundRow::updateImage() {
	int32 size = st::settingsBackgroundThumb * cIntRetinaFactor();
	QImage back(size, size, QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&back);
		PainterHighQualityEnabler hq(p);

		const auto &pix = Window::Theme::Background()->pixmap();
		const auto sx = (pix.width() > pix.height())
			? ((pix.width() - pix.height()) / 2)
			: 0;
		const auto sy = (pix.height() > pix.width())
			? ((pix.height() - pix.width()) / 2)
			: 0;
		const auto s = (pix.width() > pix.height())
			? pix.height()
			: pix.width();
		p.drawPixmap(
			0,
			0,
			st::settingsBackgroundThumb,
			st::settingsBackgroundThumb,
			pix,
			sx,
			sy,
			s,
			s);
	}
	Images::prepareRound(back, ImageRoundRadius::Small);
	_background = App::pixmapFromImageInPlace(std::move(back));
	_background.setDevicePixelRatio(cRetinaFactor());

	rtlupdate(radialRect());

	if (radialLoading()) {
		radialStart();
	}
}

void ChooseFromFile(not_null<QWidget*> parent) {
	const auto imgExtensions = cImgExtensions();
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette *")
		+ imgExtensions.join(qsl(" *"))
		+ qsl(")"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = [=](const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.paths.isEmpty()) {
			const auto filePath = result.paths.front();
			const auto hasExtension = [&](QLatin1String extension) {
				return filePath.endsWith(extension, Qt::CaseInsensitive);
			};
			if (hasExtension(qstr(".tdesktop-theme"))
				|| hasExtension(qstr(".tdesktop-palette"))) {
				Window::Theme::Apply(filePath);
				return;
			}
		}

		auto image = result.remoteContent.isEmpty()
			? App::readImage(result.paths.front())
			: App::readImage(result.remoteContent);
		if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
			return;
		} else if (image.width() > 4096 * image.height()) {
			image = image.copy(
				(image.width() - 4096 * image.height()) / 2,
				0,
				4096 * image.height(),
				image.height());
		} else if (image.height() > 4096 * image.width()) {
			image = image.copy(
				0,
				(image.height() - 4096 * image.width()) / 2,
				image.width(),
				4096 * image.width());
		}

		Window::Theme::Background()->setImage(
			Window::Theme::kCustomBackground,
			std::move(image));
		Window::Theme::Background()->setTile(false);
	};
	FileDialog::GetOpenPath(
		parent.get(),
		lang(lng_choose_image),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));

}

QString DownloadPathText() {
	if (Global::DownloadPath().isEmpty()) {
		return lang(lng_download_path_default);
	} else if (Global::DownloadPath() == qsl("tmp")) {
		return lang(lng_download_path_temp);
	}
	return QDir::toNativeSeparators(Global::DownloadPath());
}

void SetupStickersEmoji(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsStickersEmojiPadding);

	AddSubsectionTitle(container, lng_settings_stickers_emoji);

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap),
		QMargins(0, 0, 0, st::settingsCheckbox.margin.bottom())));

	const auto checkbox = [&](LangKey label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			lang(label),
			checked,
			st::settingsCheckbox);
	};
	const auto add = [&](LangKey label, bool checked, auto &&handle) {
		base::ObservableViewer(
			inner->add(
				checkbox(label, checked),
				st::settingsCheckboxPadding
			)->checkedChanged
		) | rpl::start_with_next(
			std::move(handle),
			inner->lifetime());
	};
	add(
		lng_settings_replace_emojis,
		Global::ReplaceEmoji(),
		[](bool checked) {
			Global::SetReplaceEmoji(checked);
			Global::RefReplaceEmojiChanged().notify();
			Local::writeUserSettings();
		});

	add(
		lng_settings_suggest_emoji,
		Global::SuggestEmoji(),
		[](bool checked) {
			Global::SetSuggestEmoji(checked);
			Local::writeUserSettings();
		});

	add(
		lng_settings_suggest_by_emoji,
		Global::SuggestStickersByEmoji(),
		[](bool checked) {
			Global::SetSuggestStickersByEmoji(checked);
			Local::writeUserSettings();
		});

	AddButton(
		container,
		lng_stickers_you_have,
		st::settingsButton
	)->addClickHandler([] {
		Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
	});

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupMessages(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container, st::settingsSectionSkip);

	AddSubsectionTitle(container, lng_settings_messages);

	AddSkip(container, st::settingsSendTypeSkip);

	enum class SendByType {
		Enter,
		CtrlEnter,
	};

	const auto skip = st::settingsSendTypeSkip;
	const auto group = std::make_shared<Ui::RadioenumGroup<SendByType>>(
		cCtrlEnter() ? SendByType::CtrlEnter : SendByType::Enter);
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	const auto add = [&](
		SendByType value,
		LangKey key,
		style::margins padding) {
		inner->add(
			object_ptr<Ui::Radioenum<SendByType>>(
				inner,
				group,
				value,
				lang(key),
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	const auto small = st::settingsSendTypePadding;
	const auto top = skip;
	add(
		SendByType::Enter,
		lng_settings_send_enter,
		{ small.left(), small.top() + top, small.right(), small.bottom() });
	add(
		SendByType::CtrlEnter,
		((cPlatform() == dbipMac || cPlatform() == dbipMacOld)
			? lng_settings_send_cmdenter
			: lng_settings_send_ctrlenter),
		{ small.left(), small.top(), small.right(), small.bottom() + top });
	group->setChangedCallback([](SendByType value) {
		cSetCtrlEnter(value == SendByType::CtrlEnter);
		if (App::main()) {
			App::main()->ctrlEnterSubmitUpdated();
		}
		Local::writeUserSettings();
	});

	AddSkip(inner, st::settingsCheckboxesSkip);
}

void SetupExport(not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		lng_settings_export_data,
		st::settingsButton
	)->addClickHandler([] {
		Ui::hideSettingsAndLayer();
		App::CallDelayed(
			st::boxDuration,
			&Auth(),
			[] { Auth().data().startExport(); });
	});
}

void SetupLocalStorage(not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		lng_settings_local_storage,
		st::settingsButton
	)->addClickHandler([] {
		LocalStorageBox::Show(&Auth().data().cache());
	});
}

void SetupDataStorage(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container, st::settingsSectionSkip);

	AddSubsectionTitle(container, lng_settings_data_storage);

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap),
		QMargins(0, 0, 0, st::settingsCheckbox.margin.bottom())));

	const auto ask = inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			lang(lng_download_path_ask),
			Global::AskDownloadPath(),
			st::settingsCheckbox),
		st::settingsCheckboxPadding);

#ifndef OS_WIN_STORE
	const auto showpath = Ui::AttachAsChild(
		ask,
		rpl::event_stream<bool>());
	const auto path = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				Lang::Viewer(lng_download_path),
				st::settingsButton)));
	auto pathtext = rpl::single(
		rpl::empty_value()
	) | rpl::then(base::ObservableViewer(
		Global::RefDownloadPathChanged()
	)) | rpl::map([] {
		return DownloadPathText();
	});
	CreateRightLabel(
		path->entity(),
		std::move(pathtext),
		st::settingsButton,
		lng_download_path);
	path->entity()->addClickHandler([] {
		Ui::show(Box<DownloadPathBox>());
	});
	path->toggleOn(
		showpath->events_starting_with_copy(!Global::AskDownloadPath()));
#endif // OS_WIN_STORE

	base::ObservableViewer(
		ask->checkedChanged
	) | rpl::start_with_next([=](bool checked) {
		Global::SetAskDownloadPath(checked);
		Local::writeUserSettings();

#ifndef OS_WIN_STORE
		showpath->fire_copy(!checked);
#endif // OS_WIN_STORE

	}, inner->lifetime());

	AddButton(
		container,
		lng_media_auto_settings,
		st::settingsButton
	)->addClickHandler([] {
		Ui::show(Box<AutoDownloadBox>());
	});

	SetupExport(container);
	SetupLocalStorage(container);

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupChatBackground(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container, st::settingsSectionSkip);

	AddSubsectionTitle(container, lng_settings_section_background);

	container->add(
		object_ptr<BackgroundRow>(container),
		st::settingsBackgroundPadding);

	const auto skipTop = st::settingsCheckbox.margin.top();
	const auto skipBottom = st::settingsCheckbox.margin.bottom();
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skipTop, 0, skipBottom)));

	AddSkip(container, st::settingsTileSkip);

	const auto tile = inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			lang(lng_settings_bg_tile),
			Window::Theme::Background()->tile(),
			st::settingsCheckbox),
		st::settingsSendTypePadding);
	const auto adaptive = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				lang(lng_settings_adaptive_wide),
				Global::AdaptiveForWide(),
				st::settingsCheckbox),
			st::settingsSendTypePadding));

	base::ObservableViewer(
		tile->checkedChanged
	) | rpl::start_with_next([](bool checked) {
		Window::Theme::Background()->setTile(checked);
	}, tile->lifetime());

	using Update = const Window::Theme::BackgroundUpdate;
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::Changed);
	}) | rpl::map([] {
		return Window::Theme::Background()->tile();
	}) | rpl::start_with_next([=](bool tiled) {
		tile->setChecked(tiled);
	}, tile->lifetime());

	adaptive->toggleOn(rpl::single(
		rpl::empty_value()
	) | rpl::then(base::ObservableViewer(
		Adaptive::Changed()
	)) | rpl::map([] {
		return (Global::AdaptiveChatLayout() == Adaptive::ChatLayout::Wide);
	}));

	base::ObservableViewer(
		adaptive->entity()->checkedChanged
	) | rpl::start_with_next([](bool checked) {
		Global::SetAdaptiveForWide(checked);
		Adaptive::Changed().notify();
		Local::writeUserSettings();
	}, adaptive->lifetime());
}

void SetupNightMode(not_null<Ui::VerticalLayout*> container) {
	const auto calling = Ui::AttachAsChild(container, 0);
	AddButton(
		container,
		lng_settings_use_night_mode,
		st::settingsButton
	)->toggleOn(
		rpl::single(Window::Theme::IsNightMode())
	)->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		++*calling;
		const auto change = [=] {
			if (!--*calling && toggled != Window::Theme::IsNightMode()) {
				Window::Theme::ToggleNightMode();
			}
		};
		App::CallDelayed(
			st::settingsButton.toggle.duration,
			container,
			change);
	}, container->lifetime());
}

void SetupUseDefaultTheme(not_null<Ui::VerticalLayout*> container) {
	using Update = const Window::Theme::BackgroundUpdate;
	container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				Lang::Viewer(lng_settings_bg_use_default),
				st::settingsButton))
	)->toggleOn(rpl::single(
		Window::Theme::SuggestThemeReset()
	) | rpl::then(base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::ApplyingTheme
			|| update.type == Update::Type::New);
	}) | rpl::map([] {
		return Window::Theme::SuggestThemeReset();
	})))->entity()->addClickHandler([] {
		Window::Theme::ApplyDefault();
	});
}

void SetupThemeOptions(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, lng_settings_themes);

	SetupNightMode(container);

	AddButton(
		container,
		lng_settings_bg_edit_theme,
		st::settingsButton
	)->addClickHandler(App::LambdaDelayed(
		st::settingsButton.ripple.hideDuration,
		container,
		[] { Window::Theme::Editor::Start(); }));

	SetupUseDefaultTheme(container);

	AddSkip(container);
}

Chat::Chat(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void Chat::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupStickersEmoji(content);
	SetupMessages(content);
	SetupChatBackground(content);
	SetupThemeOptions(content);
	SetupDataStorage(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
