/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_local_passcode.h"

#include "base/platform/base_platform_last_input.h"
#include "boxes/auto_lock_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "storage/storage_domain.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

void SetPasscode(
		not_null<Window::SessionController*> controller,
		const QString &pass) {
	cSetPasscodeBadTries(0);
	controller->session().domain().local().setPasscode(pass.toUtf8());
	Core::App().localPasscodeChanged();
}

} // namespace

namespace details {

class LocalPasscodeEnter : public AbstractSection {
public:
	enum class EnterType {
		Create,
		Check,
		Change,
	};

	LocalPasscodeEnter(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LocalPasscodeEnter();

	void showFinished() override;
	void setInnerFocus() override;
	[[nodiscard]] rpl::producer<Type> sectionShowOther() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;

	[[nodiscard]] rpl::producer<QString> title() override;

protected:
	void setupContent();

	[[nodiscard]] virtual EnterType enterType() const = 0;

private:

	const not_null<Window::SessionController*> _controller;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _setInnerFocus;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;

};

LocalPasscodeEnter::LocalPasscodeEnter(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent)
, _controller(controller) {
}

rpl::producer<QString> LocalPasscodeEnter::title() {
	return tr::lng_settings_passcode_title();
}

void LocalPasscodeEnter::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto isCreate = (enterType() == EnterType::Create);
	const auto isCheck = (enterType() == EnterType::Check);
	[[maybe_unused]] const auto isChange = (enterType() == EnterType::Change);

	auto icon = CreateLottieIcon(
		content,
		{
			.name = u"local_passcode_enter"_q,
			.sizeOverride = {
				st::changePhoneIconSize,
				st::changePhoneIconSize,
			},
		},
		st::settingLocalPasscodeIconPadding);
	content->add(std::move(icon.widget));
	_showFinished.events(
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, content->lifetime());

	if (isChange) {
		CloudPassword::SetupAutoCloseTimer(
			content->lifetime(),
			[=] { _showBack.fire({}); });
	}

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				isCreate
					? tr::lng_passcode_create_title()
					: isCheck
					? tr::lng_passcode_check_title()
					: tr::lng_passcode_change_title(),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding);

	const auto addDescription = [&](rpl::producer<QString> &&text) {
		const auto &st = st::settingLocalPasscodeDescription;
		content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(content, std::move(text), st)),
			st::changePhoneDescriptionPadding);
	};

	addDescription(tr::lng_passcode_about1());
	Ui::AddSkip(content);
	addDescription(tr::lng_passcode_about2());

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto addField = [&](rpl::producer<QString> &&text) {
		const auto &st = st::settingLocalPasscodeInputField;
		auto container = object_ptr<Ui::RpWidget>(content);
		container->resize(container->width(), st.heightMin);
		const auto field = Ui::CreateChild<Ui::PasswordInput>(
			container.data(),
			st,
			std::move(text));

		container->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			field->moveToLeft((r.width() - field->width()) / 2, 0);
		}, container->lifetime());

		content->add(std::move(container));
		return field;
	};

	const auto addError = [&](not_null<Ui::PasswordInput*> input) {
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
		QObject::connect(input.get(), &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
		return error;
	};

	const auto newPasscode = addField(isCreate
		? tr::lng_passcode_enter_first()
		: tr::lng_passcode_enter());

	const auto reenterPasscode = isCheck
		? (Ui::PasswordInput*)(nullptr)
		: addField(tr::lng_passcode_confirm_new());
	const auto error = addError(isCheck ? newPasscode : reenterPasscode);

	const auto button = content->add(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			content,
			object_ptr<Ui::RoundButton>(
				content,
				isCreate
					? tr::lng_passcode_create_button()
					: isCheck
					? tr::lng_passcode_check_button()
					: tr::lng_passcode_change_button(),
				st::changePhoneButton)),
		st::settingLocalPasscodeButtonPadding)->entity();
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->setClickedCallback([=] {
		const auto newText = newPasscode->text();
		const auto reenterText = reenterPasscode
			? reenterPasscode->text()
			: QString();
		if (isCreate || isChange) {
			if (newText.isEmpty()) {
				newPasscode->setFocus();
				newPasscode->showError();
			} else if (reenterText.isEmpty()) {
				reenterPasscode->setFocus();
				reenterPasscode->showError();
			} else if (newText != reenterText) {
				reenterPasscode->setFocus();
				reenterPasscode->showError();
				reenterPasscode->selectAll();
				error->show();
				error->setText(tr::lng_passcode_differ(tr::now));
			} else {
				if (isChange) {
					const auto &domain = _controller->session().domain();
					if (domain.local().checkPasscode(newText.toUtf8())) {
						newPasscode->setFocus();
						newPasscode->showError();
						newPasscode->selectAll();
						error->show();
						error->setText(tr::lng_passcode_is_same(tr::now));
						return;
					}
				}
				SetPasscode(_controller, newText);
				if (isCreate) {
					_showOther.fire(LocalPasscodeManageId());
				} else if (isChange) {
					_showBack.fire({});
				}
			}
		} else if (isCheck) {
			if (!passcodeCanTry()) {
				newPasscode->setFocus();
				newPasscode->showError();
				error->show();
				error->setText(tr::lng_flood_error(tr::now));
				return;
			}
			const auto &domain = _controller->session().domain();
			if (domain.local().checkPasscode(newText.toUtf8())) {
				cSetPasscodeBadTries(0);
				_showOther.fire(LocalPasscodeManageId());
			} else {
				cSetPasscodeBadTries(cPasscodeBadTries() + 1);
				cSetPasscodeLastTry(crl::now());

				newPasscode->selectAll();
				newPasscode->setFocus();
				newPasscode->showError();
				error->show();
				error->setText(tr::lng_passcode_wrong(tr::now));
			}
		}
	});

	const auto submit = [=] {
		if (!reenterPasscode || reenterPasscode->hasFocus()) {
			button->clicked({}, Qt::LeftButton);
		} else {
			reenterPasscode->setFocus();
		}
	};
	connect(newPasscode, &Ui::MaskedInputField::submitted, submit);
	if (reenterPasscode) {
		connect(reenterPasscode, &Ui::MaskedInputField::submitted, submit);
	}

	_setInnerFocus.events(
	) | rpl::start_with_next([=] {
		if (newPasscode->text().isEmpty()) {
			newPasscode->setFocus();
		} else if (reenterPasscode && reenterPasscode->text().isEmpty()) {
			reenterPasscode->setFocus();
		} else {
			newPasscode->setFocus();
		}
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

void LocalPasscodeEnter::showFinished() {
	_showFinished.fire({});
}

void LocalPasscodeEnter::setInnerFocus() {
	_setInnerFocus.fire({});
}

rpl::producer<Type> LocalPasscodeEnter::sectionShowOther() {
	return _showOther.events();
}

rpl::producer<> LocalPasscodeEnter::sectionShowBack() {
	return _showBack.events();
}

LocalPasscodeEnter::~LocalPasscodeEnter() = default;

} // namespace details

class LocalPasscodeCreate;
class LocalPasscodeCheck;
class LocalPasscodeChange;

template <typename SectionType>
class TypedLocalPasscodeEnter : public details::LocalPasscodeEnter {
public:
	TypedLocalPasscodeEnter(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: details::LocalPasscodeEnter(parent, controller) {
		setupContent();
	}

	[[nodiscard]] static Type Id() {
		return SectionFactory<SectionType>::Instance();
	}
	[[nodiscard]] Type id() const final override {
		return Id();
	}

protected:
	[[nodiscard]] EnterType enterType() const final override {
		if constexpr (std::is_same_v<SectionType, LocalPasscodeCreate>) {
			return EnterType::Create;
		}
		if constexpr (std::is_same_v<SectionType, LocalPasscodeCheck>) {
			return EnterType::Check;
		}
		if constexpr (std::is_same_v<SectionType, LocalPasscodeChange>) {
			return EnterType::Change;
		}
		return EnterType::Create;
	}

};

class LocalPasscodeCreate final
	: public TypedLocalPasscodeEnter<LocalPasscodeCreate> {
public:
	using TypedLocalPasscodeEnter::TypedLocalPasscodeEnter;

};

class LocalPasscodeCheck final
	: public TypedLocalPasscodeEnter<LocalPasscodeCheck> {
public:
	using TypedLocalPasscodeEnter::TypedLocalPasscodeEnter;

};

class LocalPasscodeChange final
	: public TypedLocalPasscodeEnter<LocalPasscodeChange> {
public:
	using TypedLocalPasscodeEnter::TypedLocalPasscodeEnter;

};

class LocalPasscodeManage : public Section<LocalPasscodeManage> {
public:
	LocalPasscodeManage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LocalPasscodeManage();

	[[nodiscard]] rpl::producer<QString> title() override;

	void showFinished() override;
	[[nodiscard]] rpl::producer<Type> sectionShowOther() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;

	[[nodiscard]] rpl::producer<std::vector<Type>> removeFromStack() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;

	rpl::variable<bool> _isBottomFillerShown;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;

};

LocalPasscodeManage::LocalPasscodeManage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> LocalPasscodeManage::title() {
	return tr::lng_settings_passcode_title();
}

rpl::producer<std::vector<Type>> LocalPasscodeManage::removeFromStack() {
	return rpl::single(std::vector<Type>{
		LocalPasscodeManage::Id(),
		LocalPasscodeCreate::Id(),
		LocalPasscodeCheck::Id(),
		LocalPasscodeChange::Id(),
	});
}

void LocalPasscodeManage::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	struct State {
		rpl::event_stream<> autoLockBoxClosing;
	};
	const auto state = content->lifetime().make_state<State>();

	CloudPassword::SetupAutoCloseTimer(
		content->lifetime(),
		[=] { _showBack.fire({}); });

	Ui::AddSkip(content);

	AddButtonWithIcon(
		content,
		tr::lng_passcode_change(),
		st::settingsButton,
		{ &st::menuIconLock }
	)->addClickHandler([=] {
		_showOther.fire(LocalPasscodeChange::Id());
	});

	auto autolockLabel = state->autoLockBoxClosing.events_starting_with(
		{}
	) | rpl::map([] {
		const auto autolock = Core::App().settings().autoLock();
		const auto hours = autolock / 3600;
		const auto minutes = (autolock - (hours * 3600)) / 60;

		return (hours && minutes)
			? tr::lng_passcode_autolock_hours_minutes(
				tr::now,
				lt_hours_count,
				QString::number(hours),
				lt_minutes_count,
				QString::number(minutes))
			: minutes
			? tr::lng_minutes(tr::now, lt_count, minutes)
			: tr::lng_hours(tr::now, lt_count, hours);
	});

	AddButtonWithLabel(
		content,
		(base::Platform::LastUserInputTimeSupported()
			? tr::lng_passcode_autolock_away
			: tr::lng_passcode_autolock_inactive)(),
		std::move(autolockLabel),
		st::settingsButton,
		{ &st::menuIconTimer }
	)->addClickHandler([=] {
		const auto box = _controller->show(Box<AutoLockBox>());
		box->boxClosing(
		) | rpl::start_to_stream(state->autoLockBoxClosing, box->lifetime());
	});

	Ui::AddSkip(content);

	using Divider = CloudPassword::OneEdgeBoxContentDivider;
	const auto divider = Ui::CreateChild<Divider>(this);
	divider->lower();
	const auto about = content->add(
		object_ptr<Ui::PaddingWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::combine(
					tr::lng_passcode_about1(),
					tr::lng_passcode_about3()
				) | rpl::map([](const QString &s1, const QString &s2) {
					return s1 + "\n\n" + s2;
				}),
				st::boxDividerLabel),
		st::defaultBoxDividerLabelPadding));
	about->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
	_isBottomFillerShown.value(
	) | rpl::start_with_next([=](bool shown) {
		divider->skipEdge(Qt::BottomEdge, shown);
	}, divider->lifetime());

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> LocalPasscodeManage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	auto callback = [=] {
		_controller->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_settings_passcode_disable_sure(),
				.confirmed = [=](Fn<void()> &&close) {
					SetPasscode(_controller, QString());

					close();
					_showBack.fire({});
				},
				.confirmText = tr::lng_settings_auto_night_disable(),
				.confirmStyle = &st::attentionBoxButton,
			}));
	};
	auto bottomButton = CloudPassword::CreateBottomDisableButton(
		parent,
		geometryValue(),
		tr::lng_settings_passcode_disable(),
		std::move(callback));

	_isBottomFillerShown = base::take(bottomButton.isBottomFillerShown);

	return bottomButton.content;
}

void LocalPasscodeManage::showFinished() {
	_showFinished.fire({});
}

rpl::producer<Type> LocalPasscodeManage::sectionShowOther() {
	return _showOther.events();
}

rpl::producer<> LocalPasscodeManage::sectionShowBack() {
	return _showBack.events();
}

LocalPasscodeManage::~LocalPasscodeManage() = default;

Type LocalPasscodeCreateId() {
	return LocalPasscodeCreate::Id();
}

Type LocalPasscodeCheckId() {
	return LocalPasscodeCheck::Id();
}

Type LocalPasscodeManageId() {
	return LocalPasscodeManage::Id();
}

} // namespace Settings
