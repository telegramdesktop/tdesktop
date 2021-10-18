/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/sent_code_field.h"
#include "mtproto/sender.h"

namespace Ui {
class InputField;
class FlatLabel;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

void ShowPhoneBannedError(const QString &phone);

class ConfirmPhoneBox final : public Ui::BoxContent {
public:
	static void Start(
		not_null<Main::Session*> session,
		const QString &phone,
		const QString &hash);

	[[nodiscard]] Main::Session &session() const {
		return *_session;
	}

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	ConfirmPhoneBox(
		QWidget*,
		not_null<Main::Session*> session,
		const QString &phone,
		const QString &hash);
	friend class object_ptr<ConfirmPhoneBox>;

	void sendCode();
	void sendCall();
	void checkPhoneAndHash();

	void sendCodeDone(const MTPauth_SentCode &result);
	void sendCodeFail(const MTP::Error &error);

	void callDone(const MTPauth_SentCode &result);

	void confirmDone(const MTPBool &result);
	void confirmFail(const MTP::Error &error);

	QString getPhone() const {
		return _phone;
	}
	void launch();

	void showError(const QString &error);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _sendCodeRequestId = 0;

	// _hash from the link for account.sendConfirmPhoneCode call.
	// _phoneHash from auth.sentCode for account.confirmPhone call.
	QString _phone, _hash;
	QString _phoneHash;

	// If we receive the code length, we autosubmit _code field when enough symbols is typed.
	int _sentCodeLength = 0;

	mtpRequestId _checkCodeRequestId = 0;

	object_ptr<Ui::FlatLabel> _about = { nullptr };
	object_ptr<Ui::SentCodeField> _code = { nullptr };

	QString _error;
	Ui::SentCodeCall _call;

};
