/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_instance.h"

#include "messenger.h"
#include "storage/serialize_common.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "boxes/confirm_box.h"
#include "lang/lang_file_parser.h"
#include "base/qthelp_regex.h"

namespace Lang {
namespace {

const auto kSerializeVersionTag = qsl("#new");
constexpr auto kSerializeVersion = 1;
constexpr auto kDefaultLanguage = str_const("en");
constexpr auto kCloudLangPackName = str_const("tdesktop");
constexpr auto kCustomLanguage = str_const("#custom");
constexpr auto kLangValuesLimit = 20000;

std::vector<QString> PrepareDefaultValues() {
	auto result = std::vector<QString>();
	result.reserve(kLangKeysCount);
	for (auto i = 0; i != kLangKeysCount; ++i) {
		result.emplace_back(GetOriginalValue(LangKey(i)));
	}
	return result;
}

class ValueParser {
public:
	ValueParser(
		const QByteArray &key,
		LangKey keyIndex,
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

	const QByteArray &_key;
	LangKey _keyIndex = kLangKeysCount;

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

ValueParser::ValueParser(const QByteArray &key, LangKey keyIndex, const QByteArray &value)
: _key(key)
, _keyIndex(keyIndex)
, _currentTag("")
, _begin(value.constData())
, _ch(_begin)
, _end(_begin + value.size()) {
}

void ValueParser::appendToResult(const char *nextBegin) {
	if (_ch > _begin) _result.append(QString::fromUtf8(_begin, _ch - _begin));
	_begin = nextBegin;
}

bool ValueParser::logError(const QString &text) {
	_failed = true;
	auto loggedKey = (_currentTag.size() > 0) ? (_key + QString(':') + _currentTag) : QString(_key);
	LOG(("Lang Error: %1 (key '%2')").arg(text).arg(loggedKey));
	return false;
}

bool ValueParser::readTag() {
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

QString PrepareTestValue(const QString &current, QChar filler) {
	auto size = current.size();
	auto result = QString(size + 1, filler);
	auto inCommand = false;
	for (auto i = 0; i != size; ++i) {
		auto ch = current[i];
		auto newInCommand = (ch.unicode() == TextCommand) ? (!inCommand) : inCommand;
		if (inCommand || newInCommand || ch.isSpace()) {
			result[i + 1] = ch;
		}
		inCommand = newInCommand;
	}
	return result;
}

QString PluralCodeForCustom(
	const QString &absolutePath,
	const QString &relativePath) {
	const auto path = !absolutePath.isEmpty()
		? absolutePath
		: relativePath;
	const auto name = QFileInfo(path).fileName();
	if (const auto match = qthelp::regex_match(
			"_([a-z]{2,3}_[A-Z]{2,3}|\\-[a-z]{2,3})?\\.",
			name)) {
		return match->captured(1);
	}
	return DefaultLanguageId();
}

template <typename Save>
void ParseKeyValue(
		const QByteArray &key,
		const QByteArray &value,
		Save &&save) {
	const auto index = GetKeyIndex(QLatin1String(key));
	if (index != kLangKeysCount) {
		ValueParser parser(key, index, value);
		if (parser.parse()) {
			save(index, parser.takeResult());
		}
	} else if (!key.startsWith("cloud_")) {
		DEBUG_LOG(("Lang Warning: Unknown key '%1'"
			).arg(QString::fromLatin1(key)));
	}
}

} // namespace

QString DefaultLanguageId() {
	return str_const_toString(kDefaultLanguage);
}

QString LanguageIdOrDefault(const QString &id) {
	return !id.isEmpty() ? id : DefaultLanguageId();
}

QString CloudLangPackName() {
	return str_const_toString(kCloudLangPackName);
}

QString CustomLanguageId() {
	return str_const_toString(kCustomLanguage);
}

Language DefaultLanguage() {
	return Language{
		qsl("en"),
		QString(),
		QString(),
		qsl("English"),
		qsl("English"),
	};
}

struct Instance::PrivateTag {
};

Instance::Instance()
: _values(PrepareDefaultValues())
, _nonDefaultSet(kLangKeysCount, 0) {
}

Instance::Instance(not_null<Instance*> derived, const PrivateTag &)
: _derived(derived)
, _nonDefaultSet(kLangKeysCount, 0) {
}

void Instance::switchToId(const Language &data) {
	reset(data);
	if (_id == qstr("#TEST_X") || _id == qstr("#TEST_0")) {
		for (auto &value : _values) {
			value = PrepareTestValue(value, _id[5]);
		}
		if (!_derived) {
			_updated.notify();
		}
	}
	updatePluralRules();
}

void Instance::setBaseId(const QString &baseId, const QString &pluralId) {
	if (baseId.isEmpty()) {
		_base = nullptr;
	} else {
		if (!_base) {
			_base = std::make_unique<Instance>(this, PrivateTag{});
		}
		_base->switchToId({ baseId, pluralId });
	}
}

void Instance::switchToCustomFile(const QString &filePath) {
	if (loadFromCustomFile(filePath)) {
		Local::writeLangPack();
		_updated.notify();
	}
}

void Instance::reset(const Language &data) {
	const auto computedPluralId = !data.pluralId.isEmpty()
		? data.pluralId
		: !data.baseId.isEmpty()
		? data.baseId
		: data.id;
	setBaseId(data.baseId, computedPluralId);
	_id = LanguageIdOrDefault(data.id);
	_pluralId = computedPluralId;
	_name = data.name;
	_nativeName = data.nativeName;

	_legacyId = kLegacyLanguageNone;
	_customFilePathAbsolute = QString();
	_customFilePathRelative = QString();
	_customFileContent = QByteArray();
	_version = 0;
	_nonDefaultValues.clear();
	for (auto i = 0, count = int(_values.size()); i != count; ++i) {
		_values[i] = GetOriginalValue(LangKey(i));
	}
	ranges::fill(_nonDefaultSet, 0);
}

QString Instance::systemLangCode() const {
	if (_systemLanguage.isEmpty()) {
		_systemLanguage = Platform::SystemLanguage();
		if (_systemLanguage.isEmpty()) {
			auto uiLanguages = QLocale::system().uiLanguages();
			if (!uiLanguages.isEmpty()) {
				_systemLanguage = uiLanguages.front();
			}
			if (_systemLanguage.isEmpty()) {
				_systemLanguage = DefaultLanguageId();
			}
		}
	}
	return _systemLanguage;
}

QString Instance::cloudLangCode(Pack pack) const {
	return (isCustom() || id().isEmpty())
		? DefaultLanguageId()
		: id(pack);
}

QString Instance::id() const {
	return id(Pack::Current);
}

QString Instance::baseId() const {
	return id(Pack::Base);
}

QString Instance::name() const {
	return _name.isEmpty() ? getValue(lng_language_name) : _name;
}

QString Instance::nativeName() const {
	return _nativeName.isEmpty()
		? getValue(lng_language_name)
		: _nativeName;
}

QString Instance::id(Pack pack) const {
	return (pack != Pack::Base)
		? _id
		: _base
		? _base->id(Pack::Current)
		: QString();
}

bool Instance::isCustom() const {
	return (_id == CustomLanguageId())
		|| (_id == qstr("#TEST_X"))
		|| (_id == qstr("#TEST_0"));
}

int Instance::version(Pack pack) const {
	return (pack != Pack::Base)
		? _version
		: _base
		? _base->version(Pack::Current)
		: 0;
}

QString Instance::langPackName() const {
	return isCustom() ? QString() : CloudLangPackName();
}

QByteArray Instance::serialize() const {
	auto size = Serialize::stringSize(kSerializeVersionTag)
		+ sizeof(qint32) // serializeVersion
		+ Serialize::stringSize(_id)
		+ Serialize::stringSize(_pluralId)
		+ Serialize::stringSize(_name)
		+ Serialize::stringSize(_nativeName)
		+ sizeof(qint32) // version
		+ Serialize::stringSize(_customFilePathAbsolute)
		+ Serialize::stringSize(_customFilePathRelative)
		+ Serialize::bytearraySize(_customFileContent)
		+ sizeof(qint32); // _nonDefaultValues.size()
	for (auto &nonDefault : _nonDefaultValues) {
		size += Serialize::bytearraySize(nonDefault.first)
			+ Serialize::bytearraySize(nonDefault.second);
	}
	const auto base = _base ? _base->serialize() : QByteArray();
	size += Serialize::bytearraySize(base);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< kSerializeVersionTag
			<< qint32(kSerializeVersion)
			<< _id
			<< _pluralId
			<< _name
			<< _nativeName
			<< qint32(_version)
			<< _customFilePathAbsolute
			<< _customFilePathRelative
			<< _customFileContent
			<< qint32(_nonDefaultValues.size());
		for (const auto &nonDefault : _nonDefaultValues) {
			stream << nonDefault.first << nonDefault.second;
		}
		stream << base;
	}
	return result;
}

void Instance::fillFromSerialized(
		const QByteArray &data,
		int dataAppVersion) {
	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 serializeVersion = 0;
	QString serializeVersionTag;
	QString id, pluralId, name, nativeName;
	qint32 version = 0;
	QString customFilePathAbsolute, customFilePathRelative;
	QByteArray customFileContent;
	qint32 nonDefaultValuesCount = 0;
	stream >> serializeVersionTag;
	const auto legacyFormat = (serializeVersionTag != kSerializeVersionTag);
	if (legacyFormat) {
		id = serializeVersionTag;
		stream
			>> version
			>> customFilePathAbsolute
			>> customFilePathRelative
			>> customFileContent
			>> nonDefaultValuesCount;
	} else {
		stream >> serializeVersion;
		if (serializeVersion == kSerializeVersion) {
			stream
				>> id
				>> pluralId
				>> name
				>> nativeName
				>> version
				>> customFilePathAbsolute
				>> customFilePathRelative
				>> customFileContent
				>> nonDefaultValuesCount;
		} else {
			LOG(("Lang Error: Unsupported serialize version."));
			return;
		}
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("Lang Error: Could not read data from serialized langpack."));
		return;
	}
	if (nonDefaultValuesCount > kLangValuesLimit) {
		LOG(("Lang Error: Values count limit exceeded: %1"
			).arg(nonDefaultValuesCount));
		return;
	}

	if (!customFilePathAbsolute.isEmpty()) {
		id = CustomLanguageId();
		auto currentCustomFileContent = Lang::FileParser::ReadFile(
			customFilePathAbsolute,
			customFilePathRelative);
		if (!currentCustomFileContent.isEmpty()
			&& currentCustomFileContent != customFileContent) {
			fillFromCustomContent(
				customFilePathAbsolute,
				customFilePathRelative,
				currentCustomFileContent);
			Local::writeLangPack();
			return;
		}
	}

	std::vector<QByteArray> nonDefaultStrings;
	nonDefaultStrings.reserve(2 * nonDefaultValuesCount);
	for (auto i = 0; i != nonDefaultValuesCount; ++i) {
		QByteArray key, value;
		stream >> key >> value;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Lang Error: "
				"Could not read data from serialized langpack."));
			return;
		}

		nonDefaultStrings.push_back(key);
		nonDefaultStrings.push_back(value);
	}

	_base = nullptr;
	QByteArray base;
	if (legacyFormat) {
		if (!stream.atEnd()) {
			stream >> pluralId;
		} else {
			pluralId = id;
		}
		if (!stream.atEnd()) {
			stream >> base;
			if (base.isEmpty()) {
				stream.setStatus(QDataStream::ReadCorruptData);
			}
		}
		if (stream.status() != QDataStream::Ok) {
			LOG(("Lang Error: "
				"Could not read data from serialized langpack."));
			return;
		}
	} else {
		stream >> base;
	}
	if (!base.isEmpty()) {
		_base = std::make_unique<Instance>(this, PrivateTag{});
		_base->fillFromSerialized(base, dataAppVersion);
	}

	_id = id;
	_pluralId = (id == CustomLanguageId())
		? PluralCodeForCustom(
			customFilePathAbsolute,
			customFilePathRelative)
		: pluralId;
	_name = name;
	_nativeName = nativeName;
	_version = version;
	_customFilePathAbsolute = customFilePathAbsolute;
	_customFilePathRelative = customFilePathRelative;
	_customFileContent = customFileContent;
	LOG(("Lang Info: Loaded cached, keys: %1").arg(nonDefaultValuesCount));
	for (auto i = 0, count = nonDefaultValuesCount * 2; i != count; i += 2) {
		applyValue(nonDefaultStrings[i], nonDefaultStrings[i + 1]);
	}
	updatePluralRules();
}

void Instance::loadFromContent(const QByteArray &content) {
	Lang::FileParser loader(content, [this](QLatin1String key, const QByteArray &value) {
		applyValue(QByteArray(key.data(), key.size()), value);
	});
	if (!loader.errors().isEmpty()) {
		LOG(("Lang load errors: %1").arg(loader.errors()));
	} else if (!loader.warnings().isEmpty()) {
		LOG(("Lang load warnings: %1").arg(loader.warnings()));
	}
}

void Instance::fillFromCustomContent(
		const QString &absolutePath,
		const QString &relativePath,
		const QByteArray &content) {
	setBaseId(QString(), QString());
	_id = CustomLanguageId();
	_pluralId = PluralCodeForCustom(absolutePath, relativePath);
	_name = _nativeName = QString();
	loadFromCustomContent(absolutePath, relativePath, content);
}

void Instance::loadFromCustomContent(
		const QString &absolutePath,
		const QString &relativePath,
		const QByteArray &content) {
	_version = 0;
	_customFilePathAbsolute = absolutePath;
	_customFilePathRelative = relativePath;
	_customFileContent = content;
	loadFromContent(_customFileContent);
}

bool Instance::loadFromCustomFile(const QString &filePath) {
	auto absolutePath = QFileInfo(filePath).absoluteFilePath();
	auto relativePath = QDir().relativeFilePath(filePath);
	auto content = Lang::FileParser::ReadFile(absolutePath, relativePath);
	if (!content.isEmpty()) {
		reset({
			CustomLanguageId(),
			PluralCodeForCustom(absolutePath, relativePath) });
		loadFromCustomContent(absolutePath, relativePath, content);
		updatePluralRules();
		return true;
	}
	return false;
}

void Instance::fillFromLegacy(int legacyId, const QString &legacyPath) {
	if (legacyId == kLegacyDefaultLanguage) {
		_legacyId = legacyId;

		// We suppose that user didn't switch to the default language,
		// so we will suggest him to switch to his language if we get it.
		//
		// The old available languages (de/it/nl/ko/es/pt_BR) won't be
		// suggested anyway, because everyone saw the suggestion in intro.
		_id = QString();// str_const_toString(kLegacyLanguages[legacyId]);
	} else if (legacyId == kLegacyCustomLanguage) {
		auto absolutePath = QFileInfo(legacyPath).absoluteFilePath();
		auto relativePath = QDir().relativeFilePath(absolutePath);
		auto content = Lang::FileParser::ReadFile(absolutePath, relativePath);
		if (!content.isEmpty()) {
			_legacyId = legacyId;
			fillFromCustomContent(absolutePath, relativePath, content);
		}
	} else if (legacyId > kLegacyDefaultLanguage && legacyId < base::array_size(kLegacyLanguages)) {
		auto languageId = str_const_toString(kLegacyLanguages[legacyId]);
		auto resourcePath = qsl(":/langs/lang_") + languageId + qsl(".strings");
		auto content = Lang::FileParser::ReadFile(resourcePath, resourcePath);
		if (!content.isEmpty()) {
			_legacyId = legacyId;
			_id = languageId;
			_version = 0;
			loadFromContent(content);
		}
	}
	_id = LanguageIdOrDefault(ConvertLegacyLanguageId(_id));
	if (!isCustom()) {
		_pluralId = _id;
	}
	_name = _nativeName = QString();
	_base = nullptr;
	updatePluralRules();
}

// SetCallback takes two QByteArrays: key, value.
// It is called for all key-value pairs in string.
// ResetCallback takes one QByteArray: key.
template <typename SetCallback, typename ResetCallback>
void HandleString(
		const MTPLangPackString &string,
		SetCallback setCallback,
		ResetCallback resetCallback) {
	string.match([&](const MTPDlangPackString &data) {
		setCallback(qba(data.vkey), qba(data.vvalue));
	}, [&](const MTPDlangPackStringPluralized &data) {
		const auto key = qba(data.vkey);
		setCallback(
			key + "#zero",
			data.has_zero_value() ? qba(data.vzero_value) : QByteArray());
		setCallback(
			key + "#one",
			data.has_one_value() ? qba(data.vone_value) : QByteArray());
		setCallback(
			key + "#two",
			data.has_two_value() ? qba(data.vtwo_value) : QByteArray());
		setCallback(
			key + "#few",
			data.has_few_value() ? qba(data.vfew_value) : QByteArray());
		setCallback(
			key + "#many",
			data.has_many_value() ? qba(data.vmany_value) : QByteArray());
		setCallback(key + "#other", qba(data.vother_value));
	}, [&](const MTPDlangPackStringDeleted &data) {
		auto key = qba(data.vkey);
		resetCallback(key);
		const auto postfixes = {
			"#zero",
			"#one",
			"#two",
			"#few",
			"#many",
			"#other"
		};
		for (const auto plural : postfixes) {
			resetCallback(key + plural);
		}
	});
}

void Instance::applyDifference(
		Pack pack,
		const MTPDlangPackDifference &difference) {
	switch (pack) {
	case Pack::Current:
		applyDifferenceToMe(difference);
		break;
	case Pack::Base:
		Assert(_base != nullptr);
		_base->applyDifference(Pack::Current, difference);
		break;
	default:
		Unexpected("Pack in Instance::applyDifference.");
	}
}

void Instance::applyDifferenceToMe(
		const MTPDlangPackDifference &difference) {
	Expects(LanguageIdOrDefault(_id) == qs(difference.vlang_code));
	Expects(difference.vfrom_version.v <= _version);

	_version = difference.vversion.v;
	for (const auto &string : difference.vstrings.v) {
		HandleString(string, [&](auto &&key, auto &&value) {
			applyValue(key, value);
		}, [&](auto &&key) {
			resetValue(key);
		});
	}
	if (!_derived) {
		_updated.notify();
	} else {
		_derived->_updated.notify();
	}
}

std::map<LangKey, QString> Instance::ParseStrings(
		const MTPVector<MTPLangPackString> &strings) {
	auto result = std::map<LangKey, QString>();
	for (const auto &string : strings.v) {
		HandleString(string, [&](auto &&key, auto &&value) {
			ParseKeyValue(key, value, [&](LangKey key, QString &&value) {
				result[key] = std::move(value);
			});
		}, [&](auto &&key) {
			auto keyIndex = GetKeyIndex(QLatin1String(key));
			if (keyIndex != kLangKeysCount) {
				result.erase(keyIndex);
			}
		});
	}
	return result;
}

QString Instance::getNonDefaultValue(const QByteArray &key) const {
	const auto i = _nonDefaultValues.find(key);
	return (i != end(_nonDefaultValues))
		? QString::fromUtf8(i->second)
		: _base
		? _base->getNonDefaultValue(key)
		: QString();
}

void Instance::applyValue(const QByteArray &key, const QByteArray &value) {
	_nonDefaultValues[key] = value;
	ParseKeyValue(key, value, [&](LangKey key, QString &&value) {
		_nonDefaultSet[key] = 1;
		if (!_derived) {
			_values[key] = std::move(value);
		} else if (!_derived->_nonDefaultSet[key]) {
			_derived->_values[key] = std::move(value);
		}
	});
}

void Instance::updatePluralRules() {
	if (_pluralId.isEmpty()) {
		_pluralId = isCustom()
			? PluralCodeForCustom(
				_customFilePathAbsolute,
				_customFilePathRelative)
			: LanguageIdOrDefault(_id);
	}
	UpdatePluralRules(_pluralId);
}

void Instance::resetValue(const QByteArray &key) {
	_nonDefaultValues.erase(key);

	const auto keyIndex = GetKeyIndex(QLatin1String(key));
	if (keyIndex != kLangKeysCount) {
		_nonDefaultSet[keyIndex] = 0;
		if (!_derived) {
			const auto base = _base
				? _base->getNonDefaultValue(key)
				: QString();
			_values[keyIndex] = !base.isEmpty()
				? base
				: GetOriginalValue(keyIndex);
		} else if (!_derived->_nonDefaultSet[keyIndex]) {
			_derived->_values[keyIndex] = GetOriginalValue(keyIndex);
		}
	}
}

Instance &Current() {
	return Messenger::Instance().langpack();
}

rpl::producer<QString> Viewer(LangKey key) {
	return rpl::single(
		Current().getValue(key)
	) | then(base::ObservableViewer(
		Current().updated()
	) | rpl::map([=] {
		return Current().getValue(key);
	}));
}

} // namespace Lang
