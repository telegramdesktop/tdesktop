/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "boxes/abstract_box.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {

class ManageDictionariesBox : public Ui::BoxContent {
public:
	ManageDictionariesBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	const not_null<Main::Session*> _session;
	Fn<void()> _setInnerFocus;

};

} // namespace Ui

#endif // !TDESKTOP_DISABLE_SPELLCHECK
