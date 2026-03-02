# Code Review Style Guide

This file contains style and formatting rules that the review subagent must check and fix. These are mechanical issues that should always be caught during code review.

## Empty line before closing brace

Always add an empty line before the closing brace of a **class** (which has one or more sections like `public:` / `private:`). Plain **structs** with just data members do NOT get a trailing empty line — they are compact: `struct Foo { data lines; };`.

```cpp
// BAD:
class MyClass {
public:
    void foo();

private:
    int _value = 0;
};

// GOOD:
class MyClass {
public:
    void foo();

private:
    int _value = 0;

};
```

## Multi-line expressions — operators at the start of continuation lines

When splitting an expression across multiple lines, place operators (like `&&`, `||`, `;`, `+`, etc.) at the **beginning** of continuation lines, not at the end of the previous line. This makes it immediately obvious from the left edge whether a line is a continuation or new code.

```cpp
// BAD - continuation looks like scope code:
if (const auto &lottie = animation->lottie;
	lottie && lottie->valid() && lottie->framesCount() > 1) {
	lottie->animate([=] {

// GOOD - semicolon at start signals continuation:
if (const auto &lottie = animation->lottie
	; lottie && lottie->valid() && lottie->framesCount() > 1) {
	lottie->animate([=] {

// BAD - trailing && makes next line look like independent code:
if (veryLongExpression() &&
	anotherLongExpression() &&
	anotherOne()) {
	doSomething();

// GOOD - leading && clearly marks continuation:
if (veryLongExpression()
	&& anotherLongExpression()
	&& anotherOne()) {
	doSomething();
```

## Minimize type checks — prefer direct cast over is + as

Don't check a type and then cast — just cast and check for null. `asUser()` already returns `nullptr` when the peer is not a user, so calling `isUser()` first is redundant. The same applies to `asChannel()`, `asChat()`, etc.

```cpp
// BAD - redundant isUser() check, then asUser():
if (peer && peer->isUser()) {
	peer->asUser()->setNoForwardFlags(

// GOOD - just cast and null-check:
if (const auto user = peer->asUser()) {
	user->setNoForwardFlags(
```

When you need a specific subtype, look up the specific subtype directly instead of loading a generic type and then casting:

```cpp
// BAD - loads generic peer, then casts:
if (const auto peer = session().data().peerLoaded(peerId)
	; peer && peer->isUser()) {
	peer->asUser()->setNoForwardFlags(

// GOOD - look up the specific subtype directly:
const auto userId = peerToUser(peerId);
if (const auto user = session().data().userLoaded(userId)) {
	user->setNoForwardFlags(
```

Avoid C++17 `if` with initializer (`;` inside the condition) when the code can be written more clearly with simple nested `if` statements or by extracting the value beforehand:

```cpp
// BAD - complex if-with-initializer:
if (const auto peer = session().data().peerLoaded(peerId)
	; peer && peer->isUser()) {

// GOOD - simple nested ifs when direct lookup isn't available:
if (const auto peer = session().data().peerLoaded(peerId)) {
	if (const auto user = peer->asUser()) {

## Always initialize variables of basic types

Never leave variables of basic types (`int`, `float`, `bool`, pointers, etc.) uninitialized. Custom types with constructors are fine — they initialize themselves. But for any basic type, always provide a default value (`= 0`, `= false`, `= nullptr`, etc.). This applies especially to class fields, where uninitialized members are a persistent source of bugs.

The only exception is performance-critical hot paths where you can prove no read-from-uninitialized-memory occurs. For class fields there is no such exception — always initialize.

```cpp
// BAD:
int _bulletLeft;
int _bulletTop;
bool _expanded;
SomeType *_pointer;

// GOOD:
int _bulletLeft = 0;
int _bulletTop = 0;
bool _expanded = false;
SomeType *_pointer = nullptr;
```

## Prefer tr:: projections over Ui::Text:: in localization calls

Inside `tr::lng_...()` calls, always use the `tr::` projection helpers instead of their `Ui::Text::` equivalents. The `tr::` helpers are shorter and work uniformly as both placeholder wrappers and final projectors.

| Instead of | Use |
|---|---|
| `Ui::Text::Bold(x)` | `tr::bold(x)` |
| `Ui::Text::Italic(x)` | `tr::italic(x)` |
| `Ui::Text::RichLangValue` | `tr::rich` |
| `Ui::Text::WithEntities` | `tr::marked` |

```cpp
// BAD - verbose Ui::Text:: functions:
tr::lng_some_key(
    tr::now,
    lt_name,
    Ui::Text::Bold(name),
    lt_group,
    Ui::Text::Bold(group),
    Ui::Text::RichLangValue)

// GOOD - concise tr:: helpers:
tr::lng_some_key(
    tr::now,
    lt_name,
    tr::bold(name),
    lt_group,
    tr::bold(group),
    tr::rich)
```

## Multi-line calls — one argument per line

When a function call doesn't fit on one line, put each argument on its own line. Don't group "logical pairs" on the same line — it creates inconsistent line lengths and makes diffs noisier.

```cpp
// BAD - pairs of arguments sharing lines:
tr::lng_some_key(
    tr::now,
    lt_name, tr::bold(name),
    lt_group, tr::bold(group),
    tr::rich)

// GOOD - one argument per line:
tr::lng_some_key(
    tr::now,
    lt_name,
    tr::bold(name),
    lt_group,
    tr::bold(group),
    tr::rich)

// Single-line is fine when everything fits:
auto text = tr::lng_settings_title(tr::now);
```

## std::optional access — avoid value()

Do not call `std::optional::value()` because it throws an exception that is not available on older macOS targets. Use `has_value()`, `value_or()`, `operator bool()`, or `operator*` instead.
```
