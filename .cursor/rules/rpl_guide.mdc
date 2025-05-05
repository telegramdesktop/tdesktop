# RPL (Reactive Programming Library) Guide

## Coding Style Note

**Use `auto`:** In the actual codebase, variable types are almost always deduced using `auto` (or `const auto`, `const auto &`) rather than being written out explicitly. Examples in this guide may use explicit types for clarity, but prefer `auto` in practice.

```cpp
// Prefer this:
auto intProducer = rpl::single(123);
const auto &lifetime = existingLifetime;

// Instead of this:
rpl::producer<int> intProducer = rpl::single(123);
const rpl::lifetime &lifetime = existingLifetime;

// Sometimes needed if deduction is ambiguous or needs help:
auto user = std::make_shared<UserData>();
auto data = QByteArray::fromHex("...");
```

## Introduction

RPL is the reactive programming library used in this project, residing in the `rpl::` namespace. It allows handling asynchronous streams of data over time.

The core concept is the `rpl::producer`, which represents a stream of values that can be generated over a certain lifetime.

## Producers: `rpl::producer<Type, Error = no_error>`

The fundamental building block is `rpl::producer<Type, Error>`. It produces values of `Type` and can optionally signal an error of type `Error`. By default, `Error` is `rpl::no_error`, indicating that the producer does not explicitly handle error signaling through this mechanism.

```cpp
// A producer that emits integers.
auto intProducer = /* ... */; // Type: rpl::producer<int>

// A producer that emits strings and can potentially emit a CustomError.
auto stringProducerWithError = /* ... */; // Type: rpl::producer<QString, CustomError>
```

Producers are typically lazy; they don't start emitting values until someone subscribes to them.

## Lifetime Management: `rpl::lifetime`

Reactive pipelines have a limited duration, managed by `rpl::lifetime`. An `rpl::lifetime` object essentially holds a collection of cleanup callbacks. When the lifetime ends (either explicitly destroyed or goes out of scope), these callbacks are executed, tearing down the associated pipeline and freeing resources.

```cpp
rpl::lifetime myLifetime;
// ... later ...
// myLifetime is destroyed, cleanup happens.

// Or, pass a lifetime instance to manage a pipeline's duration.
rpl::lifetime &parentLifetime = /* ... get lifetime from context ... */;
```

## Starting a Pipeline: `rpl::start_...`

To consume values from a producer, you start a pipeline using one of the `rpl::start_...` methods. These methods subscribe to the producer and execute callbacks for the events they handle.

The most common method is `rpl::start_with_next`:

```cpp
auto counter = /* ... */; // Type: rpl::producer<int>
rpl::lifetime lifetime;

// Counter is consumed here, use std::move if it's an l-value.
std::move(
    counter
) | rpl::start_with_next([=](int nextValue) {
    // Process the next integer value emitted by the producer.
    qDebug() << "Received: " << nextValue;
}, lifetime); // Pass the lifetime to manage the subscription.
// Note: `counter` is now in a moved-from state and likely invalid.

// If you need to start the same producer multiple times, duplicate it:
// rpl::duplicate(counter) | rpl::start_with_next(...);

// If you DON'T pass a lifetime to a start_... method:
auto counter2 = /* ... */; // Type: rpl::producer<int>
rpl::lifetime subscriptionLifetime = std::move(
    counter2
) | rpl::start_with_next([=](int nextValue) { /* ... */ });
// The returned lifetime MUST be stored. If it's discarded immediately,
// the subscription stops instantly.
// `counter2` is also moved-from here.
```

Other variants allow handling errors (`_error`) and completion (`_done`):

```cpp
auto dataStream = /* ... */; // Type: rpl::producer<QString, Error>
rpl::lifetime lifetime;

// Assuming dataStream might be used again, we duplicate it for the first start.
// If it's the only use, std::move(dataStream) would be preferred.
rpl::duplicate(
    dataStream
) | rpl::start_with_error([=](Error &&error) {
    // Handle the error signaled by the producer.
    qDebug() << "Error: " << error.text();
}, lifetime);

// Using dataStream again, perhaps duplicated again or moved if last use.
rpl::duplicate(
    dataStream
) | rpl::start_with_done([=]() {
    // Execute when the producer signals it's finished emitting values.
    qDebug() << "Stream finished.";
}, lifetime);

// Last use of dataStream, so we move it.
std::move(
    dataStream
) | rpl::start_with_next_error_done(
    [=](QString &&value) { /* handle next value */ },
    [=](Error &&error) { /* handle error */ },
    [=]() { /* handle done */ },
    lifetime);
```

## Transforming Producers

RPL provides functions to create new producers by transforming existing ones:

*   `rpl::map`: Transforms each value emitted by a producer.
    ```cpp
    auto ints = /* ... */; // Type: rpl::producer<int>
    // The pipe operator often handles the move implicitly for chained transformations.
    auto strings = std::move(
        ints // Explicit move is safer
    ) | rpl::map([](int value) {
        return QString::number(value * 2);
    }); // Emits strings like "0", "2", "4", ...
    ```

*   `rpl::filter`: Emits only the values from a producer that satisfy a condition.
    ```cpp
    auto ints = /* ... */; // Type: rpl::producer<int>
    auto evenInts = std::move(
        ints // Explicit move is safer
    ) | rpl::filter([](int value) {
        return (value % 2 == 0);
    }); // Emits only even numbers.
    ```

## Combining Producers

You can combine multiple producers into one:

*   `rpl::combine`: Combines the latest values from multiple producers whenever *any* of them emits a new value. Requires all producers to have emitted at least one value initially.
    While it produces a `std::tuple`, subsequent operators like `map`, `filter`, and `start_with_next` can automatically unpack this tuple into separate lambda arguments.
    ```cpp
    auto countProducer = rpl::single(1); // Type: rpl::producer<int>
    auto textProducer = rpl::single(u"hello"_q); // Type: rpl::producer<QString>
    rpl::lifetime lifetime;

    // rpl::combine takes producers by const-ref internally and duplicates,
    // so move/duplicate is usually not strictly needed here unless you
    // want to signal intent or manage the lifetime of p1/p2 explicitly.
    auto combined = rpl::combine(
        countProducer, // or rpl::duplicate(countProducer)
        textProducer   // or rpl::duplicate(textProducer)
    );

    // Starting the combined producer consumes it.
    // The lambda receives unpacked arguments, not the tuple itself.
    std::move(
        combined
    ) | rpl::start_with_next([=](int count, const QString &text) {
        // No need for std::get<0>(latest), etc.
        qDebug() << "Combined: Count=" << count << ", Text=" << text;
    }, lifetime);

    // This also works with map, filter, etc.
    std::move(
        combined
    ) | rpl::filter([=](int count, const QString &text) {
        return count > 0 && !text.isEmpty();
    }) | rpl::map([=](int count, const QString &text) {
        return text.repeated(count);
    }) | rpl::start_with_next([=](const QString &result) {
        qDebug() << "Mapped & Filtered: " << result;
    }, lifetime);
    ```

*   `rpl::merge`: Merges the output of multiple producers of the *same type* into a single producer. It emits a value whenever *any* of the source producers emits a value.
    ```cpp
    auto sourceA = /* ... */; // Type: rpl::producer<QString>
    auto sourceB = /* ... */; // Type: rpl::producer<QString>

    // rpl::merge also duplicates internally.
    auto merged = rpl::merge(sourceA, sourceB);

    // Starting the merged producer consumes it.
    std::move(
        merged
    ) | rpl::start_with_next([=](QString &&value) {
        // Receives values from either sourceA or sourceB as they arrive.
        qDebug() << "Merged value: " << value;
    }, lifetime);
    ```

## Key Concepts Summary

*   Use `rpl::producer<Type, Error>` to represent streams of values.
*   Manage subscription duration using `rpl::lifetime`.
*   Pass `rpl::lifetime` to `rpl::start_...` methods.
*   If `rpl::lifetime` is not passed, **store the returned lifetime** to keep the subscription active.
*   Use operators like `| rpl::map`, `| rpl::filter` to transform streams.
*   Use `rpl::combine` or `rpl::merge` to combine streams.
*   When starting a chain (`std::move(producer) | rpl::map(...)`), explicitly move the initial producer.
*   These functions typically duplicate their input producers internally.
*   Starting a pipeline consumes the producer; use `