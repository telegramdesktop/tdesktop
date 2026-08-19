/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class VerticalLayout;
class FlatLabel;
class RpWidget;
} // namespace Ui

namespace Info::Profile {

struct Section {
	object_ptr<Ui::RpWidget> widget = { nullptr };
	rpl::producer<bool> shown;
};

class SectionStack final {
public:
	explicit SectionStack(not_null<Ui::VerticalLayout*> layout);

	void add(Section section);
	void addPlainSeparator();
	void addTextSeparator(
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown,
		Fn<void(not_null<Ui::FlatLabel*>)> setup = nullptr);
	void finalize();

	[[nodiscard]] not_null<Ui::VerticalLayout*> layout() const;

private:
	enum class RowType {
		Block,
		PlainSeparator,
		TextSeparator,
	};
	struct Row {
		RowType type = RowType::Block;
		object_ptr<Ui::RpWidget> widget = { nullptr };
		rpl::producer<bool> shown;
		rpl::producer<TextWithEntities> text;
		Fn<void(not_null<Ui::FlatLabel*>)> textSetup;
	};

	[[nodiscard]] static std::vector<bool> ComputeVisibility(
		const std::vector<RowType> &kinds,
		const std::vector<bool> &intrinsic);

	const not_null<Ui::VerticalLayout*> _layout;
	std::vector<Row> _rows;
	bool _finalized = false;

};

} // namespace Info::Profile
