/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/message_field.h"

#include "history/history_widget.h"
#include "base/qthelp_regex.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "styles/style_history.h"

namespace {

constexpr auto kParseLinksTimeout = TimeMs(1000);

// For mention tags save and validate userId, ignore tags for different userId.
class FieldTagMimeProcessor : public Ui::InputField::TagMimeProcessor {
public:
	QString mimeTagFromTag(const QString &tagId) override {
		return ConvertTagToMimeTag(tagId);
	}

	QString tagFromMimeTag(const QString &mimeTag) override {
		if (mimeTag.startsWith(qstr("mention://"))) {
			auto match = QRegularExpression(":(\\d+)$").match(mimeTag);
			if (!match.hasMatch()
				|| match.capturedRef(1).toInt() != Auth().userId()) {
				return QString();
			}
			return mimeTag.mid(0, mimeTag.size() - match.capturedLength());
		}
		return mimeTag;
	}

};

} // namespace

QString ConvertTagToMimeTag(const QString &tagId) {
	if (tagId.startsWith(qstr("mention://"))) {
		return tagId + ':' + QString::number(Auth().userId());
	}
	return tagId;
}

EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags) {
	EntitiesInText result;
	if (tags.isEmpty()) {
		return result;
	}

	result.reserve(tags.size());
	auto mentionStart = qstr("mention://user.");
	for (const auto &tag : tags) {
		const auto push = [&](
				EntityInTextType type,
				const QString &data = QString()) {
			result.push_back(
				EntityInText(type, tag.offset, tag.length, data));
		};
		if (tag.id.startsWith(mentionStart)) {
			if (auto match = qthelp::regex_match("^(\\d+\\.\\d+)(/|$)", tag.id.midRef(mentionStart.size()))) {
				push(EntityInTextMentionName, match->captured(1));
			}
		} else if (tag.id == Ui::InputField::kTagBold) {
			push(EntityInTextBold);
		} else if (tag.id == Ui::InputField::kTagItalic) {
			push(EntityInTextItalic);
		} else if (tag.id == Ui::InputField::kTagCode) {
			push(EntityInTextCode);
		} else if (tag.id == Ui::InputField::kTagPre) {
			push(EntityInTextPre);
		}
	}
	return result;
}

TextWithTags::Tags ConvertEntitiesToTextTags(const EntitiesInText &entities) {
	TextWithTags::Tags result;
	if (entities.isEmpty()) {
		return result;
	}

	result.reserve(entities.size());
	for (const auto &entity : entities) {
		const auto push = [&](const QString &tag) {
			result.push_back({ entity.offset(), entity.length(), tag });
		};
		switch (entity.type()) {
		case EntityInTextMentionName: {
			auto match = QRegularExpression("^(\\d+\\.\\d+)$").match(entity.data());
			if (match.hasMatch()) {
				push(qstr("mention://user.") + entity.data());
			}
		} break;
		case EntityInTextBold: push(Ui::InputField::kTagBold); break;
		case EntityInTextItalic: push(Ui::InputField::kTagItalic); break;
		case EntityInTextCode: push(Ui::InputField::kTagCode); break;
		case EntityInTextPre: push(Ui::InputField::kTagPre); break;
		}
	}
	return result;
}

std::unique_ptr<QMimeData> MimeDataFromTextWithEntities(
		const TextWithEntities &forClipboard) {
	if (forClipboard.text.isEmpty()) {
		return nullptr;
	}

	auto result = std::make_unique<QMimeData>();
	result->setText(forClipboard.text);
	auto tags = ConvertEntitiesToTextTags(forClipboard.entities);
	if (!tags.isEmpty()) {
		for (auto &tag : tags) {
			tag.id = ConvertTagToMimeTag(tag.id);
		}
		result->setData(
			TextUtilities::TagsMimeType(),
			TextUtilities::SerializeTags(tags));
	}
	return result;
}

void SetClipboardWithEntities(
		const TextWithEntities &forClipboard,
		QClipboard::Mode mode) {
	if (auto data = MimeDataFromTextWithEntities(forClipboard)) {
		QApplication::clipboard()->setMimeData(data.release(), mode);
	}
}

void InitMessageField(not_null<Ui::InputField*> field) {
	field->setMinHeight(st::historySendSize.height() - 2 * st::historySendPadding);
	field->setMaxHeight(st::historyComposeFieldMaxHeight);

	field->setTagMimeProcessor(std::make_unique<FieldTagMimeProcessor>());

	field->document()->setDocumentMargin(4.);
	field->setAdditionalMargin(convertScale(4) - 4);

	field->customTab(true);
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(Global::ReplaceEmojiValue());
	field->setMarkdownReplacesEnabled(rpl::single(true));
}

bool HasSendText(not_null<const Ui::InputField*> field) {
	const auto &text = field->getTextWithTags().text;
	for (const auto ch : text) {
		const auto code = ch.unicode();
		if (code != ' '
			&& code != '\n'
			&& code != '\r'
			&& !chReplacedBySpace(code)) {
			return true;
		}
	}
	return false;
}

InlineBotQuery ParseInlineBotQuery(not_null<const Ui::InputField*> field) {
	auto result = InlineBotQuery();

	const auto &text = field->getTextWithTags().text;
	const auto textLength = text.size();

	auto inlineUsernameStart = 1;
	auto inlineUsernameLength = 0;
	if (textLength > 2 && text[0] == '@' && text[1].isLetter()) {
		inlineUsernameLength = 1;
		for (auto i = inlineUsernameStart + 1; i != textLength; ++i) {
			const auto ch = text[i];
			if (ch.isLetterOrNumber() || ch.unicode() == '_') {
				++inlineUsernameLength;
				continue;
			} else if (!ch.isSpace()) {
				inlineUsernameLength = 0;
			}
			break;
		}
		auto inlineUsernameEnd = inlineUsernameStart + inlineUsernameLength;
		auto inlineUsernameEqualsText = (inlineUsernameEnd == textLength);
		auto validInlineUsername = false;
		if (inlineUsernameEqualsText) {
			validInlineUsername = text.endsWith(qstr("bot"));
		} else if (inlineUsernameEnd < textLength && inlineUsernameLength) {
			validInlineUsername = text[inlineUsernameEnd].isSpace();
		}
		if (validInlineUsername) {
			auto username = text.midRef(inlineUsernameStart, inlineUsernameLength);
			if (username != result.username) {
				result.username = username.toString();
				if (const auto peer = App::peerByName(result.username)) {
					if (const auto user = peer->asUser()) {
						result.bot = peer->asUser();
					} else {
						result.bot = nullptr;
					}
					result.lookingUpBot = false;
				} else {
					result.bot = nullptr;
					result.lookingUpBot = true;
				}
			}
			if (result.lookingUpBot) {
				result.query = QString();
				return result;
			} else if (result.bot && (!result.bot->botInfo
				|| result.bot->botInfo->inlinePlaceholder.isEmpty())) {
				result.bot = nullptr;
			} else {
				result.query = inlineUsernameEqualsText
					? QString()
					: text.mid(inlineUsernameEnd + 1);
				return result;
			}
		} else {
			inlineUsernameLength = 0;
		}
	}
	if (inlineUsernameLength < 3) {
		result.bot = nullptr;
		result.username = QString();
	}
	result.query = QString();
	return result;
}

AutocompleteQuery ParseMentionHashtagBotCommandQuery(
		not_null<const Ui::InputField*> field) {
	auto result = AutocompleteQuery();

	const auto cursor = field->textCursor();
	const auto position = cursor.position();
	if (cursor.anchor() != position) {
		return result;
	}

	const auto document = field->document();
	const auto block = document->findBlock(position);
	for (auto item = block.begin(); !item.atEnd(); ++item) {
		const auto fragment = item.fragment();
		if (!fragment.isValid()) {
			continue;
		}

		const auto fragmentPosition = fragment.position();
		const auto fragmentEnd = fragmentPosition + fragment.length();
		if (fragmentPosition >= position || fragmentEnd < position) {
			continue;
		}

		const auto format = fragment.charFormat();
		if (format.isImageFormat()) {
			continue;
		}

		bool mentionInCommand = false;
		const auto text = fragment.text();
		for (auto i = position - fragmentPosition; i != 0; --i) {
			if (text[i - 1] == '@') {
				if ((position - fragmentPosition - i < 1 || text[i].isLetter()) && (i < 2 || !(text[i - 2].isLetterOrNumber() || text[i - 2] == '_'))) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				} else if ((position - fragmentPosition - i < 1 || text[i].isLetter()) && i > 2 && (text[i - 2].isLetterOrNumber() || text[i - 2] == '_') && !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				return result;
			} else if (text[i - 1] == '#') {
				if (i < 2 || !(text[i - 2].isLetterOrNumber() || text[i - 2] == '_')) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				}
				return result;
			} else if (text[i - 1] == '/') {
				if (i < 2) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				}
				return result;
			}
			if (position - fragmentPosition - i > 127 || (!mentionInCommand && (position - fragmentPosition - i > 63))) {
				break;
			}
			if (!text[i - 1].isLetterOrNumber() && text[i - 1] != '_') {
				break;
			}
		}
		break;
	}
	return result;
}

QtConnectionOwner::QtConnectionOwner(QMetaObject::Connection connection)
: _data(connection) {
}

QtConnectionOwner::QtConnectionOwner(QtConnectionOwner &&other)
: _data(base::take(other._data)) {
}

QtConnectionOwner &QtConnectionOwner::operator=(QtConnectionOwner &&other) {
	disconnect();
	_data = base::take(other._data);
	return *this;
}

void QtConnectionOwner::disconnect() {
	QObject::disconnect(base::take(_data));
}

QtConnectionOwner::~QtConnectionOwner() {
	disconnect();
}

MessageLinksParser::MessageLinksParser(not_null<Ui::InputField*> field)
: _field(field)
, _timer([=] { parse(); }) {
	_connection = QObject::connect(_field, &Ui::InputField::changed, [=] {
		const auto length = _field->getTextWithTags().text.size();
		const auto timeout = (std::abs(length - _lastLength) > 2)
			? 0
			: kParseLinksTimeout;
		if (!_timer.isActive() || timeout < _timer.remainingTime()) {
			_timer.callOnce(timeout);
		}
		_lastLength = length;
	});
	_field->installEventFilter(this);
}

bool MessageLinksParser::eventFilter(QObject *object, QEvent *event) {
	if (object == _field) {
		if (event->type() == QEvent::KeyPress) {
			const auto text = static_cast<QKeyEvent*>(event)->text();
			if (!text.isEmpty() && text.size() < 3) {
				const auto ch = text[0];
				if (false
					|| ch == '\n'
					|| ch == '\r'
					|| ch.isSpace()
					|| ch == QChar::LineSeparator) {
					_timer.callOnce(0);
				}
			}
		} else if (event->type() == QEvent::Drop) {
			_timer.callOnce(0);
		}
	}
	return QObject::eventFilter(object, event);
}

const rpl::variable<QStringList> &MessageLinksParser::list() const {
	return _list;
}

void MessageLinksParser::parse() {
	const auto &text = _field->getTextWithTags().text;
	if (text.isEmpty()) {
		_list = QStringList();
		return;
	}

	auto ranges = QVector<LinkRange>();
	const auto len = text.size();
	const QChar *start = text.unicode(), *end = start + text.size();
	for (auto offset = 0, matchOffset = offset; offset < len;) {
		auto m = TextUtilities::RegExpDomain().match(text, matchOffset);
		if (!m.hasMatch()) break;

		auto domainOffset = m.capturedStart();

		auto protocol = m.captured(1).toLower();
		auto topDomain = m.captured(3).toLower();
		auto isProtocolValid = protocol.isEmpty() || TextUtilities::IsValidProtocol(protocol);
		auto isTopDomainValid = !protocol.isEmpty() || TextUtilities::IsValidTopDomain(topDomain);

		if (protocol.isEmpty() && domainOffset > offset + 1 && *(start + domainOffset - 1) == QChar('@')) {
			auto forMailName = text.mid(offset, domainOffset - offset - 1);
			auto mMailName = TextUtilities::RegExpMailNameAtEnd().match(forMailName);
			if (mMailName.hasMatch()) {
				offset = matchOffset = m.capturedEnd();
				continue;
			}
		}
		if (!isProtocolValid || !isTopDomainValid) {
			offset = matchOffset = m.capturedEnd();
			continue;
		}

		QStack<const QChar*> parenth;
		const QChar *domainEnd = start + m.capturedEnd(), *p = domainEnd;
		for (; p < end; ++p) {
			QChar ch(*p);
			if (chIsLinkEnd(ch)) break; // link finished
			if (chIsAlmostLinkEnd(ch)) {
				const QChar *endTest = p + 1;
				while (endTest < end && chIsAlmostLinkEnd(*endTest)) {
					++endTest;
				}
				if (endTest >= end || chIsLinkEnd(*endTest)) {
					break; // link finished at p
				}
				p = endTest;
				ch = *p;
			}
			if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
				parenth.push(p);
			} else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') {
				if (parenth.isEmpty()) break;
				const QChar *q = parenth.pop(), open(*q);
				if ((ch == ')' && open != '(') || (ch == ']' && open != '[') || (ch == '}' && open != '{') || (ch == '>' && open != '<')) {
					p = q;
					break;
				}
			}
		}
		if (p > domainEnd) { // check, that domain ended
			if (domainEnd->unicode() != '/' && domainEnd->unicode() != '?') {
				matchOffset = domainEnd - start;
				continue;
			}
		}
		ranges.push_back({ domainOffset, static_cast<int>(p - start - domainOffset) });
		offset = matchOffset = p - start;
	}

	apply(text, ranges);
}

void MessageLinksParser::apply(
		const QString &text,
		const QVector<LinkRange> &ranges) {
	const auto count = int(ranges.size());
	const auto current = _list.current();
	const auto changed = [&] {
		if (current.size() != count) {
			return true;
		}
		for (auto i = 0; i != count; ++i) {
			const auto &range = ranges[i];
			if (text.midRef(range.start, range.length) != current[i]) {
				return true;
			}
		}
		return false;
	}();
	if (!changed) {
		return;
	}
	auto parsed = QStringList();
	parsed.reserve(count);
	for (const auto &range : ranges) {
		parsed.push_back(text.mid(range.start, range.length));
	}
	_list = std::move(parsed);
}
