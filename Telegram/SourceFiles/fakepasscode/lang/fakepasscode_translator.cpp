#include "fakepasscode_translator.h"

#include "lang_auto.h"

class ValueParser {
public:
    ValueParser(
            ushort keyIndex,
            const QByteArray &value);

    QString takeResult() {
        Expects(!_failed);

        return std::move(_result);
    }

    bool parse();

private:
    void appendToResult(const char *nextBegin);
    bool logError(const QString &text);
    bool readTag();

    ushort _keyIndex = Lang::kKeysCount;

    QLatin1String _currentTag;
    ushort _currentTagIndex = 0;
    QString _currentTagReplacer;

    bool _failed = true;

    const char *_begin = nullptr;
    const char *_ch = nullptr;
    const char *_end = nullptr;

    QString _result;
    OrderedSet<ushort> _tagsUsed;

};

ValueParser::ValueParser(
        ushort keyIndex,
        const QByteArray &value)
        : _keyIndex(keyIndex)
        , _currentTag("")
        , _begin(value.constData())
        , _ch(_begin)
        , _end(_begin + value.size()) {
}

bool ValueParser::logError(const QString &text) {
    _failed = true;
    auto loggedKey = (_currentTag.size() > 0) ? (_currentTag) : QString("");
    DEBUG_LOG(("Lang Error: %1 (key '%2')").arg(text, loggedKey));
    return false;
}

void ValueParser::appendToResult(const char *nextBegin) {
    if (_ch > _begin) _result.append(QString::fromUtf8(_begin, _ch - _begin));
    _begin = nextBegin;
}

bool ValueParser::readTag() {
    using namespace Lang;
    auto tagStart = _ch;
    auto isTagChar = [](QChar ch) {
        if (ch >= 'a' && ch <= 'z') {
            return true;
        } else if (ch >= 'A' && ch <= 'z') {
            return true;
        } else if (ch >= '0' && ch <= '9') {
            return true;
        }
        return (ch == '_');
    };
    while (_ch != _end && isTagChar(*_ch)) {
        ++_ch;
    }
    if (_ch == tagStart) {
        return logError("Expected tag name");
    }

    _currentTag = QLatin1String(tagStart, _ch - tagStart);
    if (_ch == _end || *_ch != '}') {
        return logError("Expected '}' after tag name");
    }

    _currentTagIndex = GetTagIndex(_currentTag);
    if (_currentTagIndex == kTagsCount) {
        return logError("Unknown tag");
    }
    if (!IsTagReplaced(_keyIndex, _currentTagIndex)) {
        return logError("Unexpected tag");
    }
    if (_tagsUsed.contains(_currentTagIndex)) {
        return logError("Repeated tag");
    }
    _tagsUsed.insert(_currentTagIndex);

    if (_currentTagReplacer.isEmpty()) {
        _currentTagReplacer = QString(4, TextCommand);
        _currentTagReplacer[1] = TextCommandLangTag;
    }
    _currentTagReplacer[2] = QChar(0x0020 + _currentTagIndex);

    return true;
}

bool ValueParser::parse() {
    _failed = false;
    _result.reserve(_end - _begin);
    for (; _ch != _end; ++_ch) {
        if (*_ch == '{') {
            appendToResult(_ch);

            ++_ch;
            if (!readTag()) {
                return false;
            }

            _result.append(_currentTagReplacer);

            _begin = _ch + 1;
            _currentTag = QLatin1String("");
        }
    }
    appendToResult(_end);
    return true;
}

QString Translate(ushort key, const QString& value, const QString& lang_id) {
    DEBUG_LOG(qsl("FakePasscodeTranslate: lang_id=%1").arg(lang_id));
    if (lang_id == "ru") {
        switch (key) {
            case tr::lng_fakepasscode.base: {
                ValueParser parser(key, "Пароль {caption}");
                if (parser.parse()) {
                    return parser.takeResult();
                }
                break;
            }
            case tr::lng_fakepasscodes_list.base:
                return "Список ложных код-паролей";
            case tr::lng_fakeaction_list.base:
                return "Действия";
            case tr::lng_remove_fakepasscode.base:
                return "Удалить ложный код-пароль";
            case tr::lng_show_fakes.base:
                return "Показать ложные код-пароли";
            case tr::lng_add_fakepasscode.base:
                return "Добавить ложный код-пароль";
            case tr::lng_add_fakepasscode_passcode.base:
                return "Ложный код-пароль";
            case tr::lng_fakepasscode_create.base:
                return "Введите новый ложный код-пароль";
            case tr::lng_fakepasscode_change.base:
                return "Изменить ложный код-пароль";
            case tr::lng_fakepasscode_name.base:
                return "Имя ложного код-пароля";
            case tr::lng_passcode_exists.base:
                return "Код-пароль уже используется";
            case tr::lng_clear_proxy.base:
                return "Очистить список прокси";
            case tr::lng_clear_cache.base:
                return "Очистить кеш";
            case tr::lng_logout.base:
                return "Выход из аккаунтов";
            case tr::lng_logout_account.base: {
                ValueParser parser(key, "Выйти из аккаунта {caption}");
                if (parser.parse()) {
                    return parser.takeResult();
                }
                break;
            }
        }
    } else if (lang_id == "be") {
        switch (key) {
            case tr::lng_fakepasscode.base: {
                ValueParser parser(key, "Пароль {caption}");
                if (parser.parse()) {
                    return parser.takeResult();
                }
                break;
            }
            case tr::lng_fakepasscodes_list.base:
                return "Спіс несапраўдных код-пароляў";
            case tr::lng_fakeaction_list.base:
                return "Дзеянні";
            case tr::lng_remove_fakepasscode.base:
                return "Выдаліць несапраўдны код-пароль";
            case tr::lng_show_fakes.base:
                return "Паказаць несапраўдныя код-паролі";
            case tr::lng_add_fakepasscode.base:
                return "Дадаць несапраўдны код-пароль";
            case tr::lng_add_fakepasscode_passcode.base:
                return "Несапраўдны код-пароль";
            case tr::lng_fakepasscode_create.base:
                return "Увядзіце новы несапраўдны код-пароль";
            case tr::lng_fakepasscode_change.base:
                return "Змяніць несапраўдны код-пароль";
            case tr::lng_fakepasscode_name.base:
                return "Імя несапраўднага код-пароля";
            case tr::lng_passcode_exists.base:
                return "Код-пароль ужо выкарыстоўваецца";
            case tr::lng_clear_proxy.base:
                return "Ачысціць спіс проксі";
            case tr::lng_clear_cache.base:
                return "Ачысціць кэш";
            case tr::lng_logout.base:
                return "Выхад з акаўнтаў";
            case tr::lng_logout_account.base: {
                ValueParser parser(key, "Выхад з акаўнта {caption}");
                if (parser.parse()) {
                    return parser.takeResult();
                }
                break;
            }
        }
    }

    return value;
}
