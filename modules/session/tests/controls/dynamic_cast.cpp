// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// BUILD CONTROL: a dynamic_cast down a polymorphic hierarchy compiled with felitronics::session's compile options must
// not compile (-fno-rtti; /GR- with /we4541). Built clean (it must compile) and with FELITRONICS_SESSION_PLANT (it must
// fail with the compiler's RTTI diagnostic).

struct FelitronicsSessionControlShape
{
    virtual ~FelitronicsSessionControlShape() = default;
    virtual int sides() const { return 0; }
};

struct FelitronicsSessionControlSquare final : FelitronicsSessionControlShape
{
    int sides() const override { return 4; }
};

int felitronicsSessionControlDynamicCast (const FelitronicsSessionControlShape* s);

int felitronicsSessionControlDynamicCast (const FelitronicsSessionControlShape* s)
{
#if defined (FELITRONICS_SESSION_PLANT)
    return dynamic_cast<const FelitronicsSessionControlSquare*> (s) != nullptr ? 1 : 0;
#else
    return s->sides();
#endif
}
