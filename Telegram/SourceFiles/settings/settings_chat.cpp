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
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/effects/radial_animation.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "lang/lang_keys.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "support/support_common.h"
#include "support/support_templates.h"
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

class DefaultTheme final : public Ui::AbstractCheckView {
public:
	enum class Type {
		DayBlue,
		Default,
		Night,
		NightGreen,
	};
	struct Scheme {
		Type type = Type();
		QColor background;
		QColor sent;
		QColor received;
		QColor radiobuttonInactive;
		QColor radiobuttonActive;
		QString name;
		QString path;
	};
	DefaultTheme(Scheme scheme, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	void checkedChangedHook(anim::type animated) override;

	Scheme _scheme;
	Ui::RadioView _radio;

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
	const auto updated = _radial.update(
		radialProgress(),
		!radialLoading(),
		ms + radialTimeShift());
	if (timer && _radial.animating() && (!anim::Disabled() || updated)) {
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

DefaultTheme::DefaultTheme(Scheme scheme, bool checked)
: AbstractCheckView(st::defaultRadio.duration, checked, nullptr)
, _scheme(scheme)
, _radio(st::defaultRadio, checked, [=] { update(); }) {
	_radio.setToggledOverride(_scheme.radiobuttonActive);
	_radio.setUntoggledOverride(_scheme.radiobuttonInactive);
}

QSize DefaultTheme::getSize() const {
	return st::settingsThemePreviewSize;
}

void DefaultTheme::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) {
	const auto received = QRect(
		st::settingsThemeBubblePosition,
		st::settingsThemeBubbleSize);
	const auto sent = QRect(
		outerWidth - received.width() - st::settingsThemeBubblePosition.x(),
		received.y() + received.height() + st::settingsThemeBubbleSkip,
		received.width(),
		received.height());
	const auto radius = st::settingsThemeBubbleRadius;

	p.fillRect(
		QRect(QPoint(), st::settingsThemePreviewSize),
		_scheme.background);

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_scheme.received);
	p.drawRoundedRect(rtlrect(received, outerWidth), radius, radius);
	p.setBrush(_scheme.sent);
	p.drawRoundedRect(rtlrect(sent, outerWidth), radius, radius);

	const auto radio = _radio.getSize();
	_radio.paint(
		p,
		(outerWidth - radio.width()) / 2,
		getSize().height() - radio.height() - st::settingsThemeRadioBottom,
		outerWidth,
		getms());
}

QImage DefaultTheme::prepareRippleMask() const {
	return QImage();
}

bool DefaultTheme::checkRippleStartPosition(QPoint position) const {
	return false;
}

void DefaultTheme::checkedChangedHook(anim::type animated) {
	_radio.setChecked(checked(), animated);
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
	AddDivider(container);
	AddSkip(container);

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
		st::settingsChatButton,
		&st::settingsIconStickers,
		st::settingsChatIconLeft
	)->addClickHandler([] {
		Ui::show(Box<StickersBox>(StickersBox::Section::Installed));
	});

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupMessages(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, lng_settings_messages);

	AddSkip(container, st::settingsSendTypeSkip);

	using SendByType = Ui::InputSubmitSettings;

	const auto skip = st::settingsSendTypeSkip;
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	const auto group = std::make_shared<Ui::RadioenumGroup<SendByType>>(
		Auth().settings().sendSubmitWay());
	const auto add = [&](SendByType value, LangKey key) {
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
	add(SendByType::Enter, lng_settings_send_enter);
	add(
		SendByType::CtrlEnter,
		((cPlatform() == dbipMac || cPlatform() == dbipMacOld)
			? lng_settings_send_cmdenter
			: lng_settings_send_ctrlenter));

	group->setChangedCallback([](SendByType value) {
		Auth().settings().setSendSubmitWay(value);
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
		lng_settings_manage_local_storage,
		st::settingsButton
	)->addClickHandler([] {
		LocalStorageBox::Show(&Auth().data().cache());
	});
}

void SetupDataStorage(not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, lng_settings_data_storage);

	const auto ask = AddButton(
		container,
		lng_download_path_ask,
		st::settingsButton
	)->toggleOn(rpl::single(Global::AskDownloadPath()));

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
	path->toggleOn(ask->toggledValue() | rpl::map(!_1));
#endif // OS_WIN_STORE

	ask->toggledValue(
	) | rpl::filter([](bool checked) {
		return (checked != Global::AskDownloadPath());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetAskDownloadPath(checked);
		Local::writeUserSettings();

#ifndef OS_WIN_STORE
		showpath->fire_copy(!checked);
#endif // OS_WIN_STORE

	}, ask->lifetime());

	AddButton(
		container,
		lng_media_auto_settings,
		st::settingsButton
	)->addClickHandler([] {
		Ui::show(Box<AutoDownloadBox>());
	});

	SetupLocalStorage(container);
	SetupExport(container);

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupChatBackground(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

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

void SetupDefaultThemes(not_null<Ui::VerticalLayout*> container) {
	using Type = DefaultTheme::Type;
	using Scheme = DefaultTheme::Scheme;
	const auto block = container->add(object_ptr<Ui::FixedHeightWidget>(
		container));
	const auto scheme = DefaultTheme::Scheme();
	const auto color = [](str_const hex) {
		Expects(hex.size() == 6);

		const auto component = [](char a, char b) {
			const auto convert = [](char ch) {
				Expects((ch >= '0' && ch <= '9')
					|| (ch >= 'A' && ch <= 'F')
					|| (ch >= 'a' && ch <= 'f'));

				return (ch >= '0' && ch <= '9')
					? int(ch - '0')
					: int(ch - ((ch >= 'A' && ch <= 'F') ? 'A' : 'a') + 10);
			};
			return convert(a) * 16 + convert(b);
		};

		return QColor(
			component(hex[0], hex[1]),
			component(hex[2], hex[3]),
			component(hex[4], hex[5]));
	};
	static const auto schemes = {
		Scheme{
			Type::DayBlue,
			color("7ec4ea"),
			color("d7f0ff"),
			color("ffffff"),
			color("d7f0ff"),
			color("ffffff"),
			"Blue",
			":/gui/day-blue.tdesktop-theme"
		},
		Scheme{
			Type::Default,
			color("90ce89"),
			color("eaffdc"),
			color("ffffff"),
			color("eaffdc"),
			color("ffffff"),
			"Classic",
			QString()
		},
		Scheme{
			Type::Night,
			color("485761"),
			color("5ca7d4"),
			color("6b808d"),
			color("6b808d"),
			color("5ca7d4"),
			"Midnight",
			":/gui/night.tdesktop-theme"
		},
		Scheme{
			Type::NightGreen,
			color("485761"),
			color("74bf93"),
			color("6b808d"),
			color("6b808d"),
			color("74bf93"),
			"Matrix",
			":/gui/night-green.tdesktop-theme"
		},
	};
	const auto chosen = [&] {
		if (Window::Theme::IsNonDefaultBackground()) {
			return Type(-1);
		}
		const auto path = Window::Theme::Background()->themeAbsolutePath();
		for (const auto scheme : schemes) {
			if (path == scheme.path) {
				return scheme.type;
			}
		}
		return Type(-1);
	};
	const auto group = std::make_shared<Ui::RadioenumGroup<Type>>(chosen());
	auto buttons = ranges::view::all(
		schemes
	) | ranges::view::transform([&](const Scheme &scheme) {
		auto check = std::make_unique<DefaultTheme>(scheme, false);
		const auto weak = check.get();
		const auto result = Ui::CreateChild<Ui::Radioenum<Type>>(
			block,
			group,
			scheme.type,
			scheme.name,
			st::settingsTheme,
			std::move(check));
		weak->setUpdateCallback([=] { result->update(); });
		return result;
	}) | ranges::to_vector;

	using Update = const Window::Theme::BackgroundUpdate;
	const auto apply = [=](const Scheme &scheme) {
		const auto isNight = [](const Scheme &scheme) {
			const auto type = scheme.type;
			return (type != Type::DayBlue) && (type != Type::Default);
		};
		const auto currentlyIsCustom = (chosen() == Type(-1));
		if (Window::Theme::IsNightMode() == isNight(scheme)) {
			Window::Theme::ApplyDefaultWithPath(scheme.path);
		} else {
			Window::Theme::ToggleNightMode(scheme.path);
		}
		if (!currentlyIsCustom) {
			Window::Theme::KeepApplied();
		}
	};
	group->setChangedCallback([=](Type type) {
		const auto i = ranges::find_if(schemes, [&](const Scheme &scheme) {
			return (type == scheme.type && type != chosen());
		});
		if (i != end(schemes)) {
			apply(*i);
		}
	});
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::ApplyingTheme
			|| update.type == Update::Type::New);
	}) | rpl::map([=] {
		return chosen();
	}) | rpl::start_with_next([=](Type type) {
		group->setValue(type);
	}, container->lifetime());

	for (const auto button : buttons) {
		button->setCheckAlignment(style::al_top);
		button->resizeToWidth(button->width());
	}
	block->resize(block->width(), buttons[0]->height());
	block->widthValue(
	) | rpl::start_with_next([buttons = std::move(buttons)](int width) {
		Expects(!buttons.empty());

		//     |------|      |---------|        |-------|      |-------|
		// pad | blue | skip | classic | 3*skip | night | skip | night | pad
		//     |------|      |---------|        |-------|      |-------|
		const auto padding = st::settingsButton.padding;
		width -= padding.left() + padding.right();
		const auto desired = st::settingsThemePreviewSize.width();
		const auto count = int(buttons.size());
		const auto smallSkips = (count / 2);
		const auto bigSkips = ((count - 1) / 2);
		const auto skipRatio = 3;
		const auto skipSegments = smallSkips + bigSkips * skipRatio;
		const auto minSkip = st::settingsThemeMinSkip;
		const auto single = [&] {
			if (width >= skipSegments * minSkip + count * desired) {
				return desired;
			}
			return (width - skipSegments * minSkip) / count;
		}();
		if (single <= 0) {
			return;
		}
		const auto fullSkips = width - count * single;
		const auto segment = fullSkips / float64(skipSegments);
		const auto smallSkip = segment;
		const auto bigSkip = segment * skipRatio;
		auto left = padding.left() + 0.;
		auto index = 0;
		for (const auto button : buttons) {
			button->resizeToWidth(single);
			button->moveToLeft(int(std::round(left)), 0);
			left += button->width() + ((index++ % 2) ? bigSkip : smallSkip);
		}
	}, block->lifetime());

	AddSkip(container);
}

void SetupThemeOptions(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsPrivacySkip);

	AddSubsectionTitle(container, lng_settings_themes);

	AddSkip(container, st::settingsThemesTopSkip);
	SetupDefaultThemes(container);
	AddSkip(container, st::settingsThemesBottomSkip);

	AddButton(
		container,
		lng_settings_bg_edit_theme,
		st::settingsChatButton,
		&st::settingsIconThemes,
		st::settingsChatIconLeft
	)->addClickHandler(App::LambdaDelayed(
		st::settingsChatButton.ripple.hideDuration,
		container,
		[] { Window::Theme::Editor::Start(); }));

	AddSkip(container);
}

void SetupSupport(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);

	AddSubsectionTitle(container, rpl::single(qsl("Support settings")));

	AddSkip(container, st::settingsSendTypeSkip);

	using SwitchType = Support::SwitchSettings;

	const auto skip = st::settingsSendTypeSkip;
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	const auto group = std::make_shared<Ui::RadioenumGroup<SwitchType>>(
		Auth().settings().supportSwitch());
	const auto add = [&](SwitchType value, const QString &label) {
		inner->add(
			object_ptr<Ui::Radioenum<SwitchType>>(
				inner,
				group,
				value,
				label,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	add(SwitchType::None, "Just send the reply");
	add(SwitchType::Next, "Send and switch to next");
	add(SwitchType::Previous, "Send and switch to previous");
	group->setChangedCallback([](SwitchType value) {
		Auth().settings().setSupportSwitch(value);
		Local::writeUserSettings();
	});

	AddSkip(inner, st::settingsCheckboxesSkip);

	base::ObservableViewer(
		inner->add(
			object_ptr<Ui::Checkbox>(
				inner,
				"Enable templates autocomplete",
				Auth().settings().supportTemplatesAutocomplete(),
				st::settingsCheckbox),
			st::settingsSendTypePadding
		)->checkedChanged
	) | rpl::start_with_next([=](bool checked) {
		Auth().settings().setSupportTemplatesAutocomplete(checked);
		Local::writeUserSettings();
	}, inner->lifetime());

	AddSkip(inner, st::settingsCheckboxesSkip);
	AddSkip(inner);
}

Chat::Chat(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void Chat::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupThemeOptions(content);
	SetupChatBackground(content);
	SetupStickersEmoji(content);
	SetupMessages(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
