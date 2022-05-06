/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_common.h"

#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings::CloudPassword {

void OneEdgeBoxContentDivider::skipEdge(Qt::Edge edge, bool skip) {
	const auto was = _skipEdges;
	if (skip) {
		_skipEdges |= edge;
	} else {
		_skipEdges &= ~edge;
	}
	if (was != _skipEdges) {
		update();
	}
}

void OneEdgeBoxContentDivider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), Ui::BoxContentDivider::color());
	if (!(_skipEdges & Qt::TopEdge)) {
		Ui::BoxContentDivider::paintTop(p);
	}
	if (!(_skipEdges & Qt::BottomEdge)) {
		Ui::BoxContentDivider::paintBottom(p);
	}
}

BottomButton CreateBottomDisableButton(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QRect> &&sectionGeometryValue,
		rpl::producer<QString> &&buttonText,
		Fn<void()> &&callback) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	AddSkip(content);

	AddButton(
		content,
		std::move(buttonText),
		st::settingsAttentionButton
	)->addClickHandler(std::move(callback));

	const auto divider = Ui::CreateChild<OneEdgeBoxContentDivider>(
		parent.get());
	divider->skipEdge(Qt::TopEdge, true);
	rpl::combine(
		std::move(sectionGeometryValue),
		parent->geometryValue(),
		content->geometryValue()
	) | rpl::start_with_next([=](
			const QRect &r,
			const QRect &parentRect,
			const QRect &bottomRect) {
		const auto top = r.y() + r.height();
		divider->setGeometry(
			0,
			top,
			r.width(),
			parentRect.height() - top - bottomRect.height());
	}, divider->lifetime());
	divider->show();

	return {
		.content = Ui::MakeWeak(not_null<Ui::RpWidget*>{ content }),
		.isBottomFillerShown = divider->geometryValue(
		) | rpl::map([](const QRect &r) {
			return r.height() > 0;
		}),
	};
}

void SetupHeader(
		not_null<Ui::VerticalLayout*> content,
		const QString &lottie,
		rpl::producer<> &&showFinished,
		rpl::producer<QString> &&subtitle,
		rpl::producer<QString> &&about) {
	if (!lottie.isEmpty()) {
		const auto &size = st::settingsCloudPasswordIconSize;
		auto icon = CreateLottieIcon(
			content,
			{ .name = lottie, .sizeOverride = { size, size } },
			st::settingLocalPasscodeIconPadding);
		content->add(std::move(icon.widget));
		std::move(
			showFinished
		) | rpl::start_with_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, content->lifetime());

		AddSkip(content);
	}

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(subtitle),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding);

	{
		const auto &st = st::settingLocalPasscodeDescription;
		content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(content, std::move(about), st)),
			st::changePhoneDescriptionPadding);
	}
}

not_null<Ui::PasswordInput*> AddPasswordField(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&placeholder,
		const QString &text) {
	const auto &st = st::settingLocalPasscodeInputField;
	auto container = object_ptr<Ui::RpWidget>(content);
	container->resize(container->width(), st.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		container.data(),
		st,
		std::move(placeholder),
		text);

	container->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		field->moveToLeft((r.width() - field->width()) / 2, 0);
	}, container->lifetime());

	content->add(std::move(container));
	return field;
}

not_null<Ui::CenterWrap<Ui::InputField>*> AddWrappedField(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&placeholder,
		const QString &text) {
	return content->add(object_ptr<Ui::CenterWrap<Ui::InputField>>(
		content,
		object_ptr<Ui::InputField>(
			content,
			st::settingLocalPasscodeInputField,
			std::move(placeholder),
			text)));
}

not_null<Ui::FlatLabel*> AddError(
		not_null<Ui::VerticalLayout*> content,
		Ui::PasswordInput *input) {
	const auto error = content->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				// Set any text to resize.
				tr::lng_language_name(tr::now),
				st::settingLocalPasscodeError)),
		st::changePhoneDescriptionPadding)->entity();
	error->hide();
	if (input) {
		QObject::connect(input, &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
	}
	return error;
};

not_null<Ui::RoundButton*> AddDoneButton(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&text) {
	const auto button = content->add(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			content,
			object_ptr<Ui::RoundButton>(
				content,
				std::move(text),
				st::changePhoneButton)),
		st::settingLocalPasscodeButtonPadding)->entity();
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	return button;
}

void AddSkipInsteadOfField(not_null<Ui::VerticalLayout*> content) {
	AddSkip(content, st::settingLocalPasscodeInputField.heightMin);
}

void AddSkipInsteadOfError(not_null<Ui::VerticalLayout*> content) {
	auto dummy = base::make_unique_q<Ui::FlatLabel>(
		content,
		tr::lng_language_name(tr::now),
		st::settingLocalPasscodeError);
	AddSkip(content, dummy->height());
	dummy = nullptr;
}

AbstractStep::AbstractStep(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent)
, _controller(controller) {
}

not_null<Window::SessionController*> AbstractStep::controller() const {
	return _controller;
}

Api::CloudPassword &AbstractStep::cloudPassword() {
	return _controller->session().api().cloudPassword();
}

void AbstractStep::showBack() {
	_showBack.fire({});
}

void AbstractStep::showOther(Type type) {
	_showOther.fire_copy(type);
}

void AbstractStep::setFocusCallback(Fn<void()> callback) {
	_setInnerFocusCallback = callback;
}

rpl::producer<> AbstractStep::showFinishes() const {
	return _showFinished.events();
}

void AbstractStep::showFinished() {
	_showFinished.fire({});
}

void AbstractStep::setInnerFocus() {
	if (_setInnerFocusCallback) {
		_setInnerFocusCallback();
	}
}

rpl::producer<Type> AbstractStep::sectionShowOther() {
	return _showOther.events();
}

rpl::producer<> AbstractStep::sectionShowBack() {
	return _showBack.events();
}

void AbstractStep::setStepDataReference(std::any &data) {
	_stepData = &data;
}

StepData AbstractStep::stepData() const {
	if (!_stepData || !_stepData->has_value()) {
		StepData();
	}
	const auto my = std::any_cast<StepData>(_stepData);
	return my ? (*my) : StepData();
}

void AbstractStep::setStepData(StepData data) {
	if (_stepData) {
		*_stepData = data;
	}
}

AbstractStep::~AbstractStep() = default;

} // namespace Settings::CloudPassword
