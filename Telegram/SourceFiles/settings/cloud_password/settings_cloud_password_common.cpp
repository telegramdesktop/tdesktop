/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_common.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
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
	auto p = QPainter(this);
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

	Ui::AddSkip(content);

	content->add(object_ptr<Button>(
		content,
		std::move(buttonText),
		st::settingsAttentionButton
	))->addClickHandler(std::move(callback));

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

void SetupAutoCloseTimer(
		rpl::lifetime &lifetime,
		Fn<void()> callback,
		Fn<crl::time()> lastNonIdleTime) {
	constexpr auto kTimerCheck = crl::time(1000 * 60);
	constexpr auto kAutoCloseTimeout = crl::time(1000 * 60 * 10);

	const auto timer = lifetime.make_state<base::Timer>([=] {
		const auto idle = crl::now() - lastNonIdleTime();
		if (idle >= kAutoCloseTimeout) {
			callback();
		}
	});
	timer->callEach(kTimerCheck);
}

void SetupHeader(
		not_null<Ui::VerticalLayout*> content,
		const QString &lottie,
		rpl::producer<> &&showFinished,
		rpl::producer<QString> &&subtitle,
		v::text::data &&about) {
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
	}
	Ui::AddSkip(content);

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
		const auto wrap = content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(
					content,
					v::text::take_marked(std::move(about)),
					st,
					st::defaultPopupMenu,
					[=](Fn<void()> update) {
						return CommonTextContext{ std::move(update) };
					})),
			st::changePhoneDescriptionPadding);
		wrap->setAttribute(Qt::WA_TransparentForMouseEvents);
		wrap->resize(
			wrap->width(),
			st::settingLocalPasscodeDescriptionHeight);
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

not_null<Ui::LinkButton*> AddLinkButton(
		not_null<Ui::CenterWrap<Ui::InputField>*> wrap,
		rpl::producer<QString> &&text) {
	const auto button = Ui::CreateChild<Ui::LinkButton>(
		wrap->parentWidget(),
		QString());
	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		button->setText(text);
	}, button->lifetime());

	wrap->geometryValue(
	) | rpl::start_with_next([=](QRect r) {
		r.translate(wrap->entity()->pos().x(), 0);
		button->moveToLeft(r.x(), r.y() + r.height() + st::passcodeTextLine);
	}, button->lifetime());
	return button;
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
	Ui::AddSkip(content, st::settingLocalPasscodeInputField.heightMin);
}

void AddSkipInsteadOfError(not_null<Ui::VerticalLayout*> content) {
	auto dummy = base::make_unique_q<Ui::FlatLabel>(
		content,
		tr::lng_language_name(tr::now),
		st::settingLocalPasscodeError);
	const auto &padding = st::changePhoneDescriptionPadding;
	Ui::AddSkip(content, dummy->height() + padding.top() + padding.bottom());
	dummy = nullptr;
}

} // namespace Settings::CloudPassword
