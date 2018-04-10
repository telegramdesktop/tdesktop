/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// rpl - reactive programming library

#include <rpl/lifetime.h>
#include <rpl/consumer.h>
#include <rpl/producer.h>
#include <rpl/event_stream.h>

#include <rpl/range.h>
#include <rpl/complete.h>
#include <rpl/fail.h>
#include <rpl/never.h>

#include <rpl/take.h>
#include <rpl/then.h>
#include <rpl/deferred.h>
#include <rpl/map.h>
#include <rpl/mappers.h>
#include <rpl/merge.h>
#include <rpl/filter.h>
#include <rpl/distinct_until_changed.h>
#include <rpl/type_erased.h>
#include <rpl/flatten_latest.h>
#include <rpl/combine.h>
#include <rpl/combine_previous.h>
#include <rpl/variable.h>

#include <rpl/before_next.h>
#include <rpl/after_next.h>
