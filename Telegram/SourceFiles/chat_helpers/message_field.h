/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/input_fields.h"

class HistoryWidget;
namespace Window {
class Controller;
} // namespace Window

QString ConvertTagToMimeTag(const QString &tagId);

EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags);
TextWithTags::Tags ConvertEntitiesToTextTags(
	const EntitiesInText &entities);
std::unique_ptr<QMimeData> MimeDataFromTextWithEntities(
	const TextWithEntities &forClipboard);
void SetClipboardWithEntities(
	const TextWithEntities &forClipboard,
	QClipboard::Mode mode = QClipboard::Clipboard);

class MessageField final : public Ui::FlatTextarea {
	Q_OBJECT

public:
	MessageField(QWidget *parent, not_null<Window::Controller*> controller, const style::FlatTextarea &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString());

	bool hasSendText() const;

	void setInsertFromMimeDataHook(base::lambda<bool(const QMimeData *data)> hook) {
		_insertFromMimeDataHook = std::move(hook);
	}

public slots:
	void onEmojiInsert(EmojiPtr emoji);

signals:
	void focused();

protected:
	void focusInEvent(QFocusEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	bool canInsertFromMimeData(const QMimeData *source) const override;
	void insertFromMimeData(const QMimeData *source) override;

private:
	not_null<Window::Controller*> _controller;
	base::lambda<bool(const QMimeData *data)> _insertFromMimeDataHook;

};
