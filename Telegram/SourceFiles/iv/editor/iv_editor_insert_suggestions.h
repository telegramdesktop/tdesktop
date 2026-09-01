/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "iv/editor/iv_editor_page_blocks.h"
#include "ui/rp_widget.h"
#include "ui/round_rect.h"
#include "ui/widgets/shadow.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

class QKeyEvent;
class QPainter;

namespace style {
struct Menu;
} // namespace style

namespace Ui {
class InputField;
class ScrollArea;
} // namespace Ui

namespace Iv::Editor {

enum class InsertSuggestionCommand : uchar {
	Heading1,
	Heading2,
	Heading3,
	Heading4,
	Heading5,
	Heading6,
	Blockquote,
	Pullquote,
	Code,
	Footer,
	BulletList,
	OrderedList,
	TaskList,
	Details,
	Button,
	Table,
	Math,
	Divider,
	Media,
	Audio,
	Map,
};

[[nodiscard]] std::optional<InsertAction> InsertSuggestionBlock(
	InsertSuggestionCommand command);

class InsertSuggestions final : public Ui::RpWidget {
public:
	InsertSuggestions(
		QWidget *parent,
		rpl::producer<bool> premium,
		bool media,
		bool map);
	~InsertSuggestions();

	[[nodiscard]] bool applyQuery(const QString &query);
	void moveNear(QRect anchor, QRect available);
	[[nodiscard]] bool handleKeyPress(not_null<QKeyEvent*> e);
	[[nodiscard]] rpl::producer<InsertSuggestionCommand> chosen() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;

	[[nodiscard]] int chromeHeight() const;
	void resizeToRows(int rows);

	const style::Menu &_st;
	Ui::BoxShadow _shadow;
	Ui::RoundRect _roundRect;
	QMargins _shadowMargins;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Inner> _inner;

};

struct InsertSuggestionsDescriptor {
	not_null<Ui::RpWidget*> host;
	not_null<QWidget*> outer;
	Fn<Ui::InputField*()> field;
	rpl::producer<bool> premium;
	Fn<void(InsertSuggestionCommand)> chosen;
	bool media = false;
	bool map = false;
};

class InsertSuggestionsController final {
public:
	explicit InsertSuggestionsController(
		InsertSuggestionsDescriptor descriptor);
	~InsertSuggestionsController();

	[[nodiscard]] bool active() const;
	void open();
	void scheduleRefresh();
	void refresh();
	void close();
	void updatePosition();
	[[nodiscard]] bool handleKeyPress(not_null<QKeyEvent*> e);
	void takeQuery();
	void paintQuery(QPainter &p);

private:
	void ensurePanel();
	[[nodiscard]] QRect queryRect() const;
	void refreshQueryRect();

	const not_null<Ui::RpWidget*> _host;
	const not_null<QWidget*> _outer;
	const Fn<Ui::InputField*()> _field;
	const Fn<void(InsertSuggestionCommand)> _chosen;
	const bool _media = false;
	const bool _map = false;
	rpl::producer<bool> _premium;
	base::unique_qptr<InsertSuggestions> _panel;
	QRect _query;
	bool _active = false;

};

} // namespace Iv::Editor
