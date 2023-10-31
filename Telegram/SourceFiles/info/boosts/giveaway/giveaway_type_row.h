/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/empty_userpic.h"
#include "ui/widgets/buttons.h"

namespace Ui {
template <typename Enum>
class RadioenumGroup;
} // namespace Ui

namespace Giveaway {

class GiveawayTypeRow final : public Ui::RippleButton {
public:
	enum class Type {
		Random,
		SpecificUsers,

		AllMembers,
		OnlyNewMembers,
	};

	GiveawayTypeRow(
		not_null<Ui::RpWidget*> parent,
		Type type,
		rpl::producer<QString> subtitle);

	void addRadio(std::shared_ptr<Ui::RadioenumGroup<Type>> typeGroup);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int) override;

private:
	const Type _type;
	const style::PeerListItem _st;

	Ui::EmptyUserpic _userpic;
	Ui::Text::String _status;
	Ui::Text::String _name;

};

} // namespace Giveaway
