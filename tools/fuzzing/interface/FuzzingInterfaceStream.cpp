/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Interface implementation for the unified fuzzing interface
 */

#include "FuzzingInterfaceStream.h"

#include "mozilla/Assertions.h"

#ifndef JS_STANDALONE
#include "nsNetUtil.h"
#endif

namespace mozilla {

#ifdef __AFL_COMPILER

void afl_interface_stream(const char* testFile, FuzzingTestFuncStream testFunc) {
    nsCOMPtr<nsIFile> file;
    nsresult rv = NS_GetSpecialDirectory(NS_OS_CURRENT_WORKING_DIR,
                                         getter_AddRefs(file));
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
    file->AppendNative(nsDependentCString(testFile));
    while(__AFL_LOOP(1000)) {
      nsCOMPtr<nsIInputStream> inputStream;
      rv = NS_NewLocalFileInputStream(getter_AddRefs(inputStream), file);
      MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
      if (!NS_InputStreamIsBuffered(inputStream)) {
        nsCOMPtr<nsIInputStream> bufStream;
        rv = NS_NewBufferedInputStream(getter_AddRefs(bufStream),
                                       inputStream.forget(), 1024);
        MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
        inputStream = bufStream;
      }
      testFunc(inputStream.forget());
    }
}

#endif

} // namespace mozilla
