/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_setup_email.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "data/components/promo_suggestions.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "intro/intro_code_input.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/sent_code_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Window {
namespace {

[[nodiscard]] Settings::LottieIcon CreateEmailIcon(
		not_null<Ui::VerticalLayout*> container) {
	return Settings::CreateLottieIcon(
		container,
		{
			.name = u"cloud_password/email"_q,
			.sizeOverride = st::normalBoxLottieSize,
		},
		{});
}

} // namespace

SetupEmailLockWidget::SetupEmailLockWidget(
	QWidget *parent,
	not_null<Controller*> window)
: LockWidget(parent, window)
, _layout(this)
, _confirmWidget(nullptr) {
	const auto session = window->maybeSession();
	const auto state = session
		? session->promoSuggestions().setupEmailState()
		: Data::SetupEmailState::Setup;

	const auto noSkip = state == Data::SetupEmailState::SetupNoSkip;
	const auto accountsCount = session
		? session->domain().accountsAuthedCount()
		: 0;

	if (!window->isPrimary()) {
	// if (state == Data::SetupEmailState::SettingUp
	// 	|| state == Data::SetupEmailState::SettingUpNoSkip) {
		auto icon = CreateEmailIcon(_layout);
		_iconAnimate = std::move(icon.animate);
		_layout->add(std::move(icon.widget));
		base::call_delayed(st::slideDuration, crl::guard(this, [=] {
			if (_iconAnimate) {
				_iconAnimate(anim::repeat::once);
			}
		}));

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		_layout->add(
			object_ptr<Ui::FlatLabel>(
				_layout,
				tr::lng_settings_cloud_login_email_title(),
				st::lockSetupEmailTitle),
			st::boxRowPadding,
			style::al_top);

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		_layout->add(
			object_ptr<Ui::FlatLabel>(
				_layout,
				tr::lng_settings_cloud_login_email_busy(),
				st::lockSetupEmailLabel),
			st::boxRowPadding,
			style::al_top);
	} else {
		if (!noSkip) {
			_backButton = object_ptr<Ui::IconButton>(
				this,
				st::introBackButton);
			if (session) {
				session->promoSuggestions().setSetupEmailState(
					Data::SetupEmailState::SettingUp);
				_backButton->setClickedCallback([=] {
					session->promoSuggestions().dismissSetupEmail([=] {
					});
				});
			}
		} else if (accountsCount <= 1) {
			_logoutButton = object_ptr<Ui::RoundButton>(
				this,
				tr::lng_settings_logout(),
				st::defaultBoxButton);
			_logoutButton->setTextTransform(
				Ui::RoundButton::TextTransform::NoTransform);
			if (session) {
				session->promoSuggestions().setSetupEmailState(
					Data::SetupEmailState::SettingUpNoSkip);
				_logoutButton->setClickedCallback([=] {
					window->showLogoutConfirmation();
				});
			}
		} else {
			_backButton = object_ptr<Ui::IconButton>(
				this,
				st::introBackButton);
			if (session) {
				_backButton->setClickedCallback([=] {
					showAccountsMenu();
				});
			}
		}

#ifdef _DEBUG
		if (session->isTestMode()) {
			_debugButton = object_ptr<Ui::RoundButton>(
				this,
				rpl::single(u"[DEBUG] Clear bio"_q),
				st::defaultBoxButton);
			_debugButton->setTextTransform(
				Ui::RoundButton::TextTransform::NoTransform);
			_debugButton->setClickedCallback([=] {
				session->api().saveSelfBio({});
			});
		}
#endif

		auto icon = CreateEmailIcon(_layout);
		_iconAnimate = std::move(icon.animate);
		_layout->add(std::move(icon.widget));
		base::call_delayed(st::slideDuration, crl::guard(this, [=] {
			if (_iconAnimate) {
				_iconAnimate(anim::repeat::once);
			}
		}));

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		_layout->add(
			object_ptr<Ui::FlatLabel>(
				_layout,
				tr::lng_settings_cloud_login_email_title(),
				st::lockSetupEmailTitle),
			st::boxRowPadding,
			style::al_top);

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		_layout->add(
			object_ptr<Ui::FlatLabel>(
				_layout,
				tr::lng_settings_cloud_login_email_about(),
				st::lockSetupEmailLabel),
			st::boxRowPadding,
			style::al_top);

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		auto emailInput = _layout->add(
			object_ptr<Ui::InputField>(
				_layout,
				st::settingLocalPasscodeInputField,
				tr::lng_settings_cloud_login_email_placeholder()),
			st::boxRowPadding,
			style::al_top);

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		auto errorLabel = _layout->add(
			object_ptr<Ui::FlatLabel>(
				_layout,
				QString(),
				st::lockSetupEmailLabel),
			{},
			style::al_top);
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);
		Ui::AddSkip(_layout);

		auto submit = _layout->add(
			object_ptr<Ui::RoundButton>(
				_layout,
				tr::lng_settings_cloud_login_email_confirm(),
				st::changePhoneButton),
			st::boxRowPadding,
			style::al_top);
		submit->setTextTransform(
			Ui::RoundButton::TextTransform::NoTransform);

		_emailInput = emailInput;
		_errorLabel = errorLabel;
		_submit = submit;

		submit->setClickedCallback([=] { this->submit(); });

		emailInput->changes() | rpl::on_next([=] {
			_error = QString();
			errorLabel->hide();
		}, emailInput->lifetime());

		emailInput->submits() | rpl::on_next([=] {
			this->submit();
		}, emailInput->lifetime());
	}
}

void SetupEmailLockWidget::paintContent(QPainter &p) {
	if (_backAnimation) {
		_backAnimation->paintContents(p);
		return;
	}

	LockWidget::paintContent(p);
}

void SetupEmailLockWidget::submit() {
	if (!_emailInput) {
		return;
	}
	const auto email = _emailInput->getLastText().trimmed();
	if (email.isEmpty()) {
		_emailInput->setFocus();
		_emailInput->showError();
		return;
	}

	if (!email.contains('@') || !email.contains('.')) {
		_error = tr::lng_cloud_password_bad_email(tr::now);
		_errorLabel->setText(_error);
		_errorLabel->show();
		_emailInput->setFocus();
		_emailInput->showError();
		_emailInput->selectAll();
		return;
	}

	const auto session = window()->maybeSession();
	if (!session) {
		showConfirmWidget(email, 6, QString());
		return;
	}

	_api.emplace(&session->mtp());
	const auto done = crl::guard(this, [=](
			int length,
			const QString &pattern) {
		_api.reset();
		showConfirmWidget(email, length, pattern);
	});
	const auto fail = crl::guard(this, [=](const QString &type) {
		_api.reset();
		if (MTP::IsFloodError(type)) {
			_error = tr::lng_flood_error(tr::now);
		} else if (type == u"EMAIL_INVALID"_q) {
			_error = tr::lng_cloud_password_bad_email(tr::now);
		} else {
			_error = tr::lng_cloud_password_bad_email(tr::now);
		}
		_errorLabel->setText(_error);
		_errorLabel->show();
		_emailInput->setFocus();
		_emailInput->showError();
		_emailInput->selectAll();
	});
	Api::RequestLoginEmailCode(*_api, email, done, fail);
}

void SetupEmailLockWidget::showConfirmWidget(
		const QString &email,
		int codeLength,
		const QString &emailPattern) {
	_layout->hide();
	auto oldContentCache = grabContent();

	_confirmWidget = Ui::CreateChild<SetupEmailConfirmWidget>(
		this,
		window(),
		email,
		codeLength,
		emailPattern);
	_confirmWidget->resize(size());
	_confirmWidget->show();
	_confirmWidget->showAnimated(std::move(oldContentCache));
	_confirmWidget->setFocus();

	_confirmWidget->backRequests(
	) | rpl::on_next([=] {
		showEmailInput();
	}, _confirmWidget->lifetime());

	_confirmWidget->confirmations(
	) | rpl::on_next([=, controller = window()] {
		if (const auto session = controller->maybeSession()) {
			session->promoSuggestions().setSetupEmailState(
				Data::SetupEmailState::None);
		}
	}, _confirmWidget->lifetime());
}

void SetupEmailLockWidget::showEmailInput() {
	if (!_confirmWidget) {
		return;
	}

	auto oldContentCache = Ui::GrabWidget(_confirmWidget);
	_confirmWidget->hide();
	_confirmWidget->deleteLater();
	_confirmWidget = nullptr;

	if (_logoutButton) {
		_logoutButton->show();
	}
	_layout->show();
	auto newContentCache = grabContent();
	if (_logoutButton) {
		_logoutButton->hide();
	}
	_layout->hide();

	_backAnimation = std::make_unique<SlideAnimation>();
	_backAnimation->setDirection(SlideDirection::FromLeft);
	_backAnimation->setRepaintCallback([=] { update(); });
	_backAnimation->setFinishedCallback([=] {
		_layout->show();
		if (_logoutButton) {
			_logoutButton->show();
		}
		_backAnimation.reset();
		if (_iconAnimate) {
			_iconAnimate(anim::repeat::once);
		}
	});
	_backAnimation->setPixmaps(
		std::move(oldContentCache),
		std::move(newContentCache));
	_backAnimation->start();

	if (_emailInput) {
		_emailInput->setFocus();
	}
}

QPixmap SetupEmailLockWidget::grabContent() {
	return Ui::GrabWidget(this);
}

void SetupEmailLockWidget::resizeEvent(QResizeEvent *e) {
	const auto layoutWidth = std::min(
		width(),
		st::lockSetupEmailLabelMaxWidth);
	_layout->resizeToWidth(layoutWidth);
	_layout->moveToLeft(
		(width() - layoutWidth) / 2,
		st::infoLayerTopBarHeight);

	if (_backButton) {
		_backButton->moveToLeft(0, 0);
	}

	if (_logoutButton) {
		_logoutButton->move(st::lockSetupEmailLogOutPosition);
	}

	if (_debugButton) {
		_debugButton->moveToLeft(
			st::lockSetupEmailLogOutPosition.x(),
			height() - _debugButton->height()
				- st::lockSetupEmailLogOutPosition.y());
	}

	if (_confirmWidget) {
		_confirmWidget->resize(size());
	}
}

void SetupEmailLockWidget::setInnerFocus() {
	LockWidget::setInnerFocus();
	if (_confirmWidget && _confirmWidget->isVisible()) {
		_confirmWidget->setFocus();
	} else if (_emailInput) {
		_emailInput->setFocus();
	}
}

SetupEmailConfirmWidget::SetupEmailConfirmWidget(
	QWidget *parent,
	not_null<Controller*> window,
	const QString &email,
	int codeLength,
	const QString &emailPattern)
: Ui::RpWidget(parent)
, _window(window)
, _email(email)
, _emailPattern(emailPattern)
, _backButton(this, st::introBackButton)
, _layout(this) {
	auto icon = CreateEmailIcon(_layout);
	_iconAnimate = std::move(icon.animate);
	_layout->add(std::move(icon.widget));

	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);

	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_settings_cloud_login_email_code_title(),
			st::lockSetupEmailTitle),
		st::boxRowPadding,
		style::al_top);

	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);

	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_settings_cloud_login_email_code_about(
				tr::now,
				lt_email,
				_emailPattern.isEmpty() ? _email : _emailPattern),
			st::lockSetupEmailLabel),
		st::boxRowPadding,
		style::al_top);

	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);

	auto codeInput = _layout->add(
		object_ptr<Ui::CodeInput>(_layout),
		style::al_top);
	codeInput->setDigitsCountMax(codeLength > 0 ? codeLength : 6);

	Ui::AddSkip(_layout);
	Ui::AddSkip(_layout);

	auto errorLabel = _layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			QString(),
			st::lockSetupEmailLabel),
		st::boxRowPadding,
		style::al_top);
	errorLabel->setTextColorOverride(st::boxTextFgError->c);
	errorLabel->hide();

	_codeInput = codeInput;
	_errorLabel = errorLabel;

	_backButton->clicks(
	) | rpl::to_empty | rpl::start_to_stream(_backRequests, lifetime());

	base::install_event_filter(codeInput, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto k = static_cast<QKeyEvent*>(e.get());
			if (k->key() == Qt::Key_Escape) {
				_backRequests.fire({});
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});

	codeInput->codeCollected(
	) | rpl::on_next([=](const QString &code) {
		verifyCode(code);
	}, codeInput->lifetime());
}

rpl::producer<> SetupEmailConfirmWidget::backRequests() const {
	return _backRequests.events();
}

rpl::producer<> SetupEmailConfirmWidget::confirmations() const {
	return _confirmations.events();
}

void SetupEmailConfirmWidget::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	if (_showAnimation) {
		_showAnimation->paintContents(p);
		return;
	}

	p.fillRect(rect(), st::windowBg);
}

void SetupEmailConfirmWidget::showAnimated(QPixmap oldContentCache) {
	_showAnimation = nullptr;

	showChildren();
	auto newContentCache = Ui::GrabWidget(this);
	hideChildren();

	_showAnimation = std::make_unique<SlideAnimation>();
	_showAnimation->setDirection(SlideDirection::FromRight);
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] {
		showChildren();
		_showAnimation.reset();
		if (_iconAnimate) {
			_iconAnimate(anim::repeat::once);
		}
		if (_codeInput) {
			_codeInput->setFocus();
		}
	});
	_showAnimation->setPixmaps(
		std::move(oldContentCache),
		std::move(newContentCache));
	_showAnimation->start();
}

void SetupEmailConfirmWidget::resizeEvent(QResizeEvent *e) {
	const auto layoutWidth = std::min(
		width(),
		st::lockSetupEmailLabelMaxWidth);
	_layout->resizeToWidth(layoutWidth);
	_layout->move((width() - layoutWidth) / 2, st::infoLayerTopBarHeight);
	_backButton->move(0, 0);
}

void SetupEmailConfirmWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_backRequests.fire({});
		return;
	}
	Ui::RpWidget::keyPressEvent(e);
}

void SetupEmailConfirmWidget::verifyCode(const QString &code) {
	const auto session = _window->maybeSession();
	if (!session) {
		_confirmations.fire({});
		return;
	}

	_api.emplace(&session->mtp());
	const auto done = crl::guard(this, [=] {
#ifdef _DEBUG
		if (session->isTestMode()) {
			session->api().saveSelfBio({});
		}
#endif
		_window->uiShow()->showToast(
			tr::lng_settings_cloud_login_email_set_success(tr::now));
		_api.reset();
		_confirmations.fire({});
	});
	const auto fail = crl::guard(this, [=](const QString &type) {
		_api.reset();
		if (type.isEmpty()) {
			_confirmations.fire({});
			return;
		}
		if (MTP::IsFloodError(type)) {
			_error = tr::lng_flood_error(tr::now);
		} else if (type == u"EMAIL_NOT_ALLOWED"_q) {
			_error = tr::lng_settings_error_email_not_alowed(tr::now);
		} else if (type == u"CODE_INVALID"_q) {
			_error = tr::lng_signin_wrong_code(tr::now);
		} else if (type == u"CODE_EXPIRED"_q
			|| type == u"EMAIL_HASH_EXPIRED"_q) {
			_error = Lang::Hard::EmailConfirmationExpired();
		} else {
			_error = Lang::Hard::ServerError();
		}
		_errorLabel->setText(_error);
		_errorLabel->show();
		_codeInput->setFocus();
		_codeInput->showError();
	});
	Api::VerifyLoginEmail(*_api, code, done, fail);
}

void SetupEmailLockWidget::showAccountsMenu() {
	const auto session = window()->maybeSession();
	if (!session) {
		return;
	}

	const auto &st = st::popupMenuWithIcons;

	_accountsMenu = base::make_unique_q<Ui::PopupMenu>(this, st);
	const auto accounts = session->domain().orderedAccounts();

	for (const auto &account : accounts) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto user = account->session().user();
		if (user == session->user()) {
			continue;
		}
		const auto action = new QAction(user->name(), _accountsMenu);
		QObject::connect(action, &QAction::triggered, [=] {
			Core::App().domain().maybeActivate(account);
		});
		auto owned = base::make_unique_q<Ui::Menu::Action>(
			_accountsMenu->menu(),
			_accountsMenu->menu()->st(),
			action,
			nullptr,
			nullptr);
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			owned.get(),
			user,
			st::lockSetupEmailUserpicSmall);
		userpic->move(st.menu.itemIconPosition);
		_accountsMenu->addAction(std::move(owned));
	}

	_accountsMenu->setForcedOrigin(Ui::PanelAnimation::Origin::TopLeft);
	_accountsMenu->popup(_backButton
		? mapToGlobal(QPoint(_backButton->x(), _backButton->height()))
		: QCursor::pos());
}

} // namespace Window
