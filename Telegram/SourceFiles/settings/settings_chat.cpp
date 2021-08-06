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
#include "boxes/confirm_box.h"
#include "boxes/background_box.h"
#include "boxes/background_preview_box.h"
#include "boxes/download_path_box.h"
#include "boxes/local_storage_box.h"
#include "boxes/edit_color_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/chat/attach/attach_extensions.h"
#include "ui/layers/generic_box.h"
#include "ui/effects/radial_animation.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "export/export_manager.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/themes/window_themes_cloud_list.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_file_origin.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "base/platform/base_platform_info.h"
#include "platform/platform_specific.h"
#include "base/call_delayed.h"
#include "support/support_common.h"
#include "support/support_templates.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "facades.h"
#include "app.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

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
	return _widget.clicks() | rpl::to_empty;
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

	const not_null<Window::SessionController*> _controller;
	QPixmap _background;
	object_ptr<Ui::LinkButton> _chooseFromGallery;
	object_ptr<Ui::LinkButton> _chooseFromFile;

	Ui::RadialAnimation _radial;

};

void ChooseFromFile(
	not_null<Window::SessionController*> controller,
	not_null<QWidget*> parent);

BackgroundRow::BackgroundRow(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _chooseFromGallery(
	this,
	tr::lng_settings_bg_from_gallery(tr::now),
	st::settingsLink)
, _chooseFromFile(this, tr::lng_settings_bg_from_file(tr::now), st::settingsLink)
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
	updateImage();

	_chooseFromGallery->addClickHandler([=] {
		controller->show(Box<BackgroundBox>(controller));
	});
	_chooseFromFile->addClickHandler([=] {
		ChooseFromFile(controller, this);
	});

	using Update = const Window::Theme::BackgroundUpdate;
	Window::Theme::Background()->updates(
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
		const auto backThumb = _controller->content()->newBackgroundThumb();
		if (!backThumb) {
			p.drawPixmap(0, 0, _background);
		} else {
			const auto &pix = backThumb->pixBlurred(
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
	return _controller->content()->chatBackgroundProgress();
}

bool BackgroundRow::radialLoading() const {
	const auto widget = _controller->content();
	if (widget->chatBackgroundLoading()) {
		widget->checkChatBackground();
		if (widget->chatBackgroundLoading()) {
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
	_background = Ui::PixmapFromImage(std::move(back));
	_background.setDevicePixelRatio(cRetinaFactor());

	rtlupdate(radialRect());

	if (radialLoading()) {
		radialStart();
	}
}

void ChooseFromFile(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent) {
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette *")
		+ Ui::ImageExtensions().join(qsl(" *"))
		+ qsl(")"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(controller, [=](
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
			std::move(image)));
		controller->show(Box<BackgroundPreviewBox>(controller, local));
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));
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
		Core::App().settings().largeEmoji(),
		[=](bool checked) {
			Core::App().settings().setLargeEmoji(checked);
			Core::App().saveSettingsDelayed();
		});

	add(
		tr::lng_settings_replace_emojis(tr::now),
		Core::App().settings().replaceEmoji(),
		[=](bool checked) {
			Core::App().settings().setReplaceEmoji(checked);
			Core::App().saveSettingsDelayed();
		});

	add(
		tr::lng_settings_suggest_emoji(tr::now),
		Core::App().settings().suggestEmoji(),
		[=](bool checked) {
			Core::App().settings().setSuggestEmoji(checked);
			Core::App().saveSettingsDelayed();
		});

	add(
		tr::lng_settings_suggest_by_emoji(tr::now),
		Core::App().settings().suggestStickersByEmoji(),
		[=](bool checked) {
			Core::App().settings().setSuggestStickersByEmoji(checked);
			Core::App().saveSettingsDelayed();
		});

	add(
		tr::lng_settings_loop_stickers(tr::now),
		Core::App().settings().loopAnimatedStickers(),
		[=](bool checked) {
			Core::App().settings().setLoopAnimatedStickers(checked);
			Core::App().saveSettingsDelayed();
		});

	AddButton(
		container,
		tr::lng_stickers_you_have(),
		st::settingsChatButton,
		&st::settingsIconStickers,
		st::settingsChatIconLeft
	)->addClickHandler([=] {
		controller->show(
			Box<StickersBox>(controller, StickersBox::Section::Installed));
	});

	AddButton(
		container,
		tr::lng_emoji_manage_sets(),
		st::settingsChatButton,
		&st::settingsIconEmoji,
		st::settingsChatIconLeft
	)->addClickHandler([=] {
		controller->show(Box<Ui::Emoji::ManageSetsBox>(session));
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
		Core::App().settings().sendSubmitWay());
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
	add(SendByType::Enter, tr::lng_settings_send_enter(tr::now));
	add(
		SendByType::CtrlEnter,
		(Platform::IsMac()
			? tr::lng_settings_send_cmdenter(tr::now)
			: tr::lng_settings_send_ctrlenter(tr::now)));

	group->setChangedCallback([=](SendByType value) {
		Core::App().settings().setSendSubmitWay(value);
		Core::App().saveSettingsDelayed();
		controller->content()->ctrlEnterSubmitUpdated();
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
		base::call_delayed(
			st::boxDuration,
			session,
			[=] { Core::App().exportManager().start(session); });
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
	)->toggleOn(rpl::single(Core::App().settings().askDownloadPath()));

#ifndef OS_WIN_STORE
	const auto showpath = Ui::CreateChild<rpl::event_stream<bool>>(ask);
	const auto path = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				tr::lng_download_path(),
				st::settingsButton)));
	auto pathtext = Core::App().settings().downloadPathValue(
	) | rpl::map([](const QString &text) {
		if (text.isEmpty()) {
			return tr::lng_download_path_default(tr::now);
		} else if (text == qsl("tmp")) {
			return tr::lng_download_path_temp(tr::now);
		}
		return QDir::toNativeSeparators(text);
	});
	CreateRightLabel(
		path->entity(),
		std::move(pathtext),
		st::settingsButton,
		tr::lng_download_path());
	path->entity()->addClickHandler([=] {
		controller->show(Box<DownloadPathBox>(controller));
	});
	path->toggleOn(ask->toggledValue() | rpl::map(!_1));
#endif // OS_WIN_STORE

	ask->toggledValue(
	) | rpl::filter([](bool checked) {
		return (checked != Core::App().settings().askDownloadPath());
	}) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setAskDownloadPath(checked);
		Core::App().saveSettingsDelayed();

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
			controller->show(
				Box<AutoDownloadBox>(&controller->session(), source));
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
				Core::App().settings().adaptiveForWide(),
				st::settingsCheckbox),
			st::settingsSendTypePadding));

	tile->checkedChanges(
	) | rpl::start_with_next([](bool checked) {
		Window::Theme::Background()->setTile(checked);
	}, tile->lifetime());

	using Update = const Window::Theme::BackgroundUpdate;
	Window::Theme::Background()->updates(
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::Changed);
	}) | rpl::map([] {
		return Window::Theme::Background()->tile();
	}) | rpl::start_with_next([=](bool tiled) {
		tile->setChecked(tiled);
	}, tile->lifetime());

	adaptive->toggleOn(controller->adaptive().chatLayoutValue(
	) | rpl::map([](Window::Adaptive::ChatLayout layout) {
		return (layout == Window::Adaptive::ChatLayout::Wide);
	}));

	adaptive->entity()->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		Core::App().settings().setAdaptiveForWide(checked);
		Core::App().saveSettingsDelayed();
	}, adaptive->lifetime());
}

void SetupDefaultThemes(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	using Type = Window::Theme::EmbeddedType;
	using Scheme = Window::Theme::EmbeddedScheme;
	using Check = Window::Theme::CloudListCheck;
	using namespace Window::Theme;

	const auto block = container->add(object_ptr<Ui::FixedHeightWidget>(
		container));
	const auto palette = Ui::CreateChild<ColorsPalette>(
		container.get(),
		container.get());

	const auto chosen = [] {
		const auto &object = Background()->themeObject();
		if (object.cloud.id) {
			return Type(-1);
		}
		for (const auto &scheme : kSchemesList) {
			if (object.pathAbsolute == scheme.path) {
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
		const auto currentlyIsCustom = (chosen() == Type(-1))
			&& !Background()->themeObject().cloud.id;
		const auto keep = [=] {
			if (!currentlyIsCustom) {
				KeepApplied();
			}
		};
		if (IsNightMode() == isNight(scheme)) {
			ApplyDefaultWithPath(scheme.path);
			keep();
		} else {
			Window::Theme::ToggleNightModeWithConfirmation(
				window,
				[=, path = scheme.path] { ToggleNightMode(path); keep();});
		}
	};
	const auto schemeClicked = [=](
			const Scheme &scheme,
			Qt::KeyboardModifiers modifiers) {
		apply(scheme);
	};

	auto checks = base::flat_map<Type,not_null<Check*>>();
	auto buttons = ranges::views::all(
		kSchemesList
	) | ranges::views::transform([&](const Scheme &scheme) {
		auto check = std::make_unique<Check>(
			ColorsFromScheme(scheme),
			false);
		const auto weak = check.get();
		const auto result = Ui::CreateChild<Ui::Radioenum<Type>>(
			block,
			group,
			scheme.type,
			scheme.name(tr::now),
			st::settingsTheme,
			std::move(check));
		scheme.name(
		) | rpl::start_with_next([=](const auto &themeName) {
			result->setText(themeName);
		}, result->lifetime());
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
				const auto colorizer = ColorizerFrom(*scheme, *color);
				i->second->setColors(ColorsFromScheme(*scheme, colorizer));
			} else {
				i->second->setColors(ColorsFromScheme(*scheme));
			}
		}
	};
	group->setChangedCallback([=](Type type) {
		group->setValue(chosen());
	});
	for (const auto &scheme : kSchemesList) {
		refreshColorizer(scheme.type);
	}

	Background()->updates(
	) | rpl::filter([](const BackgroundUpdate &update) {
		return (update.type == BackgroundUpdate::Type::ApplyingTheme);
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

		const auto padding = st::settingsButton.padding;
		width -= padding.left() + padding.right();
		const auto desired = st::settingsThemePreviewSize.width();
		const auto count = int(buttons.size());
		const auto skips = count - 1;
		const auto minSkip = st::settingsThemeMinSkip;
		const auto single = [&] {
			if (width >= skips * minSkip + count * desired) {
				return desired;
			}
			return (width - skips * minSkip) / count;
		}();
		if (single <= 0) {
			return;
		}
		const auto fullSkips = width - count * single;
		const auto skip = fullSkips / float64(skips);
		auto left = padding.left() + 0.;
		for (const auto button : buttons) {
			button->resizeToWidth(single);
			button->moveToLeft(int(std::round(left)), 0);
			left += button->width() + skip;
		}
	}, block->lifetime());

	palette->selected(
	) | rpl::start_with_next([=](QColor color) {
		if (Background()->editingTheme()) {
			// We don't remember old accent color to revert it properly
			// in Window::Theme::Revert which is called by Editor.
			//
			// So we check here, before we change the saved accent color.
			window->show(Box<InformBox>(
				tr::lng_theme_editor_cant_change_theme(tr::now)));
			return;
		}
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

void SetupThemeOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using namespace Window::Theme;

	AddSkip(container, st::settingsPrivacySkip);

	AddSubsectionTitle(container, tr::lng_settings_themes());

	AddSkip(container, st::settingsThemesTopSkip);
	SetupDefaultThemes(&controller->window(), container);
	AddSkip(container);
}

void SetupCloudThemes(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using namespace Window::Theme;
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0);
	const auto inner = wrap->entity();

	AddDivider(inner);
	AddSkip(inner, st::settingsPrivacySkip);

	const auto title = AddSubsectionTitle(
		inner,
		tr::lng_settings_bg_cloud_themes());
	const auto showAll = Ui::CreateChild<Ui::LinkButton>(
		inner,
		tr::lng_settings_bg_show_all(tr::now));

	rpl::combine(
		title->topValue(),
		inner->widthValue(),
		showAll->widthValue()
	) | rpl::start_with_next([=](int top, int outerWidth, int width) {
		showAll->moveToRight(
			st::settingsSubsectionTitlePadding.left(),
			top,
			outerWidth);
	}, showAll->lifetime());

	AddSkip(inner, st::settingsThemesTopSkip);

	const auto list = inner->lifetime().make_state<CloudList>(
		inner,
		controller);
	inner->add(
		list->takeWidget(),
		style::margins(
			st::settingsButton.padding.left(),
			0,
			st::settingsButton.padding.right(),
			0));

	list->allShown(
	) | rpl::start_with_next([=](bool shown) {
		showAll->setVisible(!shown);
	}, showAll->lifetime());

	showAll->addClickHandler([=] {
		list->showAll();
	});

	const auto editWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner))
	)->setDuration(0);
	const auto edit = editWrap->entity();

	AddSkip(edit, st::settingsThemesBottomSkip);
	AddButton(
		edit,
		tr::lng_settings_bg_theme_edit(),
		st::settingsChatButton,
		&st::settingsIconThemes,
		st::settingsChatIconLeft
	)->addClickHandler([=] {
		StartEditor(
			&controller->window(),
			Background()->themeObject().cloud);
	});

	editWrap->toggleOn(rpl::single(BackgroundUpdate(
		BackgroundUpdate::Type::ApplyingTheme,
		Background()->tile()
	)) | rpl::then(
		Background()->updates()
	) | rpl::filter([](const BackgroundUpdate &update) {
		return (update.type == BackgroundUpdate::Type::ApplyingTheme);
	}) | rpl::map([=] {
		const auto userId = controller->session().userId();
		return (Background()->themeObject().cloud.createdBy == userId);
	}));

	AddSkip(inner, 2 * st::settingsSectionSkip);

	wrap->setDuration(0)->toggleOn(list->empty() | rpl::map(!_1));
}

void SetupAutoNightMode(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	if (!Platform::IsDarkModeSupported()) {
		return;
	}

	AddDivider(container);
	AddSkip(container, st::settingsPrivacySkip);

	AddSubsectionTitle(container, tr::lng_settings_auto_night_mode());

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto autoNight = wrap->add(
		object_ptr<Ui::Checkbox>(
			wrap,
			tr::lng_settings_auto_night_enabled(tr::now),
			Core::App().settings().systemDarkModeEnabled(),
			st::settingsCheckbox),
		st::settingsCheckboxPadding);

	autoNight->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != Core::App().settings().systemDarkModeEnabled());
	}) | rpl::start_with_next([=](bool checked) {
		if (checked && Window::Theme::Background()->editingTheme()) {
			autoNight->setChecked(false);
			controller->show(Box<InformBox>(
				tr::lng_theme_editor_cant_change_theme(tr::now)));
		} else {
			Core::App().settings().setSystemDarkModeEnabled(checked);
			Core::App().saveSettingsDelayed();
		}
	}, autoNight->lifetime());

	Core::App().settings().systemDarkModeEnabledChanges(
	) | rpl::filter([=](bool value) {
		return (value != autoNight->checked());
	}) | rpl::start_with_next([=](bool value) {
		autoNight->setChecked(value);
	}, autoNight->lifetime());

	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap)));

	AddSkip(container, st::settingsCheckboxesSkip);
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
		controller->session().saveSettingsDelayed();
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
		controller->session().saveSettingsDelayed();
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
		controller->session().saveSettingsDelayed();
	}, inner->lifetime());

	inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			"Send all messages without sound",
			controller->session().settings().supportAllSilent(),
			st::settingsCheckbox),
		st::settingsSendTypePadding
	)->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		controller->session().settings().setSupportAllSilent(
			checked);
		controller->session().saveSettingsDelayed();
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

	SetupThemeOptions(controller, content);
	SetupAutoNightMode(controller, content);
	SetupCloudThemes(controller, content);
	SetupChatBackground(controller, content);
	SetupStickersEmoji(controller, content);
	SetupMessages(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
