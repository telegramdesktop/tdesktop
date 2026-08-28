/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QTextDocument>

namespace Editor {

class EmojiDocument final : public QTextDocument {
public:
	explicit EmojiDocument(QObject *parent = nullptr);
	QVariant loadResource(int type, const QUrl &name) override;

private:
	std::map<QUrl, QVariant> _cache;
};

void ReplaceEmoji(QTextDocument *doc);
void ReplaceEmojiInRange(QTextDocument *doc, int from, int to);
void SanitizeRange(QTextDocument *doc, int from, int to);
[[nodiscard]] QString RecoverTextFromDocument(QTextDocument *doc);
[[nodiscard]] QString RecoverWrappedTextFromDocument(QTextDocument *doc);

} // namespace Editor
