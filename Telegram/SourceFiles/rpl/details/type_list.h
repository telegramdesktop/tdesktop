/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <type_traits>

namespace rpl {
namespace details {
namespace type_list {

template <typename ...Types>
struct list {
};

template <typename Type, typename ...Types>
struct list<Type, Types...> {
	using head = Type;
	using tail = list<Types...>;
};

using empty_list = list<>;

template <typename TypeList>
using head_t = typename TypeList::head;

template <typename TypeList>
using tail_t = typename TypeList::tail;

template <typename Head, typename Tail>
struct construct;

template <typename Head, typename Tail>
using construct_t = typename construct<Head, Tail>::type;

template <typename Head, typename ...Types>
struct construct<Head, list<Types...>> {
	using type = list<Head, Types...>;
};

template <typename TypeList>
struct size;

template <typename TypeList>
constexpr std::size_t size_v = size<TypeList>::value;

template <typename ...Types>
struct size<list<Types...>>
	: std::integral_constant<
	std::size_t,
	sizeof...(Types)> {
};

template <typename TypeList>
constexpr bool empty_v = (size_v<TypeList> == 0);

template <typename TypeList>
struct empty : std::bool_constant<empty_v<TypeList>> {
};

template <std::size_t Index, typename TypeList>
struct get;

template <std::size_t Index, typename TypeList>
using get_t = typename get<Index, TypeList>::type;

template <std::size_t Index, typename TypeList>
struct get {
	using type = get_t<Index - 1, tail_t<TypeList>>;
};

template <typename TypeList>
struct get<0, TypeList> {
	using type = head_t<TypeList>;
};

template <typename TypeList1, typename TypeList2>
struct concat;

template <typename TypeList1, typename TypeList2>
using concat_t = typename concat<TypeList1, TypeList2>::type;

template <typename ...Types1, typename ...Types2>
struct concat<list<Types1...>, list<Types2...>> {
	using type = list<Types1..., Types2...>;
};

template <typename TypeList, typename Type>
struct remove_all;

template <typename TypeList, typename Type>
using remove_all_t = typename remove_all<TypeList, Type>::type;

template <typename TypeList, typename Type>
struct remove_all {
	using head = head_t<TypeList>;
	using tail = tail_t<TypeList>;
	using clean_tail = remove_all_t<tail, Type>;
	using type = std::conditional_t<
		std::is_same_v<head, Type>,
		clean_tail,
		construct_t<head, clean_tail>>;
};

template <typename Type>
struct remove_all<empty_list, Type> {
	using type = empty_list;
};

template <typename TypeList>
struct last;

template <typename TypeList>
using last_t = typename last<TypeList>::type;

template <typename TypeList>
struct last {
	using type = last_t<tail_t<TypeList>>;
};

template <typename Type>
struct last<list<Type>> {
	using type = Type;
};

template <typename TypeList>
struct chop_last;

template <typename TypeList>
using chop_last_t = typename chop_last<TypeList>::type;

template <typename TypeList>
struct chop_last {
	using type = construct_t<
		head_t<TypeList>,
		chop_last_t<tail_t<TypeList>>>;
};

template <typename Type>
struct chop_last<list<Type>> {
	using type = empty_list;
};

template <typename TypeList>
struct distinct;

template <typename TypeList>
using distinct_t = typename distinct<TypeList>::type;

template <typename TypeList>
struct distinct {
	using type = construct_t<
		head_t<TypeList>,
		distinct_t<
		remove_all_t<tail_t<TypeList>, head_t<TypeList>>>>;
};

template <>
struct distinct<empty_list> {
	using type = empty_list;
};

template <typename TypeList, template <typename ...> typename To>
struct extract_to;

template <typename TypeList, template <typename ...> typename To>
using extract_to_t = typename extract_to<TypeList, To>::type;

template <typename ...Types, template <typename ...> typename To>
struct extract_to<list<Types...>, To> {
	using type = To<Types...>;
};

} // namespace type_list
} // namespace details
} // namespace rpl
