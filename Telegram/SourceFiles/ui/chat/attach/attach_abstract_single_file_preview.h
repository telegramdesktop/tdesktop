/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_abstract_single_preview.h"
#include "ui/chat/attach/attach_controls.h"
#include "ui/effects/round_checkbox.h"
#include "base/object_ptr.h"

namespace style {
struct ComposeControls;
} // namespace style

namespace Ui {

class IconButton;

class AbstractSingleFilePreview : public AbstractSinglePreview {
public:
	AbstractSingleFilePreview(
		QWidget *parent,
		const style::ComposeControls &st,
		AttachControls::Type type,
		const Text::MarkedContext &captionContext);
	~AbstractSingleFilePreview();

	[[nodiscard]] rpl::producer<> deleteRequests() const override;
	[[nodiscard]] rpl::producer<> editRequests() const override;
	[[nodiscard]] rpl::producer<> renameRequests() const;
	[[nodiscard]] rpl::producer<> selectRequests() const;
	[[nodiscard]] rpl::producer<> modifyRequests() const override;
	void setRenameEnabled(bool enabled);
	void setSelectionMode(bool enabled);
	void setSelected(
		bool selected,
		anim::type animated = anim::type::normal);
	[[nodiscard]] bool selected() const;
	virtual void setDisplayName(const QString &displayName);
	virtual void setCaption(const TextWithTags &caption);

protected:
	struct Data {
		QPixmap fileThumb;
		QString name;
		QString statusText;
		Text::String caption;
		int nameWidth = 0;
		int statusWidth = 0;
		int captionAvailableWidth = 0;
		bool fileIsAudio = false;
		bool fileIsImage = false;
	};

	void prepareThumbFor(Data &data, const QImage &preview);
	bool isThumbedLayout(const Data &data) const;
	[[nodiscard]] const Text::MarkedContext &captionContext() const {
		return _captionContext;
	}

	void setData(Data data);
	void setSelectable(bool selectable);

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void updateTextWidthFor(Data &data);
	void updateDataGeometry();
	[[nodiscard]] QRect captionRect() const;
	[[nodiscard]] QRect nameRect() const;
	[[nodiscard]] bool isOverName(QPoint point) const;
	[[nodiscard]] bool selectingByClick(
		Qt::KeyboardModifiers modifiers) const;
	void applyCursor(style::cursor cursor);

	const style::ComposeControls &_st;
	const AttachControls::Type _type;
	Text::MarkedContext _captionContext;

	Data _data;

	object_ptr<IconButton> _editMedia = { nullptr };
	object_ptr<IconButton> _deleteMedia = { nullptr };
	rpl::event_stream<> _renameRequests;
	rpl::event_stream<> _selectRequests;
	RoundCheckbox _selectCheck;

	style::cursor _cursor = style::cur_default;
	bool _namePressed = false;
	bool _renameEnabled = false;
	bool _selectable = false;
	bool _selectionMode = false;
	bool _selectPressed = false;

};

} // namespace Ui
