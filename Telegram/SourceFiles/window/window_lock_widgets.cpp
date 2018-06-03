/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_lock_widgets.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "messenger.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "styles/style_boxes.h"
#include "window/window_slide_animation.h"
#include "window/window_controller.h"
#include "auth_session.h"

namespace Window {

LockWidget::LockWidget(QWidget *parent) : RpWidget(parent) {
	show();
}

void LockWidget::setInnerFocus() {
	if (const auto controller = App::wnd()->controller()) {
		controller->dialogsListFocused().set(false, true);
	}
	setFocus();
}

void LockWidget::showAnimated(const QPixmap &bgAnimCache, bool back) {
	_showBack = back;
	(_showBack ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.finish();

	showChildren();
	setInnerFocus();
	(_showBack ? _cacheUnder : _cacheOver) = Ui::GrabWidget(this);
	hideChildren();

	_a_show.start(
		[this] { animationCallback(); },
		0.,
		1.,
		st::slideDuration,
		Window::SlideAnimation::transition());
	show();
}

void LockWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		showChildren();
		if (App::wnd()) App::wnd()->setInnerFocus();

		Ui::showChatsList();

		_cacheUnder = _cacheOver = QPixmap();
	}
}

void LockWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto progress = _a_show.current(getms(), 1.);
	if (_a_show.animating()) {
		auto coordUnder = _showBack ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = _showBack ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = _showBack ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * cRetinaFactor(), 0, coordOver * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(coordOver, 0, _cacheOver);
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
	} else {
		paintContent(p);
	}
}

void LockWidget::paintContent(Painter &p) {
	p.fillRect(rect(), st::windowBg);
}

PasscodeLockWidget::PasscodeLockWidget(QWidget *parent)
: LockWidget(parent)
, _passcode(this, st::passcodeInput, langFactory(lng_passcode_ph))
, _submit(this, langFactory(lng_passcode_submit), st::passcodeSubmit)
, _logout(this, lang(lng_passcode_logout)) {
	connect(_passcode, &Ui::MaskedInputField::changed, [=] { changed(); });
	connect(_passcode, &Ui::MaskedInputField::submitted, [=] { submit(); });

	_submit->setClickedCallback([=] { submit(); });
	_logout->setClickedCallback([] { App::wnd()->onLogout(); });
}

void PasscodeLockWidget::paintContent(Painter &p) {
	LockWidget::paintContent(p);

	p.setFont(st::passcodeHeaderFont);
	p.setPen(st::windowFg);
	p.drawText(QRect(0, _passcode->y() - st::passcodeHeaderHeight, width(), st::passcodeHeaderHeight), lang(lng_passcode_enter), style::al_center);

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
		_error = lang(lng_flood_error);
		_passcode->showError();
		update();
		return;
	}

	if (App::main()) {
		if (Local::checkPasscode(_passcode->text().toUtf8())) {
			Messenger::Instance().unlockPasscode(); // Destroys this widget.
			return;
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			error();
			return;
		}
	} else {
		if (Local::readMap(_passcode->text().toUtf8()) != Local::ReadMapPassNeeded) {
			cSetPasscodeBadTries(0);

			Messenger::Instance().startMtp();

			// Destroys this widget.
			if (AuthSession::Exists()) {
				App::wnd()->setupMain();
			} else {
				App::wnd()->setupIntro();
			}
		} else {
			cSetPasscodeBadTries(cPasscodeBadTries() + 1);
			cSetPasscodeLastTry(getms(true));
			error();
			return;
		}
	}
}

void PasscodeLockWidget::error() {
	_error = lang(lng_passcode_wrong);
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

TermsLock TermsLock::FromMTP(const MTPDhelp_termsOfService &data) {
	return {
		bytes::make_vector(data.vid.c_dataJSON().vdata.v),
		TextWithEntities {
			TextUtilities::Clean(qs(data.vtext)),
			TextUtilities::EntitiesFromMTP(data.ventities.v) },
		(data.has_min_age_confirm()
			? base::make_optional(data.vmin_age_confirm.v)
			: base::none),
		data.is_popup()
	};
}

TermsBox::TermsBox(
	QWidget*,
	const TermsLock &data,
	Fn<QString()> agree,
	Fn<QString()> cancel)
: _data(data)
, _agree(agree)
, _cancel(cancel) {
}

TermsBox::TermsBox(
	QWidget*,
	const TextWithEntities &text,
	Fn<QString()> agree,
	Fn<QString()> cancel,
	bool attentionAgree)
: _data{ {}, text, base::none, false }
, _agree(agree)
, _cancel(cancel)
, _attentionAgree(attentionAgree) {
}

rpl::producer<> TermsBox::agreeClicks() const {
	return _agreeClicks.events();
}

rpl::producer<> TermsBox::cancelClicks() const {
	return _cancelClicks.events();
}

void TermsBox::prepare() {
	setTitle(langFactory(lng_terms_header));

	auto check = std::make_unique<Ui::CheckView>(st::defaultCheck, false);
	const auto ageCheck = check.get();
	const auto age = _data.minAge
		? Ui::CreateChild<Ui::PaddingWrap<Ui::Checkbox>>(
			this,
			object_ptr<Ui::Checkbox>(
				this,
				lng_terms_age(lt_count, *_data.minAge),
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
	content->entity()->setClickHandlerHook([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto link = handler
			? handler->copyToClipboardText()
			: QString();
		if (TextUtilities::RegExpMention().match(link).hasMatch()) {
			_lastClickedMention = link;
			Ui::Toast::Show(lng_terms_agree_to_proceed(lt_bot, link));
			return false;
		}
		return true;
	});

	const auto errorAnimationCallback = [=] {
		// lambda 'this' gets deleted in _ageErrorAnimation.current() call.
		const auto check = ageCheck;
		const auto error = _ageErrorAnimation.current(
			_ageErrorShown ? 1. : 0.);
		if (error == 0.) {
			check->setUntoggledOverride(base::none);
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
	addButton(_agree, [=] {}, agreeStyle)->clicks(
	) | rpl::filter([=] {
		if (age && !age->entity()->checked()) {
			toggleAgeError(true);
			return false;
		}
		return true;
	}) | rpl::start_to_stream(_agreeClicks, lifetime());

	if (_cancel) {
		addButton(_cancel, [=] {})->clicks(
		) | rpl::start_to_stream(_cancelClicks, lifetime());
	}

	if (age) {
		base::ObservableViewer(
			age->entity()->checkedChanged
		) | rpl::start_with_next([=] {
			toggleAgeError(false);
		}, lifetime());

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
