/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/input_fields.h"
#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

QString ConvertTagToMimeTag(const QString &tagId);
QString PrepareMentionTag(not_null<UserData*> user);

EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags);
TextWithTags::Tags ConvertEntitiesToTextTags(
	const EntitiesInText &entities);
std::unique_ptr<QMimeData> MimeDataFromText(const TextForMimeData &text);
void SetClipboardText(
	const TextForMimeData &text,
	QClipboard::Mode mode = QClipboard::Clipboard);
TextWithTags PrepareEditText(not_null<HistoryItem*> item);

Fn<bool(
	Ui::InputField::EditLinkSelection selection,
	QString text,
	QString link,
	Ui::InputField::EditLinkAction action)> DefaultEditLinkCallback(
		not_null<Main::Session*> session,
		not_null<Ui::InputField*> field);
void InitMessageField(
	not_null<Window::SessionController*> controller,
	not_null<Ui::InputField*> field);
bool HasSendText(not_null<const Ui::InputField*> field);

struct InlineBotQuery {
	QString query;
	QString username;
	UserData *bot = nullptr;
	bool lookingUpBot = false;
};
InlineBotQuery ParseInlineBotQuery(not_null<const Ui::InputField*> field);

struct AutocompleteQuery {
	QString query;
	bool fromStart = false;
};
AutocompleteQuery ParseMentionHashtagBotCommandQuery(
	not_null<const Ui::InputField*> field);

class QtConnectionOwner final {
public:
	QtConnectionOwner(QMetaObject::Connection connection = {});
	QtConnectionOwner(QtConnectionOwner &&other);
	QtConnectionOwner &operator=(QtConnectionOwner &&other);
	~QtConnectionOwner();

private:
	void disconnect();

	QMetaObject::Connection _data;

};

class MessageLinksParser : private QObject {
public:
	MessageLinksParser(not_null<Ui::InputField*> field);

	const rpl::variable<QStringList> &list() const;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	struct LinkRange {
		int start;
		int length;
		QString custom;
	};
	friend inline bool operator==(const LinkRange &a, const LinkRange &b) {
		return (a.start == b.start)
			&& (a.length == b.length)
			&& (a.custom == b.custom);
	}
	friend inline bool operator!=(const LinkRange &a, const LinkRange &b) {
		return !(a == b);
	}

	void parse();
	void apply(const QString &text, const QVector<LinkRange> &ranges);

	not_null<Ui::InputField*> _field;
	rpl::variable<QStringList> _list;
	int _lastLength = 0;
	base::Timer _timer;
	QtConnectionOwner _connection;

};

enum class SendMenuType {
	Disabled,
	SilentOnly,
	Scheduled,
	Reminder,
};

void SetupSendMenu(
	not_null<Ui::RpWidget*> button,
	Fn<SendMenuType()> type,
	Fn<void()> silent,
	Fn<void()> schedule);
