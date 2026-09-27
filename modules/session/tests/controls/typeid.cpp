// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// BUILD CONTROL: a typeid of a polymorphic object compiled with felitronics::session's compile options must not compile
// (-fno-rtti; /GR- with /we4541). Built clean (it must compile) and with FELITRONICS_SESSION_PLANT (it must fail with
// the compiler's RTTI diagnostic).

#if defined (FELITRONICS_SESSION_PLANT)
    #include <typeinfo>
#endif

struct FelitronicsSessionControlBase
{
    virtual ~FelitronicsSessionControlBase() = default;
    virtual int id() const { return 1; }
};

int felitronicsSessionControlTypeid (const FelitronicsSessionControlBase& b);

int felitronicsSessionControlTypeid (const FelitronicsSessionControlBase& b)
{
#if defined (FELITRONICS_SESSION_PLANT)
    return typeid (b) == typeid (FelitronicsSessionControlBase) ? 1 : 0;
#else
    return b.id();
#endif
}
