#ifndef BTN_FIXED_H
#define BTN_FIXED_H

#include "btn_assert.h"
#include "btn_limits.h"
#include "btn_functional.h"

namespace btn
{

template<int Precision>
class fixed_t
{
    static_assert(Precision > 0 && Precision < 31, "Invalid precision");

public:
    [[nodiscard]] static constexpr fixed_t create(int value)
    {
        return fixed_t(value, false);
    }

    [[nodiscard]] static constexpr int precision()
    {
        return Precision;
    }

    [[nodiscard]] static constexpr int scale()
    {
        return 1 << Precision;
    }

    constexpr fixed_t() = default;

    constexpr fixed_t(int integer) :
        _value(integer * scale())
    {
    }

    constexpr fixed_t(int integer, int fraction) :
        _value((integer * scale()) + fraction)
    {
        BTN_CONSTEXPR_ASSERT(fraction >= 0, "Fraction is negative");
    }

    constexpr fixed_t(float value) :
        _value(int(value * scale()))
    {
    }

    constexpr fixed_t(double value) :
        _value(int(value * scale()))
    {
    }

    template<int OtherPrecision>
    constexpr fixed_t(fixed_t<OtherPrecision> other) :
        _value(Precision < OtherPrecision ?
                   other.value() / (other.scale() / scale()) :
                   other.value() * (scale() - other.scale()))
    {
    }

    [[nodiscard]] constexpr int value() const
    {
        return _value;
    }

    [[nodiscard]] constexpr int integer() const
    {
        return _value / scale();
    }

    [[nodiscard]] constexpr unsigned unsigned_integer() const
    {
        return _value >> Precision;
    }

    [[nodiscard]] constexpr int fraction() const
    {
        return _value & (scale() - 1);
    }

    [[nodiscard]] constexpr float to_float() const
    {
        return float(_value) / scale();
    }

    [[nodiscard]] constexpr double to_double() const
    {
        return double(_value) / scale();
    }

    [[nodiscard]] constexpr fixed_t multiplication(fixed_t other) const
    {
        return fixed_t::create(int((int64_t(_value) * other._value) >> Precision));
    }

    [[nodiscard]] constexpr fixed_t multiplication(int integer) const
    {
        return fixed_t::create(_value * integer);
    }

    [[nodiscard]] constexpr fixed_t unsafe_multiplication(fixed_t other) const
    {
        return fixed_t::create((_value * other._value) >> Precision);
    }

    [[nodiscard]] constexpr fixed_t unsafe_multiplication(int integer) const
    {
        return multiplication(integer);
    }

    [[nodiscard]] constexpr fixed_t reciprocal_division(int integer) const
    {
        BTN_CONSTEXPR_ASSERT(_value >= 0, "Internal value is not positive");
        BTN_CONSTEXPR_ASSERT(integer >= 0, "Integer is not greater than 0");

        return *this * fixed_t::create((scale() + integer - 1) / integer);
    }

    [[nodiscard]] constexpr fixed_t division(fixed_t other) const
    {
        BTN_CONSTEXPR_ASSERT(other._value, "Other's internal value is zero");

        return fixed_t::create(int((int64_t(_value) * scale()) / other._value));
    }

    [[nodiscard]] constexpr fixed_t division(int integer) const
    {
        BTN_CONSTEXPR_ASSERT(integer, "Integer is zero");

        return fixed_t::create(_value / integer);
    }

    [[nodiscard]] constexpr fixed_t unsafe_division(fixed_t other) const
    {
        BTN_CONSTEXPR_ASSERT(other._value, "Other's internal value is zero");

        return fixed_t::create((_value * scale()) / other._value);
    }

    [[nodiscard]] constexpr fixed_t unsafe_division(int integer) const
    {
        BTN_CONSTEXPR_ASSERT(integer, "Integer is zero");

        return division(integer);
    }

    [[nodiscard]] constexpr fixed_t operator-() const
    {
        return fixed_t::create(-_value);
    }

    constexpr fixed_t& operator+=(fixed_t other)
    {
        _value += other._value;
        return *this;
    }

    constexpr fixed_t& operator-=(fixed_t other)
    {
        _value -= other._value;
        return *this;
    }

    constexpr fixed_t& operator*=(fixed_t other)
    {
        *this = multiplication(other);
        return *this;
    }

    constexpr fixed_t& operator*=(int integer)
    {
        *this = multiplication(integer);
        return *this;
    }

    constexpr fixed_t& operator/=(fixed_t other)
    {
        *this = division(other);
        return *this;
    }

    constexpr fixed_t& operator/=(int integer)
    {
        *this = division(integer);
        return *this;
    }

    [[nodiscard]] constexpr friend fixed_t operator+(fixed_t a, fixed_t b)
    {
        return fixed_t::create(a._value + b._value);
    }

    [[nodiscard]] constexpr friend fixed_t operator-(fixed_t a, fixed_t b)
    {
        return fixed_t::create(a._value - b._value);
    }

    [[nodiscard]] constexpr friend fixed_t operator*(fixed_t a, fixed_t b)
    {
        return a.multiplication(b);
    }

    [[nodiscard]] constexpr friend fixed_t operator*(fixed_t a, int b)
    {
        return a.multiplication(b);
    }

    [[nodiscard]] constexpr friend fixed_t operator/(fixed_t a, fixed_t b)
    {
        return a.division(b);
    }

    [[nodiscard]] constexpr friend fixed_t operator/(fixed_t a, int b)
    {
        return a.division(b);
    }

    [[nodiscard]] constexpr friend bool operator==(fixed_t a, fixed_t b)
    {
        return a._value == b._value;
    }

    [[nodiscard]] constexpr friend bool operator!=(fixed_t a, fixed_t b)
    {
        return a._value != b._value;
    }

    [[nodiscard]] constexpr friend bool operator<(fixed_t a, fixed_t b)
    {
        return a._value < b._value;
    }

    [[nodiscard]] constexpr friend bool operator<=(fixed_t a, fixed_t b)
    {
        return a._value <= b._value;
    }

    [[nodiscard]] constexpr friend bool operator>(fixed_t a, fixed_t b)
    {
        return a._value > b._value;
    }

    [[nodiscard]] constexpr friend bool operator>=(fixed_t a, fixed_t b)
    {
        return a._value >= b._value;
    }

private:
    int _value = 0;

    constexpr fixed_t(int value, bool) :
        _value(value)
    {
    }
};

using fixed = fixed_t<12>;

template<int Precision>
struct hash<fixed_t<Precision>>
{
    [[nodiscard]] constexpr unsigned operator()(fixed_t<Precision> fixed) const
    {
        return make_hash(fixed.value());
    }
};

}

#endif
