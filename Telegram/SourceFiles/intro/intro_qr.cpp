/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_qr.h"

#include "lang/lang_keys.h"
#include "intro/introphone.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "main/main_account.h"
#include "boxes/confirm_box.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "qr/qr_generate.h"
#include "styles/style_intro.h"

namespace Intro {
namespace {

[[nodiscard]] QImage TelegramLogoImage(int size) {
	return Core::App().logo().scaled(
		size,
		size,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
}

[[nodiscard]] QImage TelegramQrExact(const Qr::Data &data, int pixel) {
	return Qr::ReplaceCenter(
		Qr::Generate(data, pixel),
		TelegramLogoImage(Qr::ReplaceSize(data, pixel)));
}

[[nodiscard]] QImage TelegramQr(const Qr::Data &data, int pixel, int max = 0) {
	Expects(data.size > 0);

	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	return TelegramQrExact(data, pixel * style::DevicePixelRatio());
}

[[nodiscard]] QImage TelegramQr(const QString &text, int pixel, int max) {
	return TelegramQr(Qr::Encode(text), pixel, max);
}

[[nodiscard]] not_null<Ui::RpWidget*> PrepareQrWidget(
		not_null<QWidget*> parent,
		rpl::producer<QImage> images) {
	auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	auto current = result->lifetime().make_state<QImage>();
	std::move(
		images
	) | rpl::start_with_next([=](QImage &&image) {
		result->resize(image.size() / cIntRetinaFactor());
		*current = std::move(image);
		result->update();
	}, result->lifetime());
	result->paintRequest(
	) | rpl::filter([=] {
		return !current->isNull();
	}) | rpl::start_with_next([=](QRect clip) {
		QPainter(result).drawImage(
			QRect(QPoint(), current->size() / cIntRetinaFactor()),
			*current);
	}, result->lifetime());
	return result;
}

} // namespace

QrWidget::QrWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Widget::Data*> data)
: Step(parent, account, data)
, _code(PrepareQrWidget(this, _qrImages.events()))
, _refreshTimer([=] { refreshCode(); }) {
	setTitleText(tr::lng_intro_qr_title());
	setDescriptionText(tr::lng_intro_qr_description());
	setErrorCentered(true);

	account->destroyStaleAuthorizationKeys();
	account->mtpUpdates(
	) | rpl::start_with_next([=](const MTPUpdates &updates) {
		checkForTokenUpdate(updates);
	}, lifetime());

	_code->widthValue(
	) | rpl::start_with_next([=] {
		updateCodeGeometry();
	}, _code->lifetime());
	_code->show();

	refreshCode();
}

void QrWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateCodeGeometry();
}

void QrWidget::checkForTokenUpdate(const MTPUpdates &updates) {
	updates.match([&](const MTPDupdateShort &data) {
		checkForTokenUpdate(data.vupdate());
	}, [&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			checkForTokenUpdate(update);
		}
	}, [&](const MTPDupdatesCombined &data) {
		for (const auto &update : data.vupdates().v) {
			checkForTokenUpdate(update);
		}
	}, [](const auto &) {});
}

void QrWidget::checkForTokenUpdate(const MTPUpdate &update) {
	update.match([&](const MTPDupdateLoginToken &data) {
		if (_requestId) {
			_forceRefresh = true;
		} else {
			_refreshTimer.cancel();
			refreshCode();
		}
	}, [](const auto &) {});
}

void QrWidget::updateCodeGeometry() {
	_code->moveToLeft(
		(width() - _code->width()) / 2,
		contentTop() + st::introQrTop);
}

void QrWidget::submit() {
	goReplace<PhoneWidget>();
}

rpl::producer<QString> QrWidget::nextButtonText() const {
	return tr::lng_intro_qr_skip();
}

void QrWidget::refreshCode() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPauth_ExportLoginToken(
		MTP_int(ApiId),
		MTP_string(ApiHash),
		MTP_vector<MTPint>(0)
	)).done([=](const MTPauth_LoginToken &result) {
		handleTokenResult(result);
	}).fail([=](const RPCError &error) {
		showTokenError(error);
	}).send();
}

void QrWidget::handleTokenResult(const MTPauth_LoginToken &result) {
	result.match([&](const MTPDauth_loginToken &data) {
		_requestId = 0;
		showToken(data.vtoken().v);

		if (base::take(_forceRefresh)) {
			refreshCode();
		} else {
			const auto left = data.vexpires().v - base::unixtime::now();
			_refreshTimer.callOnce(std::max(left, 1) * crl::time(1000));
		}
	}, [&](const MTPDauth_loginTokenMigrateTo &data) {
		importTo(data.vdc_id().v, data.vtoken().v);
	}, [&](const MTPDauth_loginTokenSuccess &data) {
		done(data.vauthorization());
	});
}

void QrWidget::showTokenError(const RPCError &error) {
	_requestId = 0;
	if (base::take(_forceRefresh)) {
		refreshCode();
	} else {
		showError(rpl::single(error.type()));
	}
}

void QrWidget::showToken(const QByteArray &token) {
	const auto encoded = token.toBase64(QByteArray::Base64UrlEncoding);
	const auto text = "tg_login/" + encoded;
	_qrImages.fire(TelegramQr(text, st::introQrPixel, st::introQrMaxSize));
}

void QrWidget::importTo(MTP::DcId dcId, const QByteArray &token) {
	Expects(_requestId != 0);

	_requestId = _api.request(MTPauth_ImportLoginToken(
		MTP_bytes(token)
	)).done([=](const MTPauth_LoginToken &result) {
		handleTokenResult(result);
	}).fail([=](const RPCError &error) {
		showTokenError(error);
	}).toDC(dcId).send();
}

void QrWidget::done(const MTPauth_Authorization &authorization) {
	authorization.match([&](const MTPDauth_authorization &data) {
		if (data.vuser().type() != mtpc_user
			|| !data.vuser().c_user().is_self()) {
			showError(rpl::single(Lang::Hard::ServerError()));
			return;
		}
		const auto phone = data.vuser().c_user().vphone().value_or_empty();
		cSetLoggedPhoneNumber(phone);
		finish(data.vuser());
	}, [&](const MTPDauth_authorizationSignUpRequired &data) {
		_requestId = 0;
		LOG(("API Error: Unexpected auth.authorizationSignUpRequired."));
		showError(rpl::single(Lang::Hard::ServerError()));
	});
}

void QrWidget::activate() {
	Step::activate();
	_code->show();
}

void QrWidget::finished() {
	Step::finished();
	_refreshTimer.cancel();
	rpcInvalidate();
	cancelled();
}

void QrWidget::cancelled() {
	_api.request(base::take(_requestId)).cancel();
}

} // namespace Intro
