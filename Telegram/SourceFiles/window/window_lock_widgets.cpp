/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_lock_widgets.h"

#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "base/system_unlock.h"
#include "lang/lang_keys.h"
#include "storage/storage_domain.h"
#include "mainwindow.h"
#include "core/application.h"
#include "api/api_text_entities.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_session_controller.h"
#include "main/main_domain.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Window {
namespace {

constexpr auto kSystemUnlockDelay = crl::time(1000);

} // namespace

LockWidget::LockWidget(QWidget *parent, not_null<Controller*> window)
: RpWidget(parent)
, _window(window) {
	show();
}

LockWidget::~LockWidget() = default;

not_null<Controller*> LockWidget::window() const {
	return _window;
}

void LockWidget::setInnerFocus() {
	setFocus();
}

void LockWidget::showAnimated(QPixmap oldContentCache) {
	_showAnimation = nullptr;

	showChildren();
	setInnerFocus();
	auto newContentCache = Ui::GrabWidget(this);
	hideChildren();

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(oldContentCache, newContentCache);
	_showAnimation->start();

	show();
}

void LockWidget::showFinished() {
	showChildren();
	_window->widget()->setInnerFocus();
	_showAnimation = nullptr;
	if (const auto controller = _window->sessionController()) {
		controller->clearSectionStack();
	}
}

void LockWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	if (_showAnimation) {
		_showAnimation->paintContents(p);
		return;
	}
	paintContent(p);
}

void LockWidget::paintContent(QPainter &p) {
	p.fillRect(rect(), st::windowBg);
}

PasscodeLockWidget::PasscodeLockWidget(
	QWidget *parent,
	not_null<Controller*> window)
: LockWidget(parent, window)
, _passcode(this, st::passcodeInput, tr::lng_passcode_ph())
, _submit(this, tr::lng_passcode_submit(), st::passcodeSubmit)
, _logout(this, tr::lng_passcode_logout(tr::now)) {
	connect(_passcode, &Ui::MaskedInputField::changed, [=] { changed(); });
	connect(_passcode, &Ui::MaskedInputField::submitted, [=] { submit(); });

	_submit->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	_submit->setClickedCallback([=] { submit(); });
	_logout->setClickedCallback([=] {
		window->showLogoutConfirmation();
	});

	using namespace rpl::mappers;
	if (Core::App().settings().systemUnlockEnabled()) {
		_systemUnlockAvailable = base::SystemUnlockStatus(
			true
		) | rpl::map([](base::SystemUnlockAvailability status) {
			return status.withBiometrics
				? SystemUnlockType::Biometrics
				: status.withCompanion
				? SystemUnlockType::Companion
				: status.available
				? SystemUnlockType::Default
				: SystemUnlockType::None;
		});
		if (Core::App().domain().started()) {
			_systemUnlockAllowed = _systemUnlockAvailable.value();
			setupSystemUnlock();
		} else {
			setupSystemUnlockInfo();
		}
	}
}

void PasscodeLockWidget::setupSystemUnlockInfo() {
	const auto macos = [&] {
		return _systemUnlockAvailable.value(
		) | rpl::map([](SystemUnlockType type) {
			return (type == SystemUnlockType::Biometrics)
				? tr::lng_passcode_touchid()
				: (type == SystemUnlockType::Companion)
				? tr::lng_passcode_applewatch()
				: tr::lng_passcode_systempwd();
		}) | rpl::flatten_latest();
	};
	auto text = Platform::IsWindows()
		? tr::lng_passcode_winhello()
		: macos();
	const auto info = Ui::CreateChild<Ui::FlatLabel>(
		this,
		std::move(text),
		st::passcodeSystemUnlockLater);
	_logout->geometryValue(
	) | rpl::start_with_next([=](QRect logout) {
		info->resizeToWidth(width()
			- st::boxRowPadding.left()
			- st::boxRowPadding.right());
		info->moveToLeft(
			st::boxRowPadding.left(),
			logout.y() + logout.height() + st::passcodeSystemUnlockSkip);
	}, info->lifetime());
	info->showOn(_systemUnlockAvailable.value(
	) | rpl::map(rpl::mappers::_1 != SystemUnlockType::None));
}

void PasscodeLockWidget::setupSystemUnlock() {
	windowActiveValue() | rpl::skip(1) | rpl::filter([=](bool active) {
		return active
			&& !_systemUnlockSuggested
			&& !_systemUnlockCooldown.isActive();
	}) | rpl::start_with_next([=](bool) {
		[[maybe_unused]] auto refresh = base::SystemUnlockStatus();
		suggestSystemUnlock();
	}, lifetime());

	const auto button = Ui::CreateChild<Ui::IconButton>(
		_passcode.data(),
		st::passcodeSystemUnlock);
	if (!Platform::IsWindows()) {
		using namespace base;
		_systemUnlockAllowed.value(
		) | rpl::start_with_next([=](SystemUnlockType type) {
			const auto icon = (type == SystemUnlockType::Biometrics)
				? &st::passcodeSystemTouchID
				: (type == SystemUnlockType::Companion)
				? &st::passcodeSystemAppleWatch
				: &st::passcodeSystemSystemPwd;
			button->setIconOverride(icon, icon);
		}, button->lifetime());
	}
	button->showOn(_systemUnlockAllowed.value(
	) | rpl::map(rpl::mappers::_1 != SystemUnlockType::None));
	_passcode->sizeValue() | rpl::start_with_next([=](QSize size) {
		button->moveToRight(0, size.height() - button->height());
	}, button->lifetime());
	button->setClickedCallback([=] {
		const auto delay = st::passcodeSystemUnlock.ripple.hideDuration;
		base::call_delayed(delay, this, [=] {
			suggestSystemUnlock();
		});
	});
}

void PasscodeLockWidget::suggestSystemUnlock() {
	InvokeQueued(this, [=] {
		if (_systemUnlockSuggested) {
			return;
		}
		_systemUnlockCooldown.cancel();

		using namespace base;
		_systemUnlockAllowed.value(
		) | rpl::filter(
			rpl::mappers::_1 != SystemUnlockType::None
		) | rpl::take(1) | rpl::start_with_next([=] {
			const auto weak = base::make_weak(this);
			const auto done = [weak](SystemUnlockResult result) {
				crl::on_main([=] {
					if (const auto strong = weak.get()) {
						strong->systemUnlockDone(result);
					}
				});
			};
			SuggestSystemUnlock(
				this,
				(::Platform::IsWindows()
					? tr::lng_passcode_winhello_unlock(tr::now)
					: tr::lng_passcode_touchid_unlock(tr::now)),
				done);
		}, _systemUnlockSuggested);
	});
}

void PasscodeLockWidget::systemUnlockDone(base::SystemUnlockResult result) {
	if (result == base::SystemUnlockResult::Success) {
		Core::App().unlockPasscode();
		return;
	}
	_systemUnlockCooldown.callOnce(kSystemUnlockDelay);
	_systemUnlockSuggested.destroy();
	if (result == base::SystemUnlockResult::FloodError) {
		_error = tr::lng_flood_error(tr::now);
		_passcode->setFocusFast();
		update();
	}
}

void PasscodeLockWidget::paintContent(QPainter &p) {
	LockWidget::paintContent(p);

	p.setFont(st::passcodeHeaderFont);
	p.setPen(st::windowFg);
	p.drawText(QRect(0, _passcode->y() - st::passcodeHeaderHeight, width(), st::passcodeHeaderHeight), tr::lng_passcode_enter(tr::now), style::al_center);

	if (!_error.isEmpty()) {
		p.setFont(st::boxTextFont);
		p.setPen(st::boxTextFgError);
		p.drawText(QRect(0, _passcode->y() + _passcode->height(), width(), st::passcodeSubmitSkip), _error, style::al_center);
	}
}

void PasscodeLockWidget::submit() {
	if (_passcode->text().isEmpty()) {
		_passcode->showError();
		return;
	}
	if (!passcodeCanTry()) {
		_error = tr::lng_flood_error(tr::now);
		_passcode->showError();
		update();
		return;
	}

	const auto passcode = _passcode->text().toUtf8();
	auto &domain = Core::App().domain();
	const auto correct = domain.started()
		? domain.local().checkPasscode(passcode)
		: (domain.start(passcode) == Storage::StartResult::Success);
	if (!correct) {
		cSetPasscodeBadTries(cPasscodeBadTries() + 1);
		cSetPasscodeLastTry(crl::now());
		error();
		return;
	}

	Core::App().unlockPasscode(); // Destroys this widget.
}

void PasscodeLockWidget::error() {
	_error = tr::lng_passcode_wrong(tr::now);
	_passcode->selectAll();
	_passcode->showError();
	update();
}

void PasscodeLockWidget::changed() {
	if (!_error.isEmpty()) {
		_error = QString();
		update();
	}
}

void PasscodeLockWidget::resizeEvent(QResizeEvent *e) {
	_passcode->move((width() - _passcode->width()) / 2, (height() / 3));
	_submit->move(_passcode->x(), _passcode->y() + _passcode->height() + st::passcodeSubmitSkip);
	_logout->move(_passcode->x() + (_passcode->width() - _logout->width()) / 2, _submit->y() + _submit->height() + st::linkFont->ascent);
}

void PasscodeLockWidget::setInnerFocus() {
	LockWidget::setInnerFocus();
	_passcode->setFocusFast();
}

TermsLock TermsLock::FromMTP(
		Main::Session *session,
		const MTPDhelp_termsOfService &data) {
	const auto minAge = data.vmin_age_confirm();
	return {
		bytes::make_vector(data.vid().c_dataJSON().vdata().v),
		TextWithEntities {
			qs(data.vtext()),
			Api::EntitiesFromMTP(session, data.ventities().v) },
		(minAge ? std::make_optional(minAge->v) : std::nullopt),
		data.is_popup()
	};
}

TermsBox::TermsBox(
	QWidget*,
	const TermsLock &data,
	rpl::producer<QString> agree,
	rpl::producer<QString> cancel)
: _data(data)
, _agree(std::move(agree))
, _cancel(std::move(cancel)) {
}

TermsBox::TermsBox(
	QWidget*,
	const TextWithEntities &text,
	rpl::producer<QString> agree,
	rpl::producer<QString> cancel,
	bool attentionAgree)
: _data{ {}, text, std::nullopt, false }
, _agree(std::move(agree))
, _cancel(std::move(cancel))
, _attentionAgree(attentionAgree) {
}

rpl::producer<> TermsBox::agreeClicks() const {
	return _agreeClicks.events();
}

rpl::producer<> TermsBox::cancelClicks() const {
	return _cancelClicks.events();
}

void TermsBox::prepare() {
	setTitle(tr::lng_terms_header());

	auto check = std::make_unique<Ui::CheckView>(st::defaultCheck, false);
	const auto ageCheck = check.get();
	const auto age = _data.minAge
		? Ui::CreateChild<Ui::PaddingWrap<Ui::Checkbox>>(
			this,
			object_ptr<Ui::Checkbox>(
				this,
				tr::lng_terms_age(tr::now, lt_count, *_data.minAge),
				st::defaultCheckbox,
				std::move(check)),
			st::termsAgePadding)
		: nullptr;
	if (age) {
		age->resizeToNaturalWidth(st::boxWideWidth);
	}

	const auto content = setInnerWidget(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel> (
				this,
				rpl::single(_data.text),
				st::termsContent),
			st::termsPadding),
		0,
		age ? age->height() : 0);
	const auto show = uiShow();
	content->entity()->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto link = handler
			? handler->copyToClipboardText()
			: QString();
		if (TextUtilities::RegExpMention().match(link).hasMatch()) {
			_lastClickedMention = link;
			show->showToast(
				tr::lng_terms_agree_to_proceed(tr::now, lt_bot, link));
			return false;
		}
		return true;
	});

	const auto errorAnimationCallback = [=] {
		const auto check = ageCheck;
		const auto error = _ageErrorAnimation.value(
			_ageErrorShown ? 1. : 0.);
		if (error == 0.) {
			check->setUntoggledOverride(std::nullopt);
		} else {
			const auto color = anim::color(
				st::defaultCheck.untoggledFg,
				st::boxTextFgError,
				error);
			check->setUntoggledOverride(color);
		}
	};
	const auto toggleAgeError = [=](bool shown) {
		if (_ageErrorShown != shown) {
			_ageErrorShown = shown;
			_ageErrorAnimation.start(
				[=] { errorAnimationCallback(); },
				_ageErrorShown ? 0. : 1.,
				_ageErrorShown ? 1. : 0.,
				st::defaultCheck.duration);
		}
	};

	const auto &agreeStyle = _attentionAgree
		? st::attentionBoxButton
		: st::defaultBoxButton;
	addButton(std::move(_agree), [=] {}, agreeStyle)->clicks(
	) | rpl::filter([=] {
		if (age && !age->entity()->checked()) {
			toggleAgeError(true);
			return false;
		}
		return true;
	}) | rpl::to_empty | rpl::start_to_stream(_agreeClicks, lifetime());

	if (_cancel) {
		addButton(std::move(_cancel), [] {})->clicks(
		) | rpl::to_empty | rpl::start_to_stream(_cancelClicks, lifetime());
	}

	if (age) {
		age->entity()->checkedChanges(
		) | rpl::start_with_next([=] {
			toggleAgeError(false);
		}, age->lifetime());

		heightValue(
		) | rpl::start_with_next([=](int height) {
			age->moveToLeft(0, height - age->height());
		}, age->lifetime());
	}

	content->resizeToWidth(st::boxWideWidth);

	using namespace rpl::mappers;
	rpl::combine(
		content->heightValue(),
		age ? age->heightValue() : rpl::single(0),
		_1 + _2
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, content->lifetime());
}

void TermsBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		_agreeClicks.fire({});
	} else {
		BoxContent::keyPressEvent(e);
	}
}

QString TermsBox::lastClickedMention() const {
	return _lastClickedMention;
}

} // namespace Window
