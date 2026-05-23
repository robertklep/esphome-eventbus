#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

#include <iostream>
#include <charconv>
#include <string>
#include <unordered_map>
#include <any>
#include <type_traits>

namespace esphome::eventbus {

class AnyMap {

public:
  using Value = std::any;

private:
  std::unordered_map<std::string, Value> data;

public:
  class Proxy {
  private:
    std::unordered_map<std::string, Value>& data;
    std::string key;

    inline const Value* get_value() const {
      return data.contains(key) ? &data[key] : nullptr;
    }

  public:
    Proxy(std::string k, std::unordered_map<std::string, Value>& d) : key(std::move(k)), data(d) {}

    // Generic assignment
    template<typename T>
    Proxy& operator=(T v) {
      data[key] = v;
      return *this;
    }

    template <typename... Ts>
    bool cast_integral_to_ll(std::any& v) {
      return (([&] {
        if (auto ptr = std::any_cast<Ts>(&v)) {
          v = static_cast<long long>(*ptr);
          return true;
        }
        return false;
      }()) || ...);
    }

    Proxy& operator=(Value v) {
      // try casting any integral types to `long long`
      if (! cast_integral_to_ll<short, int, long, long long, unsigned short, unsigned int, unsigned long, unsigned long long >(v)) {
        // cast const char* to std::string for easier handling of "numerical strings"
        if (auto ptr = std::any_cast<const char*>(&v)) {
          v = std::string(*ptr);
        }
      }
      data[key] = v;
      return *this;
    }

    template<typename T>
    requires (std::is_integral_v<T> && ! std::is_same_v<T, bool>)
    Proxy& operator=(T v) {
      data[key] = static_cast<long long>(v);
      return *this;
    }

    template<typename T>
    requires (std::is_same_v<T, const char*>)
    Proxy& operator=(T v) {
      data[key] = std::string(v);
      return *this;
    }

    // Existence checking
    bool exists() const {
      return get_value() != nullptr;
    }

    explicit operator bool() const {
      return this->exists();
    }

    // Type checking
    template<typename T> bool is() const {
      auto v = get_value();
      if (!v) return false;
      if constexpr (std::is_same_v<T, const char*>) {
        return std::any_cast<std::string>(v) != nullptr;
      } else if constexpr (std::is_same_v<T, std::string>) {
        return std::any_cast<const char*>(v) != nullptr;
      } else {
        return std::any_cast<T>(v) != nullptr;
      }
    }

    // default values to return when no fallback is provided for non-existent key
    template<typename T> static constexpr T default_value() {
      if constexpr (std::is_same_v<T, const char*>) {
        return nullptr;
      } else if constexpr (std::is_same_v<T, std::string>) {
        return "";
      } else if constexpr (std::is_same_v<T, bool>) {
        return false;
      } else if constexpr (std::is_floating_point_v<T>) {
        return T(0);
      } else if constexpr (std::is_integral_v<T>) {
        return T(0);
      }
      return T();
    }

    // try parsing numerical values (int, float, etc)
    template<typename T>
    requires (std::is_arithmetic_v<T> && ! std::is_same_v<T, bool>)
    T parse_impl(const std::string& p, T fallback) const {
      T value{};

      const char* first = p.data();
      const char* last  = first + p.size();

      auto [ ptr, ec ] = std::from_chars(first, last, value);

      if (ec != std::errc{} || ptr != last) {
        return fallback;
      }

      return value;
    }

    // try parsing boolean values (true, false, etc)
    template<typename T>
    requires std::is_same_v<T, bool>
    T parse_impl(const std::string& p, T fallback) const {
      // string cases first
      if (! strncasecmp(p.c_str(), "true", 4)) {
        return true;
      } else if (! strncasecmp(p.c_str(), "false", 5)) {
        return false;
      }
      // try to parse as a number (everything non-zero equals true)
      return parse_impl<long long>(p, fallback) != 0;
    }

    // try to parse a string to the requested type
    template<typename T>
    T try_parse(auto v, T fallback) const {
      if (auto ptr = std::any_cast<std::string>(v)) {
        return parse_impl<T>(*ptr, fallback);
      }
      return fallback;
    }

    template<typename T> T as(T fallback = default_value<T>()) const {
      auto v = get_value();
      if (!v) return fallback;

      if (auto ptr = std::any_cast<T>(v)) {
        return *ptr;
      } else if constexpr (std::is_same_v<T, const char*>) {
        if (auto ptr = std::any_cast<std::string>(v)) {
          return ptr->c_str();
        }
        return fallback;
      } else if constexpr (std::is_same_v<T, std::string>) {
        if (auto ptr = std::any_cast<const char*>(v)) {
          return std::string(*ptr);
        } else if (auto ptr = std::any_cast<long long>(v)) {
          return std::to_string(*ptr);
        } else if (auto ptr = std::any_cast<float>(v)) {
          return std::to_string(*ptr);
        } else if (auto ptr = std::any_cast<double>(v)) {
          return std::to_string(*ptr);
        }
        return fallback;
      } else if constexpr (std::is_integral_v<T> && ! std::is_same_v<T, bool>) {
        if (auto ptr = std::any_cast<long long>(v)) {
          return static_cast<T>(*ptr);
        }
        return try_parse<T>(v, fallback);
      } else if constexpr (std::is_floating_point_v<T>) {
        if (auto ptr = std::any_cast<T>(v)) {
          return static_cast<T>(*ptr);
        }
        return try_parse<T>(v, fallback);
      } else if constexpr (std::is_same_v<T, bool>) {
        if (auto ptr = std::any_cast<bool>(v)) {
          return *ptr;
        }
        return try_parse<T>(v, fallback);
      }
      return fallback;
    }

    // operator|
    template<typename T>
    auto operator|(T&& fallback) const -> std::decay_t<T> {
      using R = std::decay_t<T>;
      return as<R>(std::forward<T>(fallback));
    }

    // implicit conversion
    template<typename T>
    operator T() const {
      return as<T>();
    }
  };

  Proxy operator[](const std::string& key) {
    return Proxy(key, data);
  }
};

}
