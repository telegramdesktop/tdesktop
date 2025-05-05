# Telegram Desktop API Usage

## API Schema

The API definitions are described using [TL Language](https://core.telegram.org/mtproto/TL) in two main schema files:

1.  **`Telegram/SourceFiles/mtproto/scheme/mtproto.tl`**
    *   Defines the core MTProto protocol types and methods used for basic communication, encryption, authorization, service messages, etc.
    *   Some fundamental types and methods from this schema (like basic types, RPC calls, containers) are often implemented directly in the C++ MTProto core (`SourceFiles/mtproto/`) and may be skipped during the C++ code generation phase.
    *   Other parts of `mtproto.tl` might still be processed by the code generator.

2.  **`Telegram/SourceFiles/mtproto/scheme/api.tl`**
    *   Defines the higher-level Telegram API layer, including all the methods and types related to chat functionality, user profiles, messages, channels, stickers, etc.
    *   This is the primary schema used when making functional API requests within the application.

Both files use the same TL syntax to describe API methods (functions) and types (constructors).

## Code Generation

A custom code generation tool processes `api.tl` (and parts of `mtproto.tl`) to create corresponding C++ classes and types. These generated headers are typically included via the Precompiled Header (PCH) for the main `Telegram` project.

Generated types often follow the pattern `MTP[Type]` (e.g., `MTPUser`, `MTPMessage`) and methods correspond to functions within the `MTP` namespace or related classes (e.g., `MTPmessages_SendMessage`).

## Making API Requests

API requests are made using a standard pattern involving the `api()` object (providing access to the `MTP::Instance`), the generated `MTP...` request object, callback handlers for success (`.done()`) and failure (`.fail()`), and the `.send()` method.

Here's the general structure:

```cpp
// Include necessary headers if not already in PCH

// Obtain the API instance (usually via api() or MTP::Instance::Get())
api().request(MTPnamespace_MethodName(
    // Constructor arguments based on the api.tl definition for the method
    MTP_flags(flags_value),      // Use MTP_flags if the method has flags
    MTP_inputPeer(peer),         // Use MTP_... types for parameters
    MTP_string(messageText),
    MTP_long(randomId),
    // ... other arguments matching the TL definition
    MTP_vector<MTPMessageEntity>() // Example for a vector argument
)).done([=](const MTPResponseType &result) {
    // Handle the successful response (result).
    // 'result' will be of the C++ type corresponding to the TL type
    // specified after the '=' in the api.tl method definition.
    // How to access data depends on whether the TL type has one or multiple constructors:

    // 1. Multiple Constructors (e.g., User = User | UserEmpty):
    //    Use .match() with lambdas for each constructor:
    result.match([&](const MTPDuser &data) {
        /* use data.vfirst_name().v, etc. */
    }, [&](const MTPDuserEmpty &data) {
        /* handle empty user */
    });

    //    Alternatively, check the type explicitly and use the constructor getter:
    if (result.type() == mtpc_user) {
        const auto &data = result.c_user(); // Asserts if type is not mtpc_user!
        // use data.vfirst_name().v
    } else if (result.type() == mtpc_userEmpty) {
        const auto &data = result.c_userEmpty();
        // handle empty user
    }

    // 2. Single Constructor (e.g., Messages = messages { msgs: vector<Message> }):
    //    Use .match() with a single lambda:
    result.match([&](const MTPDmessages &data) { /* use data.vmessages().v */ });

    //    Or check the type explicitly and use the constructor getter:
    if (result.type() == mtpc_messages) {
        const auto &data = result.c_messages(); // Asserts if type is not mtpc_messages!
        // use data.vmessages().v
    }

    //    Or use the shortcut .data() for single-constructor types:
    const auto &data = result.data(); // Only works for single-constructor types!
    // use data.vmessages().v

}).fail([=](const MTP::Error &error) {
    // Handle the API error (error).
    // 'error' is an MTP::Error object containing the error code (error.type())
    // and description (error.description()). Check for specific error strings.
    if (error.type() == u"FLOOD_WAIT_X"_q) {
        // Handle flood wait
    } else {
        Ui::show(Box<InformBox>(Lang::Hard::ServerError())); // Example generic error handling
    }
}).handleFloodErrors().send(); // handleFloodErrors() is common, then send()
```

**Key Points:**

*   Always refer to `Telegram/SourceFiles/mtproto/scheme/api.tl` for the correct method names, parameters (names and types), and response types.
*   Use the generated `MTP...` types/classes for request parameters (e.g., `MTP_int`, `MTP_string`, `MTP_bool`, `MTP_vector`, `MTPInputUser`, etc.) and response handling.
*   The `.done()` lambda receives the specific C++ `MTP...` type corresponding to the TL return type.
    *   For types with **multiple constructors** (e.g., `User = User | UserEmpty`), use `result.match([&](const MTPDuser &d){ ... }, [&](const MTPDuserEmpty &d){ ... })` to handle each case, or check `result.type() == mtpc_user` / `mtpc_userEmpty` and call the specific `result.c_user()` / `result.c_userEmpty()` getter (which asserts on type mismatch).
    *   For types with a **single constructor** (e.g., `Messages = messages{...}`), you can use `result.match([&](const MTPDmessages &d){ ... })` with one lambda, or check `type()` and call `c_messages()`, or use the shortcut `result.data()` to access the fields directly.
*   The `.fail()` lambda receives an `MTP::Error` object. Check `error.type()` against known error strings (often defined as constants or using `u"..."_q` literals).
*   Directly construct the `MTPnamespace_MethodName(...)` object inside `request()`.
*   Include `.handleFloodErrors()` before `.send()` for standard flood wait handling.