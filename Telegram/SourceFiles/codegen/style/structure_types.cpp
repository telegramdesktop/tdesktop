/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/style/structure_types.h"

namespace codegen {
namespace style {
namespace structure {

struct Value::DataTypes {
	class TInt : public DataBase {
	public:
		TInt(int value) : value_(value) {
		}
		int Int() const override { return value_; }

	private:
		int value_;

	};

	class TDouble : public DataBase {
	public:
		TDouble(double value) : value_(value) {
		}
		double Double() const override { return value_; }

	private:
		double value_;

	};

	class TString : public DataBase {
	public:
		TString(std::string value) : value_(value) {
		}
		std::string String() const override { return value_; }

	private:
		std::string value_;

	};

	class TPoint : public DataBase {
	public:
		TPoint(data::point value) : value_(value) {
		}
		data::point Point() const override { return value_; }

	private:
		data::point value_;

	};

	class TSize : public DataBase {
	public:
		TSize(data::size value) : value_(value) {
		}
		data::size Size() const override { return value_; }

	private:
		data::size value_;

	};

	class TColor : public DataBase {
	public:
		TColor(data::color value) : value_(value) {
		}
		data::color Color() const override { return value_; }

	private:
		data::color value_;

	};

	class TMargins : public DataBase {
	public:
		TMargins(data::margins value) : value_(value) {
		}
		data::margins Margins() const override { return value_; }

	private:
		data::margins value_;

	};

	class TFont : public DataBase {
	public:
		TFont(data::font value) : value_(value) {
		}
		data::font Font() const override { return value_; }

	private:
		data::font value_;

	};

	class TIcon : public DataBase {
	public:
		TIcon(data::icon value) : value_(value) {
		}
		data::icon Icon() const override { return value_; }

	private:
		data::icon value_;

	};

	class TFields : public DataBase {
	public:
		TFields(data::fields value) : value_(value) {
		}
		const data::fields *Fields() const override { return &value_; }
		data::fields *Fields() override { return &value_; }

	private:
		data::fields value_;

	};
};

Value::Value() : Value(TypeTag::Invalid, std::make_shared<DataBase>()) {
}

Value::Value(data::point value) : Value(TypeTag::Point, std::make_shared<DataTypes::TPoint>(value)) {
}

Value::Value(data::size value) : Value(TypeTag::Size, std::make_shared<DataTypes::TSize>(value)) {
}

Value::Value(data::color value) : Value(TypeTag::Color, std::make_shared<DataTypes::TColor>(value)) {
}

Value::Value(data::margins value) : Value(TypeTag::Margins, std::make_shared<DataTypes::TMargins>(value)) {
}

Value::Value(data::font value) : Value(TypeTag::Font, std::make_shared<DataTypes::TFont>(value)) {
}

Value::Value(data::icon value) : Value(TypeTag::Icon, std::make_shared<DataTypes::TIcon>(value)) {
}

Value::Value(const FullName &type, data::fields value)
: type_ { TypeTag::Struct, type }
, data_(std::make_shared<DataTypes::TFields>(value)) {
}

Value::Value(TypeTag type, double value) : Value(type, std::make_shared<DataTypes::TDouble>(value)) {
	if (type_.tag != TypeTag::Double) {
		type_.tag = TypeTag::Invalid;
		data_ = std::make_shared<DataBase>();
	}
}

Value::Value(TypeTag type, int value) : Value(type, std::make_shared<DataTypes::TInt>(value)) {
	if (type_.tag != TypeTag::Int && type_.tag != TypeTag::Pixels) {
		type_.tag = TypeTag::Invalid;
		data_ = std::make_shared<DataBase>();
	}
}

Value::Value(TypeTag type, std::string value) : Value(type, std::make_shared<DataTypes::TString>(value)) {
	if (type_.tag != TypeTag::String &&
		type_.tag != TypeTag::Align) {
		type_.tag = TypeTag::Invalid;
		data_ = std::make_shared<DataBase>();
	}
}

Value::Value(Type type, Qt::Initialization) : type_(type) {
	switch (type_.tag) {
	case TypeTag::Invalid: data_ = std::make_shared<DataBase>(); break;
	case TypeTag::Int: data_ = std::make_shared<DataTypes::TInt>(0); break;
	case TypeTag::Double: data_ = std::make_shared<DataTypes::TDouble>(0.); break;
	case TypeTag::Pixels: data_ = std::make_shared<DataTypes::TInt>(0); break;
	case TypeTag::String: data_ = std::make_shared<DataTypes::TString>(""); break;
	case TypeTag::Color: data_ = std::make_shared<DataTypes::TColor>(data::color { 0, 0, 0, 255 }); break;
	case TypeTag::Point: data_ = std::make_shared<DataTypes::TPoint>(data::point { 0, 0 }); break;
	case TypeTag::Size: data_ = std::make_shared<DataTypes::TSize>(data::size { 0, 0 }); break;
	case TypeTag::Align: data_ = std::make_shared<DataTypes::TString>("topleft"); break;
	case TypeTag::Margins: data_ = std::make_shared<DataTypes::TMargins>(data::margins { 0, 0, 0, 0 }); break;
	case TypeTag::Font: data_ = std::make_shared<DataTypes::TFont>(data::font { "", 13, 0 }); break;
	case TypeTag::Icon: data_ = std::make_shared<DataTypes::TIcon>(data::icon {}); break;
	case TypeTag::Struct: data_ = std::make_shared<DataTypes::TFields>(data::fields {}); break;
	}
}

Value::Value(TypeTag type, std::shared_ptr<DataBase> &&data) : type_ { type }, data_(std::move(data)) {
}

} // namespace structure
} // namespace style
} // namespace codegen
