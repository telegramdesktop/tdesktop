/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Editor {

enum class Undo {
	Undo,
	Redo,
};

class UndoController final {
public:
	struct EnableRequest {
		Undo command = Undo::Undo;
		bool enable = true;
	};

	UndoController();

	void setCanPerformChanges(rpl::producer<EnableRequest> &&command);
	void setPerformRequestChanges(rpl::producer<Undo> &&command);

	[[nodiscard]] rpl::producer<EnableRequest> canPerformChanges() const;
	[[nodiscard]] rpl::producer<Undo> performRequestChanges() const;

private:

	rpl::event_stream<Undo> _perform;
	rpl::event_stream<EnableRequest> _enable;

	rpl::lifetime _lifetime;

};

} // namespace Editor
