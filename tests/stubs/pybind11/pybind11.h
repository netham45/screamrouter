#pragma once

#include <cstddef>

namespace pybind11 {

struct module_ {
    module_() = default;

    template <typename... Args>
    module_& def(const char*, Args&&...) { return *this; }
};

struct arg {
    explicit arg(const char*) {}
    template <typename T>
    arg& operator=(T&&) { return *this; }
};

template <typename... Args>
struct init {};

struct gil_scoped_release {
    gil_scoped_release() = default;
};

struct tuple {
    tuple() = default;
    std::size_t size() const { return 0; }
    tuple operator[](std::size_t) const { return tuple{}; }
    template <typename T>
    T cast() const { return T{}; }
};

template <typename... Args>
inline tuple make_tuple(Args&&...) { return tuple{}; }

template <typename... Options>
class class_ {
public:
    class_(module_&, const char*, const char* = nullptr) {}

    template <typename... Args>
    class_& def(const char*, Args&&...) { return *this; }

    template <typename... Args>
    class_& def(const init<Args...>&) { return *this; }

    template <typename... Args>
    class_& def_readwrite(const char*, Args&&...) { return *this; }

    template <typename... Args>
    class_& def_property(const char*, Args&&...) { return *this; }

    template <typename... Args>
    class_& def_property_readonly(const char*, Args&&...) { return *this; }

    template <typename... Args>
    class_& def_static(const char*, Args&&...) { return *this; }
};

template <typename... Options>
class enum_ {
public:
    enum_(module_&, const char*, const char* = nullptr) {}

    template <typename... Args>
    enum_& value(const char*, Args&&...) { return *this; }

    enum_& export_values() { return *this; }
};

} // namespace pybind11

namespace py = pybind11;

