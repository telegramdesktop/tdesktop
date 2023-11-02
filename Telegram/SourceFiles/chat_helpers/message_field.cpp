/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/message_field.h"

#include "history/history_widget.h"
#include "history/history.h" // History::session
#include "history/history_item.h" // HistoryItem::originalText
#include "history/history_item_helpers.h" // DropCustomEmoji
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/event_filter.h"
#include "ui/layers/generic_box.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/toast/toast.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "base/qt/qt_common_adapters.h"

#include <QtCore/QMimeData>
#include <QtCore/QStack>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextBlock>
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>

namespace {

using namespace Ui::Text;

using EditLinkAction = Ui::InputField::EditLinkAction;
using EditLinkSelection = Ui::InputField::EditLinkSelection;

constexpr auto kParseLinksTimeout = crl::time(1000);
constexpr auto kTypesDuration = 4 * crl::time(1000);

// For mention / custom emoji tags save and validate selfId,
// ignore tags for different users.
class FieldTagMimeProcessor final {
public:
	FieldTagMimeProcessor(
		not_null<Main::Session*> _session,
		Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji);

	QString operator()(QStringView mimeTag);

private:
	const not_null<Main::Session*> _session;
	const Fn<bool(not_null<DocumentData*>)> _allowPremiumEmoji;

};

FieldTagMimeProcessor::FieldTagMimeProcessor(
	not_null<Main::Session*> session,
	Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji)
: _session(session)
, _allowPremiumEmoji(allowPremiumEmoji) {
}

QString FieldTagMimeProcessor::operator()(QStringView mimeTag) {
	const auto id = _session->userId().bare;
	auto all = TextUtilities::SplitTags(mimeTag);
	auto premiumSkipped = (DocumentData*)nullptr;
	for (auto i = all.begin(); i != all.end();) {
		const auto tag = *i;
		if (TextUtilities::IsMentionLink(tag)
			&& TextUtilities::MentionNameDataToFields(tag).selfId != id) {
			i = all.erase(i);
			continue;
		} else if (Ui::InputField::IsCustomEmojiLink(tag)) {
			const auto data = Ui::InputField::CustomEmojiEntityData(tag);
			const auto emoji = Data::ParseCustomEmojiData(data);
			if (!emoji) {
				i = all.erase(i);
				continue;
			} else if (!_session->premium()) {
				const auto document = _session->data().document(emoji);
				if (document->isPremiumEmoji()) {
					if (!_allowPremiumEmoji
						|| premiumSkipped
						|| !_session->premiumPossible()
						|| !_allowPremiumEmoji(document)) {
						premiumSkipped = document;
						i = all.erase(i);
						continue;
					}
				}
			}
		}
		++i;
	}
	return TextUtilities::JoinTag(all);
}

//bool ValidateUrl(const QString &value) {
//	const auto match = qthelp::RegExpDomain().match(value);
//	if (!match.hasMatch() || match.capturedStart() != 0) {
//		return false;
//	}
//	const auto protocolMatch = RegExpProtocol().match(value);
//	return protocolMatch.hasMatch()
//		&& IsGoodProtocol(protocolMatch.captured(1));
//}

void EditLinkBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		const QString &startText,
		const QString &startLink,
		Fn<void(QString, QString)> callback,
		const style::InputField *fieldStyle) {
	Expects(callback != nullptr);

	const auto &fieldSt = fieldStyle ? *fieldStyle : st::defaultInputField;
	const auto content = box->verticalLayout();

	const auto text = content->add(
		object_ptr<Ui::InputField>(
			content,
			fieldSt,
			tr::lng_formatting_link_text(),
			startText),
		st::markdownLinkFieldPadding);
	text->setInstantReplaces(Ui::InstantReplaces::Default());
	text->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		box->getDelegate()->outerContainer(),
		text,
		&show->session());
	InitSpellchecker(show, text, fieldStyle != nullptr);

	const auto placeholder = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::markdownLinkFieldPadding);
	placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto url = Ui::AttachParentChild(
		content,
		object_ptr<Ui::InputField>(
			content,
			fieldSt,
			tr::lng_formatting_link_url(),
			startLink.trimmed()));
	url->heightValue(
	) | rpl::start_with_next([placeholder](int height) {
		placeholder->resize(placeholder->width(), height);
	}, placeholder->lifetime());
	placeholder->widthValue(
	) | rpl::start_with_next([=](int width) {
		url->resize(width, url->height());
	}, placeholder->lifetime());
	url->move(placeholder->pos());

	const auto submit = [=] {
		const auto linkText = text->getLastText();
		const auto linkUrl = qthelp::validate_url(url->getLastText());
		if (linkText.isEmpty()) {
			text->showError();
			return;
		} else if (linkUrl.isEmpty()) {
			url->showError();
			return;
		}
		const auto weak = Ui::MakeWeak(box);
		callback(linkText, linkUrl);
		if (weak) {
			box->closeBox();
		}
	};

	text->submits(
	) | rpl::start_with_next([=] {
		url->setFocusFast();
	}, text->lifetime());
	url->submits(
	) | rpl::start_with_next([=] {
		if (text->getLastText().isEmpty()) {
			text->setFocusFast();
		} else {
			submit();
		}
	}, url->lifetime());

	box->setTitle(url->getLastText().isEmpty()
		? tr::lng_formatting_link_create_title()
		: tr::lng_formatting_link_edit_title());

	box->addButton(tr::lng_formatting_link_create(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	content->resizeToWidth(st::boxWidth);
	content->moveToLeft(0, 0);
	box->setWidth(st::boxWidth);

	box->setFocusCallback([=] {
		if (startText.isEmpty()) {
			text->setFocusFast();
		} else {
			url->setFocusFast();
		}
	});

	url->customTab(true);
	text->customTab(true);

	url->tabbed(
	) | rpl::start_with_next([=] {
		text->setFocus();
	}, url->lifetime());
	text->tabbed(
	) | rpl::start_with_next([=] {
		url->setFocus();
	}, text->lifetime());
}

TextWithEntities StripSupportHashtag(TextWithEntities text) {
	static const auto expression = QRegularExpression(
		u"\\n?#tsf[a-z0-9_-]*[\\s#a-z0-9_-]*$"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto match = expression.match(text.text);
	if (!match.hasMatch()) {
		return text;
	}
	text.text.chop(match.capturedLength());
	const auto length = text.text.size();
	if (!length) {
		return TextWithEntities();
	}
	for (auto i = text.entities.begin(); i != text.entities.end();) {
		auto &entity = *i;
		if (entity.offset() >= length) {
			i = text.entities.erase(i);
			continue;
		} else if (entity.offset() + entity.length() > length) {
			entity.shrinkFromRight(length - entity.offset());
		}
		++i;
	}
	return text;
}

} // namespace

QString PrepareMentionTag(not_null<UserData*> user) {
	return TextUtilities::kMentionTagStart
		+ QString::number(user->id.value)
		+ '.'
		+ QString::number(user->accessHash())
		+ ':'
		+ QString::number(user->session().userId().bare);
}

TextWithTags PrepareEditText(not_null<HistoryItem*> item) {
	auto original = item->history()->session().supportMode()
		? StripSupportHashtag(item->originalText())
		: item->originalText();
	const auto dropCustomEmoji = !item->history()->session().premium()
		&& !item->history()->peer->isSelf();
	if (dropCustomEmoji) {
		original = DropCustomEmoji(std::move(original));
	}
	return TextWithTags{
		original.text,
		TextUtilities::ConvertEntitiesToTextTags(original.entities)
	};
}

bool EditTextChanged(
		not_null<HistoryItem*> item,
		const TextWithTags &updated) {
	const auto original = PrepareEditText(item);

	// Tags can be different for the same entities, because for
	// animated emoji each tag contains a different random number.
	// So we compare entities instead of tags.
	return (original.text != updated.text)
		|| (TextUtilities::ConvertTextTagsToEntities(original.tags)
			!= TextUtilities::ConvertTextTagsToEntities(updated.tags));
}

Fn<bool(
	Ui::InputField::EditLinkSelection selection,
	QString text,
	QString link,
	EditLinkAction action)> DefaultEditLinkCallback(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::InputField*> field,
		const style::InputField *fieldStyle) {
	const auto weak = Ui::MakeWeak(field);
	return [=](
			EditLinkSelection selection,
			QString text,
			QString link,
			EditLinkAction action) {
		if (action == EditLinkAction::Check) {
			return Ui::InputField::IsValidMarkdownLink(link)
				&& !TextUtilities::IsMentionLink(link);
		}
		auto callback = [=](const QString &text, const QString &link) {
			if (const auto strong = weak.data()) {
				strong->commitMarkdownLinkEdit(selection, text, link);
			}
		};
		show->showBox(Box(
			EditLinkBox,
			show,
			text,
			link,
			std::move(callback),
			fieldStyle));
		return true;
	};
}

void InitMessageFieldHandlers(
		not_null<Main::Session*> session,
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::InputField*> field,
		Fn<bool()> customEmojiPaused,
		Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji,
		const style::InputField *fieldStyle) {
	field->setTagMimeProcessor(
		FieldTagMimeProcessor(session, allowPremiumEmoji));
	const auto paused = [customEmojiPaused] {
		return On(PowerSaving::kEmojiChat) || customEmojiPaused();
	};
	field->setCustomEmojiFactory(
		session->data().customEmojiManager().factory(),
		std::move(customEmojiPaused));
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	field->setMarkdownReplacesEnabled(rpl::single(true));
	if (show) {
		field->setEditLinkCallback(
			DefaultEditLinkCallback(show, field, fieldStyle));
		InitSpellchecker(show, field, fieldStyle != nullptr);
	}
}

void InitMessageFieldHandlers(
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> field,
		ChatHelpers::PauseReason pauseReasonLevel,
		Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji) {
	InitMessageFieldHandlers(
		&controller->session(),
		controller->uiShow(),
		field,
		[=] { return controller->isGifPausedAtLeastFor(pauseReasonLevel); },
		allowPremiumEmoji);
}

void InitMessageFieldGeometry(not_null<Ui::InputField*> field) {
	field->setMinHeight(
		st::historySendSize.height() - 2 * st::historySendPadding);
	field->setMaxHeight(st::historyComposeFieldMaxHeight);

	field->document()->setDocumentMargin(4.);
	field->setAdditionalMargin(style::ConvertScale(4) - 4);
}

void InitMessageField(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::InputField*> field,
		Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji) {
	InitMessageFieldHandlers(
		&show->session(),
		show,
		field,
		[=] { return show->paused(ChatHelpers::PauseReason::Any); },
		std::move(allowPremiumEmoji));
	InitMessageFieldGeometry(field);
	field->customTab(true);
}

void InitMessageField(
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> field,
		Fn<bool(not_null<DocumentData*>)> allowPremiumEmoji) {
	return InitMessageField(
		controller->uiShow(),
		field,
		std::move(allowPremiumEmoji));
}

void InitSpellchecker(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::InputField*> field,
		bool skipDictionariesManager) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	using namespace Spellchecker;
	const auto session = &show->session();
	const auto menuItem = skipDictionariesManager
		? std::nullopt
		: std::make_optional(SpellingHighlighter::CustomContextMenuItem{
			tr::lng_settings_manage_dictionaries(tr::now),
			[=] { show->showBox(Box<Ui::ManageDictionariesBox>(session)); }
		});
	const auto s = Ui::CreateChild<SpellingHighlighter>(
		field.get(),
		Core::App().settings().spellcheckerEnabledValue(),
		menuItem);
	field->setExtendedContextMenu(s->contextMenuCreated());
#endif // TDESKTOP_DISABLE_SPELLCHECK
}

bool HasSendText(not_null<const Ui::InputField*> field) {
	const auto &text = field->getTextWithTags().text;
	for (const auto &ch : text) {
		const auto code = ch.unicode();
		if (code != ' '
			&& code != '\n'
			&& code != '\r'
			&& !IsReplacedBySpace(code)) {
			return true;
		}
	}
	return false;
}

InlineBotQuery ParseInlineBotQuery(
		not_null<Main::Session*> session,
		not_null<const Ui::InputField*> field) {
	auto result = InlineBotQuery();

	const auto &full = field->getTextWithTags();
	const auto &text = full.text;
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
			validInlineUsername = text.endsWith(u"bot"_q);
		} else if (inlineUsernameEnd < textLength && inlineUsernameLength) {
			validInlineUsername = text[inlineUsernameEnd].isSpace();
		}
		if (validInlineUsername) {
			if (!full.tags.isEmpty()
				&& (full.tags.front().offset
					< inlineUsernameStart + inlineUsernameLength)) {
				return InlineBotQuery();
			}
			auto username = base::StringViewMid(text, inlineUsernameStart, inlineUsernameLength);
			if (username != result.username) {
				result.username = username.toString();
				if (const auto peer = session->data().peerByUsername(result.username)) {
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
			if (result.bot
				&& (!result.bot->isBot()
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
		not_null<const Ui::InputField*> field,
		ChatHelpers::ComposeFeatures features) {
	auto result = AutocompleteQuery();

	const auto cursor = field->textCursor();
	if (cursor.hasSelection()) {
		return result;
	}

	const auto position = cursor.position();
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
				if (!features.autocompleteMentions) {
					return {};
				}
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
				if (!features.autocompleteHashtags) {
					return {};
				}
				if (i < 2 || !(text[i - 2].isLetterOrNumber() || text[i - 2] == '_')) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				}
				return result;
			} else if (text[i - 1] == '/') {
				if (!features.autocompleteCommands) {
					return {};
				}
				if (i < 2 && !fragmentPosition) {
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

MessageLinksParser::MessageLinksParser(not_null<Ui::InputField*> field)
: _field(field)
, _timer([=] { parse(); }) {
	_lifetime = _field->changes(
	) | rpl::start_with_next([=] {
		const auto length = _field->getTextWithTags().text.size();
		if (!length) {
			_lastLength = 0;
			_timer.cancel();
			parse();
			return;
		}
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

void MessageLinksParser::parseNow() {
	_timer.cancel();
	parse();
}

void MessageLinksParser::setDisabled(bool disabled) {
	_disabled = disabled;
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

void MessageLinksParser::parse() {
	const auto &textWithTags = _field->getTextWithTags();
	const auto &text = textWithTags.text;
	const auto &tags = textWithTags.tags;
	const auto &markdownTags = _field->getMarkdownTags();
	if (_disabled || text.isEmpty()) {
		_ranges = {};
		_list = QStringList();
		return;
	}
	const auto tagCanIntersectWithLink = [](const QString &tag) {
		return (tag == Ui::InputField::kTagBold)
			|| (tag == Ui::InputField::kTagItalic)
			|| (tag == Ui::InputField::kTagUnderline)
			|| (tag == Ui::InputField::kTagStrikeOut)
			|| (tag == Ui::InputField::kTagSpoiler);
	};

	_ranges.clear();

	auto tag = tags.begin();
	const auto tagsEnd = tags.end();
	const auto processTag = [&] {
		Expects(tag != tagsEnd);

		if (Ui::InputField::IsValidMarkdownLink(tag->id)
			&& !TextUtilities::IsMentionLink(tag->id)) {
			_ranges.push_back({ tag->offset, tag->length, tag->id });
		}
		++tag;
	};
	const auto processTagsBefore = [&](int offset) {
		while (tag != tagsEnd
			&& (tag->offset + tag->length <= offset
				|| tagCanIntersectWithLink(tag->id))) {
			processTag();
		}
	};
	const auto hasTagsIntersection = [&](int till) {
		if (tag == tagsEnd || tag->offset >= till) {
			return false;
		}
		while (tag != tagsEnd && tag->offset < till) {
			processTag();
		}
		return true;
	};

	auto markdownTag = markdownTags.begin();
	const auto markdownTagsEnd = markdownTags.end();
	const auto markdownTagsAllow = [&](int from, int length) {
		while (markdownTag != markdownTagsEnd
			&& (markdownTag->adjustedStart
				+ markdownTag->adjustedLength <= from
				|| !markdownTag->closed
				|| tagCanIntersectWithLink(markdownTag->tag))) {
			++markdownTag;
		}
		if (markdownTag == markdownTagsEnd
			|| markdownTag->adjustedStart >= from + length) {
			return true;
		}
		// Ignore http-links that are completely inside some tags.
		// This will allow sending http://test.com/__test__/test correctly.
		return (markdownTag->adjustedStart > from)
			|| (markdownTag->adjustedStart
				+ markdownTag->adjustedLength < from + length);
	};

	const auto len = text.size();
	const QChar *start = text.unicode(), *end = start + text.size();
	for (auto offset = 0, matchOffset = offset; offset < len;) {
		auto m = qthelp::RegExpDomain().match(text, matchOffset);
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
			if (IsLinkEnd(ch)) {
				break; // link finished
			} else if (IsAlmostLinkEnd(ch)) {
				const QChar *endTest = p + 1;
				while (endTest < end && IsAlmostLinkEnd(*endTest)) {
					++endTest;
				}
				if (endTest >= end || IsLinkEnd(*endTest)) {
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
		const auto range = MessageLinkRange{
			int(domainOffset),
			static_cast<int>(p - start - domainOffset),
			QString()
		};
		processTagsBefore(domainOffset);
		if (!hasTagsIntersection(range.start + range.length)) {
			if (markdownTagsAllow(range.start, range.length)) {
				_ranges.push_back(range);
			}
		}
		offset = matchOffset = p - start;
	}
	processTagsBefore(Ui::kQFixedMax);

	applyRanges(text);
}

void MessageLinksParser::applyRanges(const QString &text) {
	const auto count = int(_ranges.size());
	const auto current = _list.current();
	const auto computeLink = [&](const MessageLinkRange &range) {
		return range.custom.isEmpty()
			? base::StringViewMid(text, range.start, range.length)
			: QStringView(range.custom);
	};
	const auto changed = [&] {
		if (current.size() != count) {
			return true;
		}
		for (auto i = 0; i != count; ++i) {
			if (computeLink(_ranges[i]) != current[i]) {
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
	for (const auto &range : _ranges) {
		parsed.push_back(computeLink(range).toString());
	}
	_list = std::move(parsed);
}

base::unique_qptr<Ui::RpWidget> CreateDisabledFieldView(
		QWidget *parent,
		not_null<PeerData*> peer) {
	auto result = base::make_unique_q<Ui::AbstractButton>(parent);
	const auto raw = result.get();
	const auto label = CreateChild<Ui::FlatLabel>(
		result.get(),
		tr::lng_send_text_no(),
		st::historySendDisabled);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->setPointerCursor(false);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto &st = st::historyComposeField;
		const auto margins = (st.textMargins + st.placeholderMargins);
		const auto available = width - margins.left() - margins.right();
		const auto skip = st::historySendDisabledIconSkip;
		label->resizeToWidth(available - skip);
		label->moveToLeft(margins.left() + skip, margins.top(), width);
	}, label->lifetime());
	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto &st = st::historyComposeField;
		const auto margins = (st.textMargins + st.placeholderMargins);
		const auto &icon = st::historySendDisabledIcon;
		icon.paint(
			p,
			margins.left() + st::historySendDisabledPosition.x(),
			margins.top() + st::historySendDisabledPosition.y(),
			raw->width());
	}, raw->lifetime());
	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = raw->lifetime().make_state<WeakToast>();
	raw->setClickedCallback([=] {
		if (toast->get()) {
			return;
		}
		using Flag = ChatRestriction;
		const auto map = base::flat_map<Flag, tr::phrase<>>{
			{ Flag::SendPhotos, tr::lng_send_text_type_photos },
			{ Flag::SendVideos, tr::lng_send_text_type_videos },
			{
				Flag::SendVideoMessages,
				tr::lng_send_text_type_video_messages,
			},
			{ Flag::SendMusic, tr::lng_send_text_type_music },
			{
				Flag::SendVoiceMessages,
				tr::lng_send_text_type_voice_messages,
			},
			{ Flag::SendFiles, tr::lng_send_text_type_files },
			{ Flag::SendStickers, tr::lng_send_text_type_stickers },
			{ Flag::SendPolls, tr::lng_send_text_type_polls },
		};
		auto list = QStringList();
		for (const auto &[flag, phrase] : map) {
			if (Data::CanSend(peer, flag, false)) {
				list.append(phrase(tr::now));
			}
		}
		if (list.empty()) {
			return;
		}
		const auto types = (list.size() > 1)
			? tr::lng_send_text_type_and_last(
				tr::now,
				lt_types,
				list.mid(0, list.size() - 1).join(", "),
				lt_last,
				list.back())
			: list.back();
		*toast = Ui::Toast::Show(parent, {
			.text = { tr::lng_send_text_no_about(tr::now, lt_types, types) },
			.st = &st::defaultMultilineToast,
			.duration = kTypesDuration,
			.multiline = true,
			.slideSide = RectPart::Bottom,
		});
	});
	return result;
}
