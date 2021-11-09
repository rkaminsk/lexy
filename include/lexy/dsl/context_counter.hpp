// Copyright (C) 2020-2021 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef LEXY_DSL_CONTEXT_COUNTER_HPP_INCLUDED
#define LEXY_DSL_CONTEXT_COUNTER_HPP_INCLUDED

#include <lexy/_detail/iterator.hpp>
#include <lexy/action/base.hpp>
#include <lexy/dsl/base.hpp>
#include <lexy/dsl/error.hpp>

namespace lexy
{
struct unequal_counts
{
    static LEXY_CONSTEVAL auto name()
    {
        return "unequal counts";
    }
};
} // namespace lexy

namespace lexyd
{
template <typename Id>
using _ctx_counter = lexy::_detail::parse_context_var<Id, int>;

template <typename Id, int InitialValue>
struct _ctx_ccreate : rule_base
{
    template <typename NextParser>
    struct p
    {
        template <typename Context, typename Reader, typename... Args>
        LEXY_PARSER_FUNC static bool parse(Context& context, Reader& reader, Args&&... args)
        {
            _ctx_counter<Id> var(InitialValue);
            var.link(context);
            auto result = NextParser::parse(context, reader, LEXY_FWD(args)...);
            var.unlink(context);
            return result;
        }
    };
};

template <typename Id, int Delta>
struct _ctx_cadd : rule_base
{
    template <typename NextParser>
    struct p
    {
        template <typename Context, typename Reader, typename... Args>
        LEXY_PARSER_FUNC static bool parse(Context& context, Reader& reader, Args&&... args)
        {
            _ctx_counter<Id>::get(context) += Delta;
            return NextParser::parse(context, reader, LEXY_FWD(args)...);
        }
    };
};

template <typename Id, typename Rule, int Sign>
struct _ctx_cpush : _copy_base<Rule>
{
    template <typename NextParser>
    struct _pc
    {
        template <typename Context, typename Reader, typename... Args>
        LEXY_PARSER_FUNC static bool parse(Context& context, Reader& reader,
                                           typename Reader::iterator begin, Args&&... args)
        {
            auto end    = reader.position();
            auto length = lexy::_detail::range_size(begin, end);

            _ctx_counter<Id>::get(context) += int(length) * Sign;

            return NextParser::parse(context, reader, LEXY_FWD(args)...);
        }
    };

    template <typename Context, typename Reader>
    struct bp
    {
        lexy::branch_parser_for<Rule, Context, Reader> rule;

        constexpr auto try_parse(Context& context, const Reader& reader)
        {
            // Forward to the rule.
            return rule.try_parse(context, reader);
        }

        template <typename NextParser, typename... Args>
        LEXY_PARSER_FUNC auto finish(Context& context, Reader& reader, Args&&... args)
        {
            // Forward to the rule, but remember the current reader position.
            return rule.template finish<_pc<NextParser>>(context, reader, reader.position(),
                                                         LEXY_FWD(args)...);
        }
    };

    template <typename NextParser>
    struct p
    {
        template <typename Context, typename Reader, typename... Args>
        LEXY_PARSER_FUNC static bool parse(Context& context, Reader& reader, Args&&... args)
        {
            // Forward to the rule, but remember the current reader position.
            using parser = lexy::parser_for<Rule, _pc<NextParser>>;
            return parser::parse(context, reader, reader.position(), LEXY_FWD(args)...);
        }
    };
};

template <typename Id, int Value>
struct _ctx_cis : branch_base
{
    template <typename Context, typename Reader>
    struct bp
    {
        constexpr bool try_parse(Context& context, const Reader&)
        {
            return _ctx_counter<Id>::get(context) == Value;
        }

        template <typename NextParser, typename... Args>
        LEXY_PARSER_FUNC bool finish(Context& context, Reader& reader, Args&&... args)
        {
            return NextParser::parse(context, reader, LEXY_FWD(args)...);
        }
    };

    template <typename NextParser>
    using p = NextParser;
};

template <typename Id>
struct _ctx_cvalue : rule_base
{
    template <typename NextParser>
    struct p
    {
        template <typename Context, typename Reader, typename... Args>
        LEXY_PARSER_FUNC static bool parse(Context& context, Reader& reader, Args&&... args)
        {
            return NextParser::parse(context, reader, LEXY_FWD(args)...,
                                     _ctx_counter<Id>::get(context));
        }
    };
};

template <typename... Ids>
struct _ctx_ceq;
template <typename H, typename... T>
struct _ctx_ceq<H, T...> : branch_base
{
    template <typename Context, typename Reader>
    struct bp
    {
        constexpr bool try_parse(Context& context, const Reader&)
        {
            auto value = _ctx_counter<H>::get(context);
            return ((value == _ctx_counter<T>::get(context)) && ...);
        }

        template <typename NextParser, typename... Args>
        LEXY_PARSER_FUNC bool finish(Context& context, Reader& reader, Args&&... args)
        {
            return NextParser::parse(context, reader, LEXY_FWD(args)...);
        }
    };

    template <typename NextParser>
    struct p
    {
        template <typename Context, typename Reader, typename... Args>
        LEXY_PARSER_FUNC static bool parse(Context& context, Reader& reader, Args&&... args)
        {
            auto value = _ctx_counter<H>::get(context);
            if (((value != _ctx_counter<T>::get(context)) || ...))
            {
                auto err = lexy::error<Reader, lexy::unequal_counts>(reader.position());
                context.on(_ev::error{}, err);
                // Trivially recover.
            }

            return NextParser::parse(context, reader, LEXY_FWD(args)...);
        }
    };
};
} // namespace lexyd

namespace lexyd
{
template <typename Id>
struct _ctx_counter_dsl
{
    template <int InitialValue = 0>
    constexpr auto create() const
    {
        return _ctx_ccreate<Id, InitialValue>{};
    }

    constexpr auto inc() const
    {
        return _ctx_cadd<Id, +1>{};
    }
    constexpr auto dec() const
    {
        return _ctx_cadd<Id, -1>{};
    }

    template <typename Rule>
    constexpr auto push(Rule) const
    {
        return _ctx_cpush<Id, Rule, +1>{};
    }
    template <typename Rule>
    constexpr auto pop(Rule) const
    {
        return _ctx_cpush<Id, Rule, -1>{};
    }

    template <int Value>
    constexpr auto is() const
    {
        return _ctx_cis<Id, Value>{};
    }
    constexpr auto is_zero() const
    {
        return is<0>();
    }

    constexpr auto value() const
    {
        return _ctx_cvalue<Id>{};
    }
};

/// Declares an integer counter that is added to the parsing context.
template <typename Id>
constexpr auto context_counter = _ctx_counter_dsl<Id>{};

/// Takes a branch only if all counters are equal.
template <typename... Ids>
constexpr auto equal_counts(_ctx_counter_dsl<Ids>...)
{
    static_assert(sizeof...(Ids) > 1);
    return _ctx_ceq<Ids...>{};
}
} // namespace lexyd

#endif // LEXY_DSL_CONTEXT_COUNTER_HPP_INCLUDED

