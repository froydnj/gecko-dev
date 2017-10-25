/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A mixin class for dumping information when it leaks.  */

#ifndef mozilla_DumpableLeakedThing_h
#define mozilla_DumpableLeakedThing_h

#include "mozilla/LinkedList.h"

#include <functional>

namespace mozilla {

// For classes that occasionally leak, and can also keep alive many other
// classes, it's useful to print out extra information beyond "N objects of
// this class leaked".  The classes below are an attempt to make this
// functionality generalizable and therefore easy to add to your class.

// To use:
//
// 1. Define some sort of controlling preprocessor macro, conditioned on
//    NS_BUILD_REFCNT_LOGGING.  Be sure to include nscore.h first:
//
//      #include "nscore.h"
//      #ifdef NS_BUILD_REFCNT_LOGGING
//      #define DUMP_INFO_ABOUT_MYCLASS
//      #endif
//
// 2. Conditionally have MyClass inherit from DumpableLeakedThing<MyClass>:
//
//      #ifdef DUMP_INFO_ABOUT_MYCLASS
//                  , public DumpableLeakedThing<MyClass>
//      #endif
//
// 3. Have a function that's guaranteed to be called before libxul shutdown.
//    This function can be defined anywhere you like, but it's easiest if
//    the function is a (static) method of MyClass:
//
//    void MyClass::DoSetupStuff() { ... }
//
// 4. Inside the aforementioned function, declare a DumpLeaked object.
//    DumpLeaked takes a function object that will receive a `const MyClass&`
//    argument, and you can print whatever you like about said object to
//    stdout.
//
//      static DumpLeaked<DumpableLeakedThing<MyClass>> d([](const My

// 1. Have YourClass inherit from DumpableLeakedThing<YourClass>.
// 2. Initialize a static object from inside a function that is guaranteed
//    to be called.  This initialization will take care of registering the
//    destructor to run at libxul unload time, and information about the
//    objects will be dumped then.
//
//    For instance, in a ShutdownGlobalObjects() function, declare:
//
//      static DumpLeaked<DumpableLeakedThing<YourClass>> d;
//
// 3. Ensure ShutdownGlobalObjects() gets called.
//
// The machinery here will take care of the rest.

template<typename T>
class DumpableLeakedThing;

template<typename T>
class DumpLeaked;

template<typename T>
class DumpLeaked<DumpableLeakedThing<T>>
{
private:
  std::function<void(const T&)> mDescribeFun;

public:
  DumpLeaked(std::function<void(const T&)> aDescribeFun)
    : mDescribeFun(aDescribeFun)
  {}

  ~DumpLeaked();

  static LinkedList<DumpableLeakedThing<T>> gAllObjects;
};

template<typename T>
class DumpableLeakedThing : public LinkedListElement<DumpableLeakedThing<T>>
{
public:
  DumpableLeakedThing(bool aEnable);
  // Default destructor will take care of removing `this` from `gAllObjects`.
};

// Implement DumpLeaked now that we can see the definition of DumpableLeakedThing.
template<typename T>
DumpLeaked<DumpableLeakedThing<T>>::~DumpLeaked()
{
  for (auto* obj : gAllObjects) {
    mDescribeFun(*static_cast<T*>(obj));
  }
  gAllObjects.clear();
}

template<typename T>
LinkedList<DumpableLeakedThing<T>> DumpLeaked<DumpableLeakedThing<T>>::gAllObjects;

// Likewise, now that we can see gAllObjects, we can implement the interesting
// bits of DumpableLeakedThing.
template<typename T>
DumpableLeakedThing<T>::DumpableLeakedThing(bool aEnable)
{
  if (aEnable) {
    // FIXME: add main-thread assertions here.
    DumpLeaked<DumpableLeakedThing>::gAllObjects.insertBack(this);
  }
}

} // namespace mozilla

#endif // mozilla_DumpableLeakedThing_h
