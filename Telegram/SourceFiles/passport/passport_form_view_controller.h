/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Passport {

struct Value;

class ViewController {
public:
	virtual void showAskPassword() = 0;
	virtual void showNoPassword() = 0;
	virtual void showPasswordUnconfirmed() = 0;
	virtual void editValue(int index) = 0;

	virtual ~ViewController() {
	}

};

} // namespace Passport
