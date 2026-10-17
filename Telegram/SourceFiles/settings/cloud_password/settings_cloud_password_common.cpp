/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_common.h"

#include "base/qt_signal_producer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_passcode_box.h"
#include "styles/style_settings.h"

namespace Settings::CloudPassword {
namespace {

struct Icon {
	not_null<Lottie::Icon*> icon;
	Fn<void()> update;
};

Icon CreateInteractiveLottieIcon(
		not_null<Ui::VerticalLayout*> container,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding) {
	auto object = object_ptr<Ui::RpWidget>(container);
	const auto raw = object.data();

	const auto width = descriptor.sizeOverride.width();
	raw->resize((Rect(descriptor.sizeOverride) + padding).size());

	auto owned = Lottie::MakeIcon(std::move(descriptor));
	const auto icon = owned.get();

	raw->lifetime().add([kept = std::move(owned)]{});

	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top());
	}, raw->lifetime());

	container->add(std::move(object));
	return { .icon = icon, .update = [=] { raw->update(); } };
}

} // namespace

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

	const auto button = content->add(object_ptr<Button>(
		content,
		std::move(buttonText),
		st::settingsAttentionButton));
	button->addClickHandler(std::move(callback));

	const auto divider = Ui::CreateChild<OneEdgeBoxContentDivider>(
		parent.get());
	divider->skipEdge(Qt::TopEdge, true);
	rpl::combine(
		std::move(sectionGeometryValue),
		parent->geometryValue(),
		content->geometryValue()
	) | rpl::on_next([=](
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
		.content = base::make_weak(content),
		.button = base::make_weak(button),
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
		) | rpl::on_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, content->lifetime());
	}
	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			std::move(subtitle),
			st::changePhoneTitle),
		st::changePhoneTitlePadding,
		style::al_top);

	{
		const auto &st = st::settingLocalPasscodeDescription;
		const auto description = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				v::text::take_marked(std::move(about)),
				st,
				st::defaultPopupMenu),
			st::changePhoneDescriptionPadding,
			style::al_top);
		description->setTryMakeSimilarLines(true);
		description->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

not_null<Ui::PasswordInput*> AddPasswordField(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&placeholder,
		const QString &text,
		const style::InputField &st) {
	auto container = object_ptr<Ui::RpWidget>(content);
	container->resize(container->width(), st.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		container.data(),
		st,
		std::move(placeholder),
		text);

	container->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
		field->moveToLeft((r.width() - field->width()) / 2, 0);
	}, container->lifetime());

	content->add(std::move(container));
	return field;
}

not_null<Ui::InputField*> AddWrappedField(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&placeholder,
		const QString &text) {
	return content->add(
		object_ptr<Ui::InputField>(
			content,
			st::settingLocalPasscodeInputField,
			std::move(placeholder),
			text),
		style::al_top);
}

not_null<Ui::LinkButton*> AddLinkButton(
		not_null<Ui::InputField*> input,
		rpl::producer<QString> &&text) {
	const auto button = Ui::CreateChild<Ui::LinkButton>(
		input->parentWidget(),
		QString());
	std::move(
		text
	) | rpl::on_next([=](const QString &text) {
		button->setText(text);
	}, button->lifetime());

	input->geometryValue(
	) | rpl::on_next([=](QRect r) {
		button->moveToLeft(r.x(), r.y() + r.height() + st::passcodeTextLine);
	}, button->lifetime());
	return button;
}

not_null<Ui::FlatLabel*> AddError(
		not_null<Ui::VerticalLayout*> content,
		Ui::PasswordInput *input,
		const style::FlatLabel &st,
		const style::margins &padding) {
	const auto error = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			QString(),
			st),
		padding,
		style::al_top);
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
		object_ptr<Ui::RoundButton>(
			content,
			std::move(text),
			st::changePhoneButton),
		st::settingLocalPasscodeButtonPadding,
		style::al_top);
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

void SetupIntroHeader(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<> &&showFinished) {
	SetupHeader(
		content,
		u"cloud_password/intro"_q,
		std::move(showFinished),
		tr::lng_settings_cloud_password_start_title(),
		tr::lng_settings_cloud_password_start_about());
}

PasswordFieldsDescriptor CreatePasswordDescriptor(const QString &text) {
	return {
		.lottie = u"cloud_password/password_input"_q,
		.title = tr::lng_settings_cloud_password_password_subtitle(),
		.about = tr::lng_cloud_password_about(),
		.placeholder = tr::lng_cloud_password_enter_new(),
		.text = text,
		.confirm = true,
		.reactToTyping = true,
	};
}

PasswordFields SetupPasswordFields(
		not_null<Ui::VerticalLayout*> content,
		PasswordFieldsDescriptor &&descriptor) {
	const auto icon = CreateInteractiveLottieIcon(
		content,
		{
			.name = descriptor.lottie,
			.sizeOverride = Size(st::settingsCloudPasswordIconSize),
		},
		st::settingLocalPasscodeIconPadding);

	SetupHeader(
		content,
		QString(),
		rpl::never<>(),
		std::move(descriptor.title),
		std::move(descriptor.about));

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto input = AddPasswordField(
		content,
		std::move(descriptor.placeholder),
		descriptor.text);
	const auto confirm = descriptor.confirm
		? AddPasswordField(
			content,
			tr::lng_cloud_password_confirm_new(),
			descriptor.text).get()
		: nullptr;
	const auto error = AddError(content, input);
	if (confirm) {
		QObject::connect(confirm, &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
	}

	if (!descriptor.reactToTyping) {
		icon.icon->animate(icon.update, 0, icon.icon->framesCount() - 1);
	} else {
		if (!input->text().isEmpty()) {
			icon.icon->jumpTo(icon.icon->framesCount() / 2, icon.update);
		}
		base::qt_signal_producer(
			input.get(),
			&QLineEdit::textChanged // Covers Undo.
		) | rpl::map([=] {
			return input->text().isEmpty();
		}) | rpl::distinct_until_changed(
		) | rpl::on_next([=](bool empty) {
			const auto from = icon.icon->frameIndex();
			const auto to = empty ? 0 : (icon.icon->framesCount() / 2 - 1);
			icon.icon->animate(icon.update, from, to);
		}, content->lifetime());
	}
	return { .input = input, .confirm = confirm, .error = error };
}

std::optional<QString> ValidatePasswordFields(const PasswordFields &fields) {
	const auto input = fields.input;
	const auto confirm = fields.confirm;
	const auto text = input->text();
	const auto confirmText = confirm ? confirm->text() : QString();
	if (text.isEmpty()) {
		input->setFocus();
		input->showError();
		return std::nullopt;
	} else if (confirm && confirmText.isEmpty()) {
		confirm->setFocus();
		confirm->showError();
		return std::nullopt;
	} else if (confirm && (text != confirmText)) {
		confirm->setFocus();
		confirm->showError();
		confirm->selectAll();
		fields.error->show();
		fields.error->setText(tr::lng_cloud_password_differ(tr::now));
		return std::nullopt;
	}
	return text;
}

void SubmitPasswordFields(
		const PasswordFields &fields,
		Fn<void()> submit) {
	const auto confirm = fields.confirm;
	const auto chain = [=] {
		if (!confirm || confirm->hasFocus()) {
			submit();
		} else {
			confirm->setFocus();
		}
	};
	QObject::connect(fields.input, &Ui::MaskedInputField::submitted, chain);
	if (confirm) {
		QObject::connect(confirm, &Ui::MaskedInputField::submitted, chain);
	}
}

void FocusPasswordFields(const PasswordFields &fields) {
	if (!fields.confirm || fields.input->text().isEmpty()) {
		fields.input->setFocus();
	} else if (fields.confirm->text().isEmpty()) {
		fields.confirm->setFocus();
	} else {
		fields.input->setFocus();
	}
}

} // namespace Settings::CloudPassword
