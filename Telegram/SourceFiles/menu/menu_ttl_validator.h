/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "menu/menu_ttl.h"

class PeerData;

namespace Ui {
class Show;
} // namespace Ui

namespace TTLMenu {

class TTLValidator final {
public:
	TTLValidator(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer);

	void showBox() const;
	[[nodiscard]] bool can() const;
	[[nodiscard]] Args createArgs() const;
	void showToast() const;
	const style::icon *icon() const;

private:
	const not_null<PeerData*> _peer;
	const std::shared_ptr<Ui::Show> _show;

};

} // namespace TTLMenu
