/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A helper method for implementing move assignment robustly */

#ifndef mozilla_RobustOperatorEqualMove_h
#define mozilla_RobustOperatorEqualMove_h

#include "mozilla/Move.h"
#include "mozilla/OperatorNewExtensions.h"

namespace mozilla {

template<typename T>
T& RobustOperatorEqualMove(T* self, T&& from)
{
  self->~T();
  new (mozilla::KnownNotNull, self) T(Move(from));
  return *self;
}

} // namespace mozilla

// Convenience macro for writing out the entire operator= definition.
#define MOZ_DEFINE_MOVE_ASSIGNMENT(klassName) \
  klassName& operator=(klassName&& aOther)    \
  {                                           \
   return ::mozilla::RobustOperatorEqualMove(this, ::mozilla::Move(aOther)); \
  }

#endif // mozilla_RobustOperatorEqualMove_h
