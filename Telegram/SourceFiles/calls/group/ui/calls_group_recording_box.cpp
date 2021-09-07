/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/ui/calls_group_recording_box.h"

#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QSvgRenderer>

namespace Calls::Group {
namespace {

constexpr auto kRoundRadius = 9;
constexpr auto kMaxGroupCallLength = 40;
constexpr auto kSwitchDuration = 200;
constexpr auto kSelectDuration = 120;

class GraphicButton final : public Ui::AbstractButton {
public:
	GraphicButton(
		not_null<Ui::RpWidget*> parent,
		const QString &filename,
		int selectWidth = 0);

	void setToggled(bool value);
protected:
	void paintEvent(QPaintEvent *e);

private:
	const style::margins _margins;
	QSvgRenderer _renderer;
	Ui::RoundRect _roundRect;
	Ui::RoundRect _roundRectSelect;
	Ui::Animations::Simple _animation;
	bool _toggled = false;
};

class RecordingInfo final : public Ui::RpWidget {
public:
	RecordingInfo(not_null<Ui::RpWidget*> parent);

	void prepareAudio();
	void prepareVideo();

	RecordingType type() const;

private:
	void setLabel(const QString &text);

	const object_ptr<Ui::VerticalLayout> _container;
	RecordingType _type = RecordingType::AudioOnly;
};

class Switcher final : public Ui::RpWidget {
public:
	Switcher(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<bool> &&toggled);

	RecordingType type() const;

private:
	const object_ptr<Ui::BoxContentDivider> _background;
	const object_ptr<RecordingInfo> _audio;
	const object_ptr<RecordingInfo> _video;
	bool _toggled = false;

	Ui::Animations::Simple _animation;
};

GraphicButton::GraphicButton(
	not_null<Ui::RpWidget*> parent,
	const QString &filename,
	int selectWidth)
: AbstractButton(parent)
, _margins(selectWidth, selectWidth, selectWidth, selectWidth)
, _renderer(u":/gui/recording/%1.svg"_q.arg(filename))
, _roundRect(kRoundRadius, st::groupCallMembersBg)
, _roundRectSelect(kRoundRadius, st::groupCallActiveFg) {
	const auto size = style::ConvertScale(_renderer.defaultSize());
	resize((QRect(QPoint(), size) + _margins).size());
}

void GraphicButton::setToggled(bool value) {
	if (_toggled == value) {
		return;
	}
	_toggled = value;
	_animation.start(
		[=] { update(); },
		_toggled ? 0. : 1.,
		_toggled ? 1. : 0.,
		kSelectDuration);
}

void GraphicButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto progress = _animation.value(_toggled ? 1. : 0.);
	p.setOpacity(progress);
	_roundRectSelect.paint(p, rect());
	p.setOpacity(1.);
	const auto r = rect() - _margins;
	_roundRect.paint(p, r);
	_renderer.render(&p, r);
}

RecordingInfo::RecordingInfo(not_null<Ui::RpWidget*> parent)
: RpWidget(parent)
, _container(this) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		_container->resizeToWidth(size.width());
	}, _container->lifetime());
}

void RecordingInfo::prepareAudio() {
	_type = RecordingType::AudioOnly;
	setLabel(tr::lng_group_call_recording_start_audio_subtitle(tr::now));

	const auto wrap = _container->add(
		object_ptr<Ui::RpWidget>(_container),
		style::margins(0, st::groupCallRecordingAudioSkip, 0, 0));
	const auto audioIcon = Ui::CreateChild<GraphicButton>(
		wrap,
		"info_audio");
	wrap->resize(width(), audioIcon->height());
	audioIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		audioIcon->moveToLeft((size.width() - audioIcon->width()) / 2, 0);
	}, lifetime());
}

void RecordingInfo::prepareVideo() {
	setLabel(tr::lng_group_call_recording_start_video_subtitle(tr::now));

	const auto wrap = _container->add(
		object_ptr<Ui::RpWidget>(_container),
		style::margins());

	const auto landscapeIcon = Ui::CreateChild<GraphicButton>(
		wrap,
		"info_video_landscape",
		st::groupCallRecordingSelectWidth);
	const auto portraitIcon = Ui::CreateChild<GraphicButton>(
		wrap,
		"info_video_portrait",
		st::groupCallRecordingSelectWidth);
	wrap->resize(width(), portraitIcon->height());

	landscapeIcon->setToggled(true);
	_type = RecordingType::VideoLandscape;

	const auto icons = std::vector<GraphicButton*>{
		landscapeIcon,
		portraitIcon,
	};
	const auto types = std::map<GraphicButton*, RecordingType>{
		{ landscapeIcon, RecordingType::VideoLandscape },
		{ portraitIcon, RecordingType::VideoPortrait },
	};
	for (const auto icon : icons) {
		icon->clicks(
		) | rpl::start_with_next([=] {
			for (const auto &i : icons) {
				i->setToggled(icon == i);
			}
			_type = types.at(icon);
		}, lifetime());
	}

	wrap->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto wHalf = size.width() / icons.size();
		for (auto i = 0; i < icons.size(); i++) {
			const auto &icon = icons[i];
			icon->moveToLeft(
				wHalf * i + (wHalf - icon->width()) / 2,
				(size.height() - icon->height()) / 2);
		}
	}, lifetime());
}

void RecordingInfo::setLabel(const QString &text) {
	const auto label = _container->add(
		object_ptr<Ui::FlatLabel>(
			_container,
			text,
			st::groupCallRecordingSubLabel),
		st::groupCallRecordingSubLabelMargins);

	rpl::combine(
		sizeValue(),
		label->sizeValue()
	) | rpl::start_with_next([=](QSize my, QSize labelSize) {
		label->moveToLeft(
			(my.width() - labelSize.width()) / 2,
			label->y(),
			my.width());
	}, label->lifetime());
}

RecordingType RecordingInfo::type() const {
	return _type;
}

Switcher::Switcher(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<bool> &&toggled)
: RpWidget(parent)
, _background(this, st::groupCallRecordingInfoHeight, st::groupCallBg)
, _audio(this)
, _video(this) {
	_audio->prepareAudio();
	_video->prepareVideo();

	resize(0, st::groupCallRecordingInfoHeight);

	const auto updatePositions = [=](float64 progress) {
		_audio->moveToLeft(-width() * progress, 0);
		_video->moveToLeft(_audio->x() + _audio->width(), 0);
	};

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		_audio->resize(size.width(), size.height());
		_video->resize(size.width(), size.height());

		updatePositions(_toggled ? 1. : 0.);

		_background->lower();
		_background->setGeometry(QRect(QPoint(), size));
	}, lifetime());

	std::move(
		toggled
	) | rpl::start_with_next([=](bool toggled) {
		_toggled = toggled;
		_animation.start(
			updatePositions,
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			kSwitchDuration);
	}, lifetime());
}

RecordingType Switcher::type() const {
	return _toggled ? _video->type() : _audio->type();
}

} // namespace

void EditGroupCallTitleBox(
		not_null<Ui::GenericBox*> box,
		const QString &placeholder,
		const QString &title,
		bool livestream,
		Fn<void(QString)> done) {
	box->setTitle(livestream
		? tr::lng_group_call_edit_title_channel()
		: tr::lng_group_call_edit_title());
	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::groupCallField,
		rpl::single(placeholder),
		title));
	input->setMaxLength(kMaxGroupCallLength);
	box->setFocusCallback([=] {
		input->setFocusFast();
	});
	const auto submit = [=] {
		const auto result = input->getLastText().trimmed();
		box->closeBox();
		done(result);
	};
	QObject::connect(input, &Ui::InputField::submitted, submit);
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void StartGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(RecordingType)> done) {
	box->setTitle(tr::lng_group_call_recording_start());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			tr::lng_group_call_recording_start_sure(),
			st::groupCallBoxLabel));

	const auto checkbox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_group_call_recording_start_checkbox(),
			false,
			st::groupCallCheckbox),
		style::margins(
			st::boxRowPadding.left(),
			st::boxRowPadding.left(),
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));

	const auto switcher = box->addRow(
		object_ptr<Switcher>(box, checkbox->checkedChanges()),
		st::groupCallRecordingInfoMargins);

	box->addButton(tr::lng_continue(), [=] {
		const auto type = switcher->type();
		box->closeBox();
		done(type);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void AddTitleGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		Fn<void(QString)> done) {
	box->setTitle(tr::lng_group_call_recording_start_title());

	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::groupCallField,
		tr::lng_group_call_recording_start_field(),
		title));
	box->setFocusCallback([=] {
		input->setFocusFast();
	});
	const auto submit = [=] {
		const auto result = input->getLastText().trimmed();
		box->closeBox();
		done(result);
	};
	QObject::connect(input, &Ui::InputField::submitted, submit);
	box->addButton(tr::lng_group_call_recording_start_button(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}


void StopGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> done) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			tr::lng_group_call_recording_stop_sure(),
			st::groupCallBoxLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::boxPadding.top(),
			st::boxRowPadding.right(),
			st::boxPadding.bottom()));

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
		done(QString());
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Calls::Group
