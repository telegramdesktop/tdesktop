/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_cloud_manager.h"
#include "boxes/abstract_box.h"
#include "base/binary_guard.h"

namespace Ui {
class MultiSelect;
struct ScrollToRequest;
} // namespace Ui

class LanguageBox : public BoxContent {
public:
	LanguageBox(QWidget*) {
	}

	void setInnerFocus() override;

	static base::binary_guard Show();

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;

private:
	using Languages = Lang::CloudManager::Languages;

	not_null<Ui::MultiSelect*> createMultiSelect();
	int rowsInPage() const;

	Fn<void()> _setInnerFocus;
	Fn<Ui::ScrollToRequest(int rows)> _jump;

};
