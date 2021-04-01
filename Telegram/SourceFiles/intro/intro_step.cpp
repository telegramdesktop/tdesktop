/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_step.h"

#include "intro/intro_widget.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/slide_animation.h"
#include "data/data_user.h"
#include "data/data_auto_download.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "window/window_controller.h"
#include "window/themes/window_theme.h"
#include "app.h"
#include "styles/style_intro.h"
#include "styles/style_window.h"

namespace Intro {
namespace details {
namespace {

void PrepareSupportMode(not_null<Main::Session*> session) {
	using ::Data::AutoDownload::Full;

	anim::SetDisabled(true);
	Core::App().settings().setDesktopNotify(false);
	Core::App().settings().setSoundNotify(false);
	Core::App().settings().setFlashBounceNotify(false);
	Core::App().saveSettings();

	session->settings().autoDownload() = Full::FullDisabled();
	session->saveSettings();
}

} // namespace

Step::CoverAnimation::~CoverAnimation() = default;

Step::Step(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data,
	bool hasCover)
: RpWidget(parent)
, _account(account)
, _data(data)
, _hasCover(hasCover)
, _title(this, _hasCover ? st::introCoverTitle : st::introTitle)
, _description(
	this,
	object_ptr<Ui::FlatLabel>(
		this,
		_hasCover
			? st::introCoverDescription
			: st::introDescription)) {
	hide();
	subscribe(Window::Theme::Background(), [this](
			const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			if (!_coverMask.isNull()) {
				_coverMask = QPixmap();
				prepareCoverMask();
			}
		}
	});

	_errorText.value(
	) | rpl::start_with_next([=](const QString &text) {
		refreshError(text);
	}, lifetime());

	_titleText.value(
	) | rpl::start_with_next([=](const QString &text) {
		_title->setText(text);
		updateLabelsPosition();
	}, lifetime());

	_descriptionText.value(
	) | rpl::start_with_next([=](const TextWithEntities &text) {
		_description->entity()->setMarkedText(text);
		updateLabelsPosition();
	}, lifetime());
}

Step::~Step() = default;

MTP::Sender &Step::api() const {
	if (!_api) {
		_api.emplace(&_account->mtp());
	}
	return *_api;
}

void Step::apiClear() {
	_api.reset();
}

rpl::producer<QString> Step::nextButtonText() const {
	return tr::lng_intro_next();
}

void Step::goBack() {
	if (_goCallback) {
		_goCallback(nullptr, StackAction::Back, Animate::Back);
	}
}

void Step::goNext(Step *step) {
	if (_goCallback) {
		_goCallback(step, StackAction::Forward, Animate::Forward);
	}
}

void Step::goReplace(Step *step, Animate animate) {
	if (_goCallback) {
		_goCallback(step, StackAction::Replace, animate);
	}
}

void Step::finish(const MTPUser &user, QImage &&photo) {
	if (user.type() != mtpc_user
		|| !user.c_user().is_self()
		|| !user.c_user().vid().v) {
		// No idea what to do here.
		// We could've reset intro and MTP, but this really should not happen.
		Ui::show(Box<InformBox>("Internal error: bad user.is_self() after sign in."));
		return;
	}

	// Check if such account is authorized already.
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		const auto raw = existing.get();
		if (const auto session = raw->maybeSession()) {
			if (raw->mtp().environment() == _account->mtp().environment()
				&& UserId(user.c_user().vid()) == session->userId()) {
				_account->logOut();
				crl::on_main(raw, [=] {
					Core::App().domain().activate(raw);
				});
				return;
			}
		}
	}

	api().request(MTPmessages_GetDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilter> &result) {
		createSession(user, photo, result.v);
	}).fail([=](const MTP::Error &error) {
		createSession(user, photo, QVector<MTPDialogFilter>());
	}).send();
}

void Step::createSession(
		const MTPUser &user,
		QImage photo,
		const QVector<MTPDialogFilter> &filters) {
	// Save the default language if we've suggested some other and user ignored it.
	const auto currentId = Lang::Id();
	const auto defaultId = Lang::DefaultLanguageId();
	const auto suggested = Lang::CurrentCloudManager().suggestedLanguage();
	if (currentId.isEmpty() && !suggested.isEmpty() && suggested != defaultId) {
		Lang::GetInstance().switchToId(Lang::DefaultLanguage());
		Local::writeLangPack();
	}

	auto settings = std::make_unique<Main::SessionSettings>();
	settings->setDialogsFiltersEnabled(!filters.isEmpty());

	const auto account = _account;
	account->createSession(user, std::move(settings));

	// "this" is already deleted here by creating the main widget.
	account->local().writeMtpData();
	auto &session = account->session();
	session.data().chatsFilters().setPreloaded(filters);
	if (!filters.isEmpty()) {
		session.saveSettingsDelayed();
	}
	if (!photo.isNull()) {
		session.api().uploadPeerPhoto(session.user(), std::move(photo));
	}
	if (session.supportMode()) {
		PrepareSupportMode(&session);
	}
}

void Step::paintEvent(QPaintEvent *e) {
	Painter p(this);
	paintAnimated(p, e->rect());
}

void Step::resizeEvent(QResizeEvent *e) {
	updateLabelsPosition();
}

void Step::updateLabelsPosition() {
	Ui::SendPendingMoveResizeEvents(_description->entity());
	if (hasCover()) {
		_title->moveToLeft((width() - _title->width()) / 2, contentTop() + st::introCoverTitleTop);
		_description->moveToLeft((width() - _description->width()) / 2, contentTop() + st::introCoverDescriptionTop);
	} else {
		_title->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introTitleTop);
		_description->resizeToWidth(st::introDescription.minWidth);
		_description->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introDescriptionTop);
	}
	if (_error) {
		if (_errorCentered) {
			_error->entity()->resizeToWidth(width());
		}
		Ui::SendPendingMoveResizeEvents(_error->entity());
		auto errorLeft = _errorCentered ? 0 : (contentLeft() + st::buttonRadius);
		_error->moveToLeft(errorLeft, errorTop());
	}
}

int Step::errorTop() const {
	return contentTop() + st::introErrorTop;
}

void Step::setTitleText(rpl::producer<QString> titleText) {
	_titleText = std::move(titleText);
}

void Step::setDescriptionText(
		rpl::producer<QString> descriptionText) {
	setDescriptionText(
		std::move(descriptionText) | Ui::Text::ToWithEntities());
}

void Step::setDescriptionText(
		rpl::producer<TextWithEntities> richDescriptionText) {
	_descriptionText = std::move(richDescriptionText);
}

void Step::showFinished() {
	_a_show.stop();
	_coverAnimation = CoverAnimation();
	_slideAnimation.reset();
	prepareCoverMask();
	activate();
}

bool Step::paintAnimated(Painter &p, QRect clip) {
	if (_slideAnimation) {
		_slideAnimation->paintFrame(p, (width() - st::introStepWidth) / 2, contentTop(), width());
		if (!_slideAnimation->animating()) {
			showFinished();
			return false;
		}
		return true;
	}

	auto dt = _a_show.value(1.);
	if (!_a_show.animating()) {
		if (hasCover()) {
			paintCover(p, 0);
		}
		if (_coverAnimation.title) {
			showFinished();
		}
		if (!QRect(0, contentTop(), width(), st::introStepHeight).intersects(clip)) {
			return true;
		}
		return false;
	}
	if (!_coverAnimation.clipping.isEmpty()) {
		p.setClipRect(_coverAnimation.clipping);
	}

	auto progress = (hasCover() ? anim::easeOutCirc(1., dt) : anim::linear(1., dt));
	auto arrivingAlpha = progress;
	auto departingAlpha = 1. - progress;
	auto showCoverMethod = progress;
	auto hideCoverMethod = progress;
	auto coverTop = (hasCover() ? anim::interpolate(-st::introCoverHeight, 0, showCoverMethod) : anim::interpolate(0, -st::introCoverHeight, hideCoverMethod));

	paintCover(p, coverTop);

	auto positionReady = hasCover() ? showCoverMethod : hideCoverMethod;
	_coverAnimation.title->paintFrame(p, positionReady, departingAlpha, arrivingAlpha);
	_coverAnimation.description->paintFrame(p, positionReady, departingAlpha, arrivingAlpha);

	paintContentSnapshot(p, _coverAnimation.contentSnapshotWas, departingAlpha, showCoverMethod);
	paintContentSnapshot(p, _coverAnimation.contentSnapshotNow, arrivingAlpha, 1. - hideCoverMethod);

	return true;
}

void Step::fillSentCodeData(const MTPDauth_sentCode &data) {
	const auto &type = data.vtype();
	switch (type.type()) {
	case mtpc_auth_sentCodeTypeApp: {
		getData()->codeByTelegram = true;
		getData()->codeLength = type.c_auth_sentCodeTypeApp().vlength().v;
	} break;
	case mtpc_auth_sentCodeTypeSms: {
		getData()->codeByTelegram = false;
		getData()->codeLength = type.c_auth_sentCodeTypeSms().vlength().v;
	} break;
	case mtpc_auth_sentCodeTypeCall: {
		getData()->codeByTelegram = false;
		getData()->codeLength = type.c_auth_sentCodeTypeCall().vlength().v;
	} break;
	case mtpc_auth_sentCodeTypeFlashCall: LOG(("Error: should not be flashcall!")); break;
	}
}

void Step::showDescription() {
	_description->show(anim::type::normal);
}

void Step::hideDescription() {
	_description->hide(anim::type::normal);
}

void Step::paintContentSnapshot(Painter &p, const QPixmap &snapshot, float64 alpha, float64 howMuchHidden) {
	if (!snapshot.isNull()) {
		auto contentTop = anim::interpolate(height() - (snapshot.height() / cIntRetinaFactor()), height(), howMuchHidden);
		if (contentTop < height()) {
			p.setOpacity(alpha);
			p.drawPixmap(QPoint(contentLeft(), contentTop), snapshot, QRect(0, 0, snapshot.width(), (height() - contentTop) * cIntRetinaFactor()));
		}
	}
}

void Step::prepareCoverMask() {
	if (!_coverMask.isNull()) return;

	auto maskWidth = cIntRetinaFactor();
	auto maskHeight = st::introCoverHeight * cIntRetinaFactor();
	auto mask = QImage(maskWidth, maskHeight, QImage::Format_ARGB32_Premultiplied);
	auto maskInts = reinterpret_cast<uint32*>(mask.bits());
	Assert(mask.depth() == (sizeof(uint32) << 3));
	auto maskIntsPerLineAdded = (mask.bytesPerLine() >> 2) - maskWidth;
	Assert(maskIntsPerLineAdded >= 0);
	auto realHeight = static_cast<float64>(maskHeight - 1);
	for (auto y = 0; y != maskHeight; ++y) {
		auto color = anim::color(st::introCoverTopBg, st::introCoverBottomBg, y / realHeight);
		auto colorInt = anim::getPremultiplied(color);
		for (auto x = 0; x != maskWidth; ++x) {
			*maskInts++ = colorInt;
		}
		maskInts += maskIntsPerLineAdded;
	}
	_coverMask = App::pixmapFromImageInPlace(std::move(mask));
}

void Step::paintCover(Painter &p, int top) {
	auto coverHeight = top + st::introCoverHeight;
	if (coverHeight > 0) {
		p.drawPixmap(QRect(0, 0, width(), coverHeight), _coverMask, QRect(0, -top * cIntRetinaFactor(), _coverMask.width(), coverHeight * cIntRetinaFactor()));
	}

	auto left = 0;
	auto right = 0;
	if (width() < st::introCoverMaxWidth) {
		auto iconsMaxSkip = st::introCoverMaxWidth - st::introCoverLeft.width() - st::introCoverRight.width();
		auto iconsSkip = st::introCoverIconsMinSkip + (iconsMaxSkip - st::introCoverIconsMinSkip) * (width() - st::introStepWidth) / (st::introCoverMaxWidth - st::introStepWidth);
		auto outside = iconsSkip + st::introCoverLeft.width() + st::introCoverRight.width() - width();
		left = -outside / 2;
		right = -outside - left;
	}
	if (top < 0) {
		auto shown = float64(coverHeight) / st::introCoverHeight;
		auto leftShown = qRound(shown * (left + st::introCoverLeft.width()));
		left = leftShown - st::introCoverLeft.width();
		auto rightShown = qRound(shown * (right + st::introCoverRight.width()));
		right = rightShown - st::introCoverRight.width();
	}
	st::introCoverLeft.paint(p, left, coverHeight - st::introCoverLeft.height(), width());
	st::introCoverRight.paint(p, width() - right - st::introCoverRight.width(), coverHeight - st::introCoverRight.height(), width());

	auto planeLeft = (width() - st::introCoverIcon.width()) / 2 - st::introCoverIconLeft;
	auto planeTop = top + st::introCoverIconTop;
	if (top < 0 && !_hasCover) {
		auto deltaLeft = -qRound(float64(st::introPlaneWidth / st::introPlaneHeight) * top);
//		auto deltaTop = top;
		planeLeft += deltaLeft;
	//	planeTop += top;
	}
	st::introCoverIcon.paint(p, planeLeft, planeTop, width());
}

int Step::contentLeft() const {
	return (width() - st::introNextButton.width) / 2;
}

int Step::contentTop() const {
	auto result = (height() - st::introHeight) / 2;
	accumulate_max(result, st::introStepTopMin);
	if (_hasCover) {
		const auto currentHeightFull = result + st::introNextTop + st::introContentTopAdd;
		auto added = 1. - std::clamp(
			float64(currentHeightFull - st::windowMinHeight)
				/ (st::introStepHeightFull - st::windowMinHeight),
			0.,
			1.);
		result += qRound(added * st::introContentTopAdd);
	}
	return result;
}

void Step::setErrorCentered(bool centered) {
	_errorCentered = centered;
	_error.destroy();
}

void Step::showError(rpl::producer<QString> text) {
	_errorText = std::move(text);
}

void Step::refreshError(const QString &text) {
	if (text.isEmpty()) {
		if (_error) _error->hide(anim::type::normal);
	} else {
		if (!_error) {
			_error.create(
				this,
				object_ptr<Ui::FlatLabel>(
					this,
					_errorCentered
						? st::introErrorCentered
						: st::introError));
			_error->hide(anim::type::instant);
		}
		_error->entity()->setText(text);
		updateLabelsPosition();
		_error->show(anim::type::normal);
	}
}

void Step::prepareShowAnimated(Step *after) {
	setInnerFocus();
	if (hasCover() || after->hasCover()) {
		_coverAnimation = prepareCoverAnimation(after);
		prepareCoverMask();
	} else {
		auto leftSnapshot = after->prepareSlideAnimation();
		auto rightSnapshot = prepareSlideAnimation();
		_slideAnimation = std::make_unique<Ui::SlideAnimation>();
		_slideAnimation->setSnapshots(std::move(leftSnapshot), std::move(rightSnapshot));
		_slideAnimation->setOverflowHidden(false);
	}
}

Step::CoverAnimation Step::prepareCoverAnimation(Step *after) {
	Ui::SendPendingMoveResizeEvents(this);

	auto result = CoverAnimation();
	result.title = Ui::FlatLabel::CrossFade(
		after->_title,
		_title,
		st::introBg);
	result.description = Ui::FlatLabel::CrossFade(
		after->_description->entity(),
		_description->entity(),
		st::introBg,
		after->_description->pos(),
		_description->pos());
	result.contentSnapshotWas = after->prepareContentSnapshot();
	result.contentSnapshotNow = prepareContentSnapshot();
	return result;
}

QPixmap Step::prepareContentSnapshot() {
	auto otherTop = _description->y() + _description->height();
	auto otherRect = myrtlrect(contentLeft(), otherTop, st::introStepWidth, height() - otherTop);
	return Ui::GrabWidget(this, otherRect);
}

QPixmap Step::prepareSlideAnimation() {
	auto grabLeft = (width() - st::introStepWidth) / 2;
	auto grabTop = contentTop();
	return Ui::GrabWidget(
		this,
		QRect(grabLeft, grabTop, st::introStepWidth, st::introStepHeight));
}

void Step::showAnimated(Animate animate) {
	setFocus();
	show();
	hideChildren();
	if (_slideAnimation) {
		auto slideLeft = (animate == Animate::Back);
		_slideAnimation->start(
			slideLeft,
			[=] { update(0, contentTop(), width(), st::introStepHeight); },
			st::introSlideDuration);
	} else {
		_a_show.start([this] { update(); }, 0., 1., st::introCoverDuration);
	}
}

void Step::setShowAnimationClipping(QRect clipping) {
	_coverAnimation.clipping = clipping;
}

void Step::setGoCallback(
		Fn<void(Step *step, StackAction action, Animate animate)> callback) {
	_goCallback = std::move(callback);
}

void Step::setShowResetCallback(Fn<void()> callback) {
	_showResetCallback = std::move(callback);
}

void Step::setShowTermsCallback(Fn<void()> callback) {
	_showTermsCallback = std::move(callback);
}

void Step::setCancelNearestDcCallback(Fn<void()> callback) {
	_cancelNearestDcCallback = std::move(callback);
}

void Step::setAcceptTermsCallback(
		Fn<void(Fn<void()> callback)> callback) {
	_acceptTermsCallback = std::move(callback);
}

void Step::showFast() {
	show();
	showFinished();
}

bool Step::animating() const {
	return (_slideAnimation && _slideAnimation->animating())
		|| _a_show.animating();
}

bool Step::hasCover() const {
	return _hasCover;
}

bool Step::hasBack() const {
	return false;
}

void Step::activate() {
	_title->show();
	_description->show(anim::type::instant);
	if (!_errorText.current().isEmpty()) {
		_error->show(anim::type::instant);
	}
}

void Step::cancelled() {
}

void Step::finished() {
	hide();
}

} // namespace details
} // namespace Intro
