#pragma once

#include <cstddef>

// every hyprtavern protocol array may contain at most this many elements.
constexpr size_t MAX_ARRAY_ELEMENTS = 65535;

// bus object names, exposed protocol names, and property names and values, in bytes.
constexpr size_t MAX_OBJECT_NAME_LENGTH    = 256;
constexpr size_t MAX_PROTOCOL_NAME_LENGTH  = 255;
constexpr size_t MAX_PROPERTY_NAME_LENGTH  = 255;
constexpr size_t MAX_PROPERTY_VALUE_LENGTH = 4096;

// self-asserted display identities of permission contexts, in bytes.
constexpr size_t MAX_DISPLAY_NAME_LENGTH        = 256;
constexpr size_t MAX_DISPLAY_DESCRIPTION_LENGTH = 2048;
