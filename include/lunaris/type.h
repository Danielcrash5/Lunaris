#pragma once

#include <cstdint>

namespace lunaris {
enum class Type : uint8_t {
    none,          ///< No type.
    nil,           ///< Nil type.
    boolean,       ///< Boolean type.
    lightuserdata, ///< Light userdata type.
    number,        ///< Number type.
    string,        ///< String type.
    table,         ///< Table type.
    function,      ///< Function type.
    userdata,      ///< Userdata type.
    thread,        ///< Thread type.
    buffer,        ///< Buffer type.
};
}
