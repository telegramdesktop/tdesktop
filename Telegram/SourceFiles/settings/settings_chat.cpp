/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_chat.h"

#include "settings/settings_common.h"
#include "boxes/connection_box.h"
#include "boxes/auto_download_box.h"
#include "boxes/stickers_box.h"
#include "boxes/background_box.h"
#include "boxes/background_preview_box.h"
#include "boxes/download_path_box.h"
#include "boxes/local_storage_box.h"
#include "boxes/edit_color_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/effects/radial_animation.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "lang/lang_keys.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_session_controller.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "data/data_session.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "platform/platform_info.h"
#include "support/support_common.h"
#include "support/support_templates.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Settings {
namespace {

const auto kSchemesList = Window::Theme::EmbeddedThemes();
constexpr auto kCustomColorButtonParts = 7;

class ColorsPalette final {
public:
	using Type = Window::Theme::EmbeddedType;
	using Scheme = Window::Theme::EmbeddedScheme;

	explicit ColorsPalette(not_null<Ui::VerticalLayout*> container);

	void show(Type type);

	rpl::producer<QColor> selected() const;

private:
	class Button {
	public:
		Button(
			not_null<QWidget*> parent,
			std::vector<QColor> &&colors,
			bool selected);

		void moveToLeft(int x, int y);
		void update(std::vector<QColor> &&colors, bool selected);
		rpl::producer<> clicks() const;
		bool selected() const;
		QColor color() const;

	private:
		void paint();

		Ui::AbstractButton _widget;
		std::vector<QColor> _colors;
		Ui::Animations::Simple _selectedAnimation;
		bool _selected = false;

	};

	void show(
		not_null<const Scheme*> scheme,
		std::vector<QColor> &&colors,
		int selected);
	void selectCustom(not_null<const Scheme*> scheme);
	void updateInnerGeometry();

	not_null<Ui::SlideWrap<>*> _outer;
	std::vector<std::unique_ptr<Button>> _buttons;

	rpl::event_stream<QColor> _selected;

};

void PaintColorButton(Painter &p, QColor color, float64 selected) {
	const auto size = st::settingsAccentColorSize;
	const auto rect = QRect(0, 0, size, size);

	p.setBrush(color);
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);

	if (selected > 0.) {
		const auto startSkip = -st::settingsAccentColorLine / 2.;
		const auto endSkip = float64(st::settingsAccentColorSkip);
		const auto skip = startSkip + (endSkip - startSkip) * selected;
		auto pen = st::boxBg->p;
		pen.setWidth(st::settingsAccentColorLine);
		p.setBrush(Qt::NoBrush);
		p.setPen(pen);
		p.setOpacity(selected);
		p.drawEllipse(QRectF(rect).marginsRemoved({ skip, skip, skip, skip }));
	}
}

void PaintCustomButton(Painter &p, const std::vector<QColor> &colors) {
	Expects(colors.size() >= kCustomColorButtonParts);

	p.setPen(Qt::NoPen);

	const auto size = st::settingsAccentColorSize;
	const auto smallSize = size / 8.;
	const auto drawAround = [&](QPointF center, int index) {
		const auto where = QPointF{
			size * (1. + center.x()) / 2,
			size * (1. + center.y()) / 2
		};
		p.setBrush(colors[index]);
		p.drawEllipse(
			where.x() - smallSize,
			where.y() - smallSize,
			2 * smallSize,
			2 * smallSize);
	};
	drawAround(QPointF(), 0);
	for (auto i = 0; i != 6; ++i) {
		const auto angle = i * M_PI / 3.;
		const auto point = QPointF{ cos(angle), sin(angle) };
		const auto adjusted = point * (1. - (2 * smallSize / size));
		drawAround(adjusted, i + 1);
	}

}

ColorsPalette::Button::Button(
	not_null<QWidget*> parent,
	std::vector<QColor> &&colors,
	bool selected)
: _widget(parent.get())
, _colors(std::move(colors))
, _selected(selected) {
	_widget.show();
	_widget.resize(st::settingsAccentColorSize, st::settingsAccentColorSize);
	_widget.paintRequest(
	) | rpl::start_with_next([=] {
		paint();
	}, _widget.lifetime());
}

void ColorsPalette::Button::moveToLeft(int x, int y) {
	_widget.moveToLeft(x, y);
}

void ColorsPalette::Button::update(
		std::vector<QColor> &&colors,
		bool selected) {
	if (_colors != colors) {
		_colors = std::move(colors);
		_widget.update();
	}
	if (_selected != selected) {
		_selected = selected;
		_selectedAnimation.start(
			[=] { _widget.update(); },
			_selected ? 0. : 1.,
			_selected ? 1. : 0.,
			st::defaultRadio.duration * 2);
	}
}

rpl::producer<> ColorsPalette::Button::clicks() const {
	return _widget.clicks() | rpl::map([] { return rpl::empty_value(); });
}

bool ColorsPalette::Button::selected() const {
	return _selected;
}

QColor ColorsPalette::Button::color() const {
	Expects(_colors.size() == 1);

	return _colors.front();
}

void ColorsPalette::Button::paint() {
	Painter p(&_widget);
	PainterHighQualityEnabler hq(p);

	if (_colors.size() == 1) {
		PaintColorButton(
			p,
			_colors.front(),
			_selectedAnimation.value(_selected ? 1. : 0.));
	} else if (_colors.size() >= kCustomColorButtonParts) {
		PaintCustomButton(p, _colors);
	}
}

ColorsPalette::ColorsPalette(not_null<Ui::VerticalLayout*> container)
: _outer(container->add(
	object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::RpWidget>(container)))) {
	_outer->hide(anim::type::instant);

	const auto inner = _outer->entity();
	inner->widthValue(
	) | rpl::start_with_next([=] {
		updateInnerGeometry();
	}, inner->lifetime());
}

void ColorsPalette::show(Type type) {
	const auto scheme = ranges::find(kSchemesList, type, &Scheme::type);
	if (scheme == end(kSchemesList)) {
		_outer->hide(anim::type::instant);
		return;
	}
	auto list = Window::Theme::DefaultAccentColors(type);
	if (list.empty()) {
		_outer->hide(anim::type::instant);
		return;
	}
	list.insert(list.begin(), scheme->accentColor);
	const auto color = Core::App().settings().themesAccentColors().get(type);
	const auto current = color.value_or(scheme->accentColor);
	const auto i = ranges::find(list, current);
	if (i == end(list)) {
		list.back() = current;
	}
	const auto selected = std::clamp(
		int(i - begin(list)),
		0,
		int(list.size()) - 1);

	_outer->show(anim::type::instant);

	show(&*scheme, std::move(list), selected);

	const auto inner = _outer->entity();
	inner->resize(_outer->width(), inner->height());
	updateInnerGeometry();
}

void ColorsPalette::show(
		not_null<const Scheme*> scheme,
		std::vector<QColor> &&colors,
		int selected) {
	Expects(selected >= 0 && selected < colors.size());

	while (_buttons.size() > colors.size()) {
		_buttons.pop_back();
	}

	auto index = 0;
	const auto inner = _outer->entity();
	const auto pushButton = [&](std::vector<QColor> &&colors) {
		auto result = rpl::producer<>();
		const auto chosen = (index == selected);
		if (_buttons.size() > index) {
			_buttons[index]->update(std::move(colors), chosen);
		} else {
			_buttons.push_back(std::make_unique<Button>(
				inner,
				std::move(colors),
				chosen));
			result = _buttons.back()->clicks();
		}
		++index;
		return result;
	};
	for (const auto &color : colors) {
		auto clicks = pushButton({ color });
		if (clicks) {
			std::move(
				clicks
			) | rpl::map([=] {
				return _buttons[index - 1]->color();
			}) | rpl::start_with_next([=](QColor color) {
				_selected.fire_copy(color);
			}, inner->lifetime());
		}
	}

	auto clicks = pushButton(std::move(colors));
	if (clicks) {
		std::move(
			clicks
		) | rpl::start_with_next([=] {
			selectCustom(scheme);
		}, inner->lifetime());
	}
}

void ColorsPalette::selectCustom(not_null<const Scheme*> scheme) {
	const auto selected = ranges::find(_buttons, true, &Button::selected);
	Assert(selected != end(_buttons));

	const auto colorizer = Window::Theme::ColorizerFrom(
		*scheme,
		scheme->accentColor);
	auto box = Box<EditColorBox>(
		tr::lng_settings_theme_accent_title(tr::now),
		EditColorBox::Mode::HSL,
		(*selected)->color());
	box->setLightnessLimits(
		colorizer.lightnessMin,
		colorizer.lightnessMax);
	box->setSaveCallback(crl::guard(_outer, [=](QColor result) {
		_selected.fire_copy(result);
	}));
	Ui::show(std::move(box));
}

rpl::producer<QColor> ColorsPalette::selected() const {
	return _selected.events();
}

void ColorsPalette::updateInnerGeometry() {
	if (_buttons.size() < 2) {
		return;
	}
	const auto inner = _outer->entity();
	const auto size = st::settingsAccentColorSize;
	const auto padding = st::settingsButton.padding;
	const auto width = inner->width() - padding.left() - padding.right();
	const auto skip = (width - size * _buttons.size())
		/ float64(_buttons.size() - 1);
	const auto y = st::settingsSectionSkip * 2;
	auto x = float64(padding.left());
	for (const auto &button : _buttons) {
		button->moveToLeft(int(std::round(x)), y);
		x += size + skip;
	}
	inner->resize(inner->width(), y + size);
}

} // namespace

class BackgroundRow : public Ui::RpWidget {
public:
	BackgroundRow(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	void updateImage();

	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	crl::time radialTimeShift() const;
	void radialAnimationCallback(crl::time now);

	QPixmap _background;
	object_ptr<Ui::LinkButton> _chooseFromGallery;
	object_ptr<Ui::LinkButton> _chooseFromFile;

	Ui::RadialAnimation _radial;

};

class DefaultTheme final : public Ui::AbstractCheckView {
public:
	using Type = Window::Theme::EmbeddedType;
	using Scheme = Window::Theme::EmbeddedScheme;

	DefaultTheme(Scheme scheme, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

	void setColorizer(const Window::Theme::Colorizer &colorizer);

private:
	void checkedChangedHook(anim::type animated) override;

	Scheme _scheme;
	Scheme _colorized;
	Ui::RadioView _radio;

};

void ChooseFromFile(
	not_null<::Main::Session*> session,
	not_null<QWidget*> parent);

BackgroundRow::BackgroundRow(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _chooseFromGallery(
	this,
	tr::lng_settings_bg_from_gallery(tr::now),
	st::settingsLink)
, _chooseFromFile(this, tr::lng_settings_bg_from_file(tr::now), st::settingsLink)
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
	updateImage();

	_chooseFromGallery->addClickHandler([=] {
		Ui::show(Box<BackgroundBox>(&controller->session()));
	});
	_chooseFromFile->addClickHandler([=] {
		ChooseFromFile(&controller->session(), this);
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

	const auto radial = _radial.animating();
	const auto radialOpacity = radial ? _radial.opacity() : 0.;
	if (radial) {
		const auto backThumb = App::main()->newBackgroundThumb();
		if (!backThumb) {
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
				crl::now() + shift);
		}
	}
}

crl::time BackgroundRow::radialTimeShift() const {
	return st::radialDuration;
}

void BackgroundRow::radialAnimationCallback(crl::time now) {
	const auto updated = _radial.update(
		radialProgress(),
		!radialLoading(),
		now + radialTimeShift());
	if (!anim::Disabled() || updated) {
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

		if (const auto color = Window::Theme::Background()->colorForFill()) {
			p.fillRect(
				0,
				0,
				st::settingsBackgroundThumb,
				st::settingsBackgroundThumb,
				*color);
		} else {
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
	setColorizer({});
}

void DefaultTheme::setColorizer(const Window::Theme::Colorizer &colorizer) {
	_colorized = _scheme;
	if (colorizer) {
		Window::Theme::Colorize(_colorized, colorizer);
	}
	_radio.setToggledOverride(_colorized.radiobuttonActive);
	_radio.setUntoggledOverride(_colorized.radiobuttonInactive);
	update();
}

QSize DefaultTheme::getSize() const {
	return st::settingsThemePreviewSize;
}

void DefaultTheme::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	const auto received = QRect(
		st::settingsThemeBubblePosition,
		st::settingsThemeBubbleSize);
	const auto sent = QRect(
		outerWidth - received.width() - st::settingsThemeBubblePosition.x(),
		received.y() + received.height() + st::settingsThemeBubbleSkip,
		received.width(),
		received.height());
	const auto radius = st::settingsThemeBubbleRadius;

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);

	p.setBrush(_colorized.background);
	p.drawRoundedRect(
		QRect(QPoint(), st::settingsThemePreviewSize),
		radius,
		radius);

	p.setBrush(_colorized.received);
	p.drawRoundedRect(rtlrect(received, outerWidth), radius, radius);
	p.setBrush(_colorized.sent);
	p.drawRoundedRect(rtlrect(sent, outerWidth), radius, radius);

	const auto radio = _radio.getSize();
	_radio.paint(
		p,
		(outerWidth - radio.width()) / 2,
		getSize().height() - radio.height() - st::settingsThemeRadioBottom,
		outerWidth);
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

void ChooseFromFile(
		not_null<::Main::Session*> session,
		not_null<QWidget*> parent) {
	const auto &imgExtensions = cImgExtensions();
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette *")
		+ imgExtensions.join(qsl(" *"))
		+ qsl(")"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(session, [=](
			const FileDialog::OpenResult &result) {
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
		}
		auto local = Data::CustomWallPaper();
		local.setLocalImageAsThumbnail(std::make_shared<Image>(
			std::make_unique<Images::ImageSource>(
				std::move(image),
				"JPG")));
		Ui::show(Box<BackgroundPreviewBox>(session, local));
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));

}

QString DownloadPathText() {
	if (Global::DownloadPath().isEmpty()) {
		return tr::lng_download_path_default(tr::now);
	} else if (Global::DownloadPath() == qsl("tmp")) {
		return tr::lng_download_path_temp(tr::now);
	}
	return QDir::toNativeSeparators(Global::DownloadPath());
}

void SetupStickersEmoji(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_stickers_emoji());

	const auto session = &controller->session();

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap),
		QMargins(0, 0, 0, st::settingsCheckbox.margin.bottom())));

	const auto checkbox = [&](const QString &label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			label,
			checked,
			st::settingsCheckbox);
	};
	const auto add = [&](const QString &label, bool checked, auto &&handle) {
		inner->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding
		)->checkedChanges(
		) | rpl::start_with_next(
			std::move(handle),
			inner->lifetime());
	};

	add(
		tr::lng_settings_large_emoji(tr::now),
		session->settings().largeEmoji(),
		[=](bool checked) {
			session->settings().setLargeEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_replace_emojis(tr::now),
		session->settings().replaceEmoji(),
		[=](bool checked) {
			session->settings().setReplaceEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_suggest_emoji(tr::now),
		session->settings().suggestEmoji(),
		[=](bool checked) {
			session->settings().setSuggestEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_suggest_by_emoji(tr::now),
		session->settings().suggestStickersByEmoji(),
		[=](bool checked) {
			session->settings().setSuggestStickersByEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_loop_stickers(tr::now),
		session->settings().loopAnimatedStickers(),
		[=](bool checked) {
			session->settings().setLoopAnimatedStickers(checked);
			session->saveSettingsDelayed();
		});

	AddButton(
		container,
		tr::lng_stickers_you_have(),
		st::settingsChatButton,
		&st::settingsIconStickers,
		st::settingsChatIconLeft
	)->addClickHandler([=] {
		Ui::show(Box<StickersBox>(session, StickersBox::Section::Installed));
	});

	AddButton(
		container,
		tr::lng_emoji_manage_sets(),
		st::settingsChatButton,
		&st::settingsIconEmoji,
		st::settingsChatIconLeft
	)->addClickHandler([] {
		Ui::show(Box<Ui::Emoji::ManageSetsBox>());
	});

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupMessages(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_messages());

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
		controller->session().settings().sendSubmitWay());
	const auto add = [&](SendByType value, const QString &text) {
		inner->add(
			object_ptr<Ui::Radioenum<SendByType>>(
				inner,
				group,
				value,
				text,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	const auto small = st::settingsSendTypePadding;
	const auto top = skip;
	add(SendByType::Enter, tr::lng_settings_send_enter(tr::now));
	add(
		SendByType::CtrlEnter,
		(Platform::IsMac()
			? tr::lng_settings_send_cmdenter(tr::now)
			: tr::lng_settings_send_ctrlenter(tr::now)));

	group->setChangedCallback([=](SendByType value) {
		controller->session().settings().setSendSubmitWay(value);
		if (App::main()) {
			App::main()->ctrlEnterSubmitUpdated();
		}
		Local::writeUserSettings();
	});

	AddSkip(inner, st::settingsCheckboxesSkip);
}

void SetupExport(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_export_data(),
		st::settingsButton
	)->addClickHandler([=] {
		const auto session = &controller->session();
		Ui::hideSettingsAndLayer();
		App::CallDelayed(
			st::boxDuration,
			session,
			[=] { session->data().startExport(); });
	});
}

void SetupLocalStorage(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_manage_local_storage(),
		st::settingsButton
	)->addClickHandler([=] {
		LocalStorageBox::Show(&controller->session());
	});
}

void SetupDataStorage(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_data_storage());

	const auto ask = AddButton(
		container,
		tr::lng_download_path_ask(),
		st::settingsButton
	)->toggleOn(rpl::single(Global::AskDownloadPath()));

#ifndef OS_WIN_STORE
	const auto showpath = Ui::CreateChild<rpl::event_stream<bool>>(ask);
	const auto path = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				tr::lng_download_path(),
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
		tr::lng_download_path());
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

	SetupLocalStorage(controller, container);
	SetupExport(controller, container);

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupAutoDownload(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_media_auto_settings());

	using Source = Data::AutoDownload::Source;
	const auto add = [&](rpl::producer<QString> label, Source source) {
		AddButton(
			container,
			std::move(label),
			st::settingsButton
		)->addClickHandler([=] {
			Ui::show(Box<AutoDownloadBox>(&controller->session(), source));
		});
	};
	add(tr::lng_media_auto_in_private(), Source::User);
	add(tr::lng_media_auto_in_groups(), Source::Group);
	add(tr::lng_media_auto_in_channels(), Source::Channel);

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupChatBackground(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_section_background());

	container->add(
		object_ptr<BackgroundRow>(container, controller),
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
			tr::lng_settings_bg_tile(tr::now),
			Window::Theme::Background()->tile(),
			st::settingsCheckbox),
		st::settingsSendTypePadding);
	const auto adaptive = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_settings_adaptive_wide(tr::now),
				Global::AdaptiveForWide(),
				st::settingsCheckbox),
			st::settingsSendTypePadding));

	tile->checkedChanges(
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

	adaptive->entity()->checkedChanges(
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
	const auto palette = Ui::CreateChild<ColorsPalette>(
		container.get(),
		container.get());

	const auto chosen = [] {
		if (Window::Theme::IsNonDefaultBackground()) {
			return Type(-1);
		}
		const auto path = Window::Theme::Background()->themeAbsolutePath();
		for (const auto &scheme : kSchemesList) {
			if (path == scheme.path) {
				return scheme.type;
			}
		}
		return Type(-1);
	};
	const auto group = std::make_shared<Ui::RadioenumGroup<Type>>(chosen());

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
	const auto schemeClicked = [=](
			const Scheme &scheme,
			Qt::KeyboardModifiers modifiers) {
		apply(scheme);
	};

	auto checks = base::flat_map<Type,not_null<DefaultTheme*>>();
	auto buttons = ranges::view::all(
		kSchemesList
	) | ranges::view::transform([&](const Scheme &scheme) {
		auto check = std::make_unique<DefaultTheme>(scheme, false);
		const auto weak = check.get();
		const auto result = Ui::CreateChild<Ui::Radioenum<Type>>(
			block,
			group,
			scheme.type,
			scheme.name(tr::now),
			st::settingsTheme,
			std::move(check));
		result->addClickHandler([=] {
			schemeClicked(scheme, result->clickModifiers());
		});
		weak->setUpdateCallback([=] { result->update(); });
		checks.emplace(scheme.type, weak);
		return result;
	}) | ranges::to_vector;

	const auto refreshColorizer = [=](Type type) {
		if (type == chosen()) {
			palette->show(type);
		}

		const auto &colors = Core::App().settings().themesAccentColors();
		const auto i = checks.find(type);
		const auto scheme = ranges::find(kSchemesList, type, &Scheme::type);
		if (scheme == end(kSchemesList)) {
			return;
		}
		if (i != end(checks)) {
			if (const auto color = colors.get(type)) {
				const auto colorizer = Window::Theme::ColorizerFrom(
					*scheme,
					*color);
				i->second->setColorizer(colorizer);
			} else {
				i->second->setColorizer({});
			}
		}
	};

	for (const auto &scheme : kSchemesList) {
		refreshColorizer(scheme.type);
	}

	using Update = const Window::Theme::BackgroundUpdate;
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::ApplyingTheme
			|| update.type == Update::Type::New);
	}) | rpl::map([=] {
		return chosen();
	}) | rpl::start_with_next([=](Type type) {
		refreshColorizer(type);
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

	palette->selected(
	) | rpl::start_with_next([=](QColor color) {
		const auto type = chosen();
		const auto scheme = ranges::find(kSchemesList, type, &Scheme::type);
		if (scheme == end(kSchemesList)) {
			return;
		}
		auto &colors = Core::App().settings().themesAccentColors();
		if (colors.get(type) != color) {
			colors.set(type, color);
			Local::writeSettings();
		}
		apply(*scheme);
	}, container->lifetime());

	AddSkip(container);
}

void SetupThemeOptions(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsPrivacySkip);

	AddSubsectionTitle(container, tr::lng_settings_themes());

	AddSkip(container, st::settingsThemesTopSkip);
	SetupDefaultThemes(container);
	AddSkip(container, st::settingsThemesBottomSkip);

	AddButton(
		container,
		tr::lng_settings_bg_edit_theme(),
		st::settingsChatButton,
		&st::settingsIconThemes,
		st::settingsChatIconLeft
	)->addClickHandler(App::LambdaDelayed(
		st::settingsChatButton.ripple.hideDuration,
		container,
		[] { Window::Theme::Editor::Start(); }));

	AddSkip(container);
}

void SetupSupportSwitchSettings(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using SwitchType = Support::SwitchSettings;
	const auto group = std::make_shared<Ui::RadioenumGroup<SwitchType>>(
		controller->session().settings().supportSwitch());
	const auto add = [&](SwitchType value, const QString &label) {
		container->add(
			object_ptr<Ui::Radioenum<SwitchType>>(
				container,
				group,
				value,
				label,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	add(SwitchType::None, "Just send the reply");
	add(SwitchType::Next, "Send and switch to next");
	add(SwitchType::Previous, "Send and switch to previous");
	group->setChangedCallback([=](SwitchType value) {
		controller->session().settings().setSupportSwitch(value);
		Local::writeUserSettings();
	});
}

void SetupSupportChatsLimitSlice(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	constexpr auto kDayDuration = 24 * 60 * 60;
	struct Option {
		int days = 0;
		QString label;
	};
	const auto options = std::vector<Option>{
		{ 1, "1 day" },
		{ 7, "1 week" },
		{ 30, "1 month" },
		{ 365, "1 year" },
		{ 0, "All of them" },
	};
	const auto current = controller->session().settings().supportChatsTimeSlice();
	const auto days = current / kDayDuration;
	const auto best = ranges::min_element(
		options,
		std::less<>(),
		[&](const Option &option) { return std::abs(option.days - days); });

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(best->days);
	for (const auto &option : options) {
		container->add(
			object_ptr<Ui::Radiobutton>(
				container,
				group,
				option.days,
				option.label,
				st::settingsSendType),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int days) {
		controller->session().settings().setSupportChatsTimeSlice(
			days * kDayDuration);
		Local::writeUserSettings();
	});
}

void SetupSupport(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);

	AddSubsectionTitle(container, rpl::single(qsl("Support settings")));

	AddSkip(container, st::settingsSendTypeSkip);

	const auto skip = st::settingsSendTypeSkip;
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	SetupSupportSwitchSettings(controller, inner);

	AddSkip(inner, st::settingsCheckboxesSkip);

	inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			"Enable templates autocomplete",
			controller->session().settings().supportTemplatesAutocomplete(),
			st::settingsCheckbox),
		st::settingsSendTypePadding
	)->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		controller->session().settings().setSupportTemplatesAutocomplete(
			checked);
		Local::writeUserSettings();
	}, inner->lifetime());

	AddSkip(inner, st::settingsCheckboxesSkip);

	AddSubsectionTitle(inner, rpl::single(qsl("Load chats for a period")));

	SetupSupportChatsLimitSlice(controller, inner);

	AddSkip(inner, st::settingsCheckboxesSkip);

	AddSkip(inner);
}

Chat::Chat(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Chat::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupThemeOptions(content);
	SetupChatBackground(controller, content);
	SetupStickersEmoji(controller, content);
	SetupMessages(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
