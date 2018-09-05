/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_cloud_manager.h"
#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "base/binary_guard.h"

namespace Ui {
class RadiobuttonGroup;
class Radiobutton;
} // namespace Ui

class LanguageBox : public BoxContent, private MTP::Sender  {
public:
	LanguageBox(QWidget*) {
	}

	static base::binary_guard Show();

protected:
	void prepare() override;

private:
	using Languages = Lang::CloudManager::Languages;

	void refresh();
	void refreshLanguages();
	void refreshLang();

	Languages _languages;

	class Inner;
	QPointer<Inner> _inner;

};
