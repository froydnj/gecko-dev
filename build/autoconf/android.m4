dnl This Source Code Form is subject to the terms of the Mozilla Public
dnl License, v. 2.0. If a copy of the MPL was not distributed with this
dnl file, You can obtain one at http://mozilla.org/MPL/2.0/.

AC_DEFUN([MOZ_ANDROID_NDK],
[

MOZ_ARG_WITH_STRING(android-ndk,
[  --with-android-ndk=DIR
                          location where the Android NDK can be found],
    android_ndk=$withval)

MOZ_ARG_WITH_STRING(android-toolchain,
[  --with-android-toolchain=DIR
                          location of the Android toolchain],
    android_toolchain=$withval)

MOZ_ARG_WITH_STRING(android-gnu-compiler-version,
[  --with-android-gnu-compiler-version=VER
                          gnu compiler version to use],
    android_gnu_compiler_version=$withval)

MOZ_ARG_WITH_STRING(android-llvm-compiler-version,
[  --with-android-llvm-compiler-version=VER
                          llvm compiler version to use (optional)],
    android_llvm_compiler_version=$withval)

MOZ_ARG_WITH_STRING(android-cxx-stl,
[  --with-android-cxx-stl=VALUE
                          use the specified C++ STL (stlport, libstdc++, libc++)],
    android_cxx_stl=$withval,
    android_cxx_stl=mozstlport)

define([MIN_ANDROID_VERSION], [9])
android_version=MIN_ANDROID_VERSION

MOZ_ARG_WITH_STRING(android-version,
[  --with-android-version=VER
                          android platform version, default] MIN_ANDROID_VERSION,
    android_version=$withval)

if test $android_version -lt MIN_ANDROID_VERSION ; then
    AC_MSG_ERROR([--with-android-version must be at least MIN_ANDROID_VERSION.])
fi

case "$target" in
arm-*linux*-android*|*-linuxandroid*)
    android_tool_prefix="arm-linux-androideabi"
    ;;
i?86-*android*)
    android_tool_prefix="i686-linux-android"
    ;;
mipsel-*android*)
    android_tool_prefix="mipsel-linux-android"
    ;;
*)
    android_tool_prefix="$target_os"
    ;;
esac

case "$target" in
*-android*|*-linuxandroid*)
    if test -z "$android_ndk" ; then
        AC_MSG_ERROR([You must specify --with-android-ndk=/path/to/ndk when targeting Android.])
    fi

    if test -z "$android_toolchain" ; then
        AC_MSG_CHECKING([for android toolchain directory])

        kernel_name=`uname -s | tr "[[:upper:]]" "[[:lower:]]"`

        for version in $android_gnu_compiler_version 4.9 4.8 4.7; do
            case "$target_cpu" in
            arm)
                target_name=arm-linux-androideabi-$version
                ;;
            i?86)
                target_name=x86-$version
                ;;
            mipsel)
                target_name=mipsel-linux-android-$version
                ;;
            *)
                AC_MSG_ERROR([target cpu is not supported])
                ;;
            esac
            case "$host_cpu" in
            i*86)
                android_toolchain="$android_ndk"/toolchains/$target_name/prebuilt/$kernel_name-x86
                ;;
            x86_64)
                android_toolchain="$android_ndk"/toolchains/$target_name/prebuilt/$kernel_name-x86_64
                if ! test -d "$android_toolchain" ; then
                    android_toolchain="$android_ndk"/toolchains/$target_name/prebuilt/$kernel_name-x86
                fi
                ;;
            *)
                AC_MSG_ERROR([No known toolchain for your host cpu])
                ;;
            esac
            if test -d "$android_toolchain" ; then
                android_gnu_compiler_version=$version
                break
            elif test -n "$android_gnu_compiler_version" ; then
                AC_MSG_ERROR([not found. Your --with-android-gnu-compiler-version may be wrong.])
            fi
        done

        if test -z "$android_gnu_compiler_version" ; then
            AC_MSG_ERROR([not found. You have to specify --with-android-toolchain=/path/to/ndk/toolchain.])
        else
            AC_MSG_RESULT([$android_toolchain])
        fi
        NSPR_CONFIGURE_ARGS="$NSPR_CONFIGURE_ARGS --with-android-toolchain=$android_toolchain"

        # Only use LLVM if the user specifically requests it.
        if test -n "$android_llvm_compiler_version"; then
            AC_MSG_CHECKING([for android llvm toolchain directory])
            version="$android_llvm_compiler_version"
            case "$host_cpu" in
            i*86)
                android_llvm_toolchain="$android_ndk"/toolchains/llvm-"$version"/prebuilt/"$kernel_name"-x86
                ;;
            x86_64)
                android_llvm_toolchain="$android_ndk"/toolchains/llvm-"$version"/prebuilt/"$kernel_name"-x86_64
                echo testing llvm toolchain dir $android_llvm_toolchain
                if ! test -d "$android_llvm_toolchain" ; then
                    android_llvm_toolchain="$android_ndk"/toolchains/llvm-"$version"/prebuilt/"$kernel_name"-x86
                fi
                ;;
            *)
                AC_MSG_ERROR([No known llvm toolchain for your host cpu])
                ;;
            esac
            echo llvm toolchain dir $android_llvm_toolchain
            if ! test -d "$android_llvm_toolchain"; then
                AC_MSG_ERROR([not found.  Your --with-android-llvm-compiler-version may be wrong.])
            fi
        fi
    fi

    NSPR_CONFIGURE_ARGS="$NSPR_CONFIGURE_ARGS --with-android-version=$android_version"

    AC_MSG_CHECKING([for android platform directory])

    case "$target_cpu" in
    arm)
        target_name=arm
        ;;
    i?86)
        target_name=x86
        ;;
    mipsel)
        target_name=mips
        ;;
    esac

    android_platform="$android_ndk"/platforms/android-"$android_version"/arch-"$target_name"

    if test -d "$android_platform" ; then
        AC_MSG_RESULT([$android_platform])
    else
        AC_MSG_ERROR([not found. Please check your NDK. With the current configuration, it should be in $android_platform])
    fi

    dnl set up compilers
    TOOLCHAIN_PREFIX="$android_toolchain/bin/$android_tool_prefix-"
    AS="$android_toolchain"/bin/"$android_tool_prefix"-as
    if test -z "$CC"; then
        if test -n "$android_llvm_toolchain"; then
            CC="$android_llvm_toolchain/bin/clang --target=armv5te-none-linux-androideabi -gcc-toolchain $android_toolchain"
        else
            CC="$android_toolchain"/bin/"$android_tool_prefix"-gcc
        fi
    fi
    if test -z "$CXX"; then
        if test -n "$android_llvm_toolchain"; then
            CXX="$android_llvm_toolchain/bin/clang++ --target=armv5te-none-linux-androideabi -gcc-toolchain $android_toolchain"
        else
            CXX="$android_toolchain"/bin/"$android_tool_prefix"-g++
        fi
    fi
    if test -z "$CPP"; then
        if test -n "$android_llvm_toolchain"; then
            CPP="$android_llvm_toolchain/bin/clang --target=armv5te-none-linux-androideabi -gcc-toolchain $android_toolchain -E"
        else
            CPP="$android_toolchain"/bin/"$android_tool_prefix"-cpp
        fi
    fi
    LD="$android_toolchain"/bin/"$android_tool_prefix"-ld
    AR="$android_toolchain"/bin/"$android_tool_prefix"-ar
    RANLIB="$android_toolchain"/bin/"$android_tool_prefix"-ranlib
    STRIP="$android_toolchain"/bin/"$android_tool_prefix"-strip
    OBJCOPY="$android_toolchain"/bin/"$android_tool_prefix"-objcopy

    # -idirafter does not work correctly with llvm; the default #include search
    # paths actually include host paths outside of the NDK (!), so using -idirafter
    # insert the android platform's /usr/include after the host's /usr/include,
    # which obviously doesn't work that well.  Using -I appears to put things in
    # the correct place.
    if test -z "$android_llvm_toolchain"; then
        CPPFLAGS="-idirafter $android_platform/usr/include $CPPFLAGS"
    else
        CPPFLAGS="-isystem $android_platform/usr/include $CPPFLAGS"
    fi
    CFLAGS="-fno-short-enums -fno-exceptions $CFLAGS"
    if test -z "$android_llvm_toolchain"; then
        CFLAGS="-mandroid $CFLAGS"
    fi
    CXXFLAGS="-fno-short-enums -fno-exceptions $CXXFLAGS"
    if test -z "$android_llvm_toolchain"; then
        CXXFLAGS="-mandroid -Wno-psabi $CXXFLAGS"
    fi
    ASFLAGS="-idirafter $android_platform/usr/include -DANDROID $ASFLAGS"

    dnl Add -llog by default, since we use it all over the place.
    dnl Add --allow-shlib-undefined, because libGLESv2 links to an
    dnl undefined symbol (present on the hardware, just not in the
    dnl NDK.)
    LDFLAGS="-L$android_platform/usr/lib -Wl,-rpath-link=$android_platform/usr/lib --sysroot=$android_platform -llog -Wl,--allow-shlib-undefined $LDFLAGS"
    if test -z "$android_llvm_toolchain"; then
        LDFLAGS="-mandroid $LDFLAGS"
    fi
    dnl prevent cross compile section from using these flags as host flags
    if test -z "$HOST_CPPFLAGS" ; then
        HOST_CPPFLAGS=" "
    fi
    if test -z "$HOST_CFLAGS" ; then
        HOST_CFLAGS=" "
    fi
    if test -z "$HOST_CXXFLAGS" ; then
        HOST_CXXFLAGS=" "
    fi
    if test -z "$HOST_LDFLAGS" ; then
        HOST_LDFLAGS=" "
    fi

    ANDROID_NDK="${android_ndk}"
    ANDROID_TOOLCHAIN="${android_toolchain}"
    ANDROID_PLATFORM="${android_platform}"

    AC_DEFINE(ANDROID)
    AC_SUBST(ANDROID_NDK)
    AC_SUBST(ANDROID_TOOLCHAIN)
    AC_SUBST(ANDROID_PLATFORM)

    ;;
esac

])

AC_DEFUN([MOZ_ANDROID_CPU_ARCH],
[

if test "$OS_TARGET" = "Android" -a -z "$gonkdir"; then
    case "${CPU_ARCH}-${MOZ_ARCH}" in
    arm-armv7*)
        ANDROID_CPU_ARCH=armeabi-v7a
        ;;
    arm-*)
        ANDROID_CPU_ARCH=armeabi
        ;;
    x86-*)
        ANDROID_CPU_ARCH=x86
        ;;
    mips-*) # When target_cpu is mipsel, CPU_ARCH is mips
        ANDROID_CPU_ARCH=mips
        ;;
    esac

    AC_SUBST(ANDROID_CPU_ARCH)
fi
])

AC_DEFUN([MOZ_ANDROID_STLPORT],
[

if test "$OS_TARGET" = "Android" -a -z "$gonkdir"; then
    cpu_arch_dir="$ANDROID_CPU_ARCH"
    if test "$MOZ_THUMB2" = 1; then
        cpu_arch_dir="$cpu_arch_dir/thumb"
    fi

    if test -z "$STLPORT_CPPFLAGS$STLPORT_LIBS"; then
        case "$android_cxx_stl" in
        libstdc++)
            # android-ndk-r8b and later
            ndk_base="$android_ndk/sources/cxx-stl/gnu-libstdc++/$android_gnu_compiler_version"
            ndk_libs_include="$ndk_base/libs/$ANDROID_CPU_ARCH"
            ndk_libs="$ndk_base/libs/$cpu_arch_dir"
            ndk_include="$ndk_base/include"

            if ! test -e "$ndk_libs/libgnustl_static.a"; then
                AC_MSG_ERROR([Couldn't find path to gnu-libstdc++ in the android ndk])
            fi

            STLPORT_LIBS="-L$ndk_libs -lgnustl_static"
            STLPORT_CPPFLAGS="-I$ndk_include -I$ndk_include/backward -I$ndk_libs_include/include"
            ;;
        libc++)
            # android-ndk-r8b and later
            ndk_base="$android_ndk/sources/cxx-stl"
            cxx_base="$ndk_base/llvm-libc++"
            cxx_libs="$cxx_base/libs/$cpu_arch_dir"
            cxx_include="$cxx_base/libcxx/include"
            cxxabi_base="$ndk_base/llvm-libc++abi"
            cxxabi_include="$cxxabi_base/libcxxabi/include"

            if ! test -e "$cxx_libs/libc++_static.a"; then
                AC_MSG_ERROR([Couldn't find path to llvm-libc++ in the android ndk])
            fi

            STLPORT_LIBS="-L$cxx_libs -lc++_static"
            # Add android/support/include/ for prototyping long double math
            # functions, locale-specific C library functions, multibyte support,
            # etc.
            STLPORT_CPPFLAGS="-I$android_ndk/sources/android/support/include -I$cxx_include -I$cxxabi_include"
            ;;
        mozstlport)
            # We don't need to set STLPORT_LIBS, because the build system will
            # take care of linking in our home-built stlport where it is needed.
            STLPORT_CPPFLAGS="-I $_topsrcdir/build/stlport/stlport -I $_topsrcdir/build/stlport/overrides -I $android_ndk/sources/cxx-stl/system/include"
            ;;
        *)
            AC_MSG_ERROR([Bad value for --enable-android-cxx-stl])
            ;;
        esac
    fi
    CXXFLAGS="$CXXFLAGS $STLPORT_CPPFLAGS"
fi
MOZ_ANDROID_CXX_STL=$android_cxx_stl
AC_SUBST([MOZ_ANDROID_CXX_STL])
AC_SUBST([STLPORT_LIBS])

])


AC_DEFUN([concat],[$1$2$3$4])

dnl Find a component of an AAR.
dnl Arg 1: variable name to expose, like ANDROID_SUPPORT_V4_LIB.
dnl Arg 2: path to component.
dnl Arg 3: if non-empty, expect and require component.
AC_DEFUN([MOZ_ANDROID_AAR_COMPONENT], [
  ifelse([$3], ,
  [
    if test -e "$$1" ; then
      AC_MSG_ERROR([Found unexpected exploded $1!])
    fi
  ],
  [
    AC_MSG_CHECKING([for $1])
    $1="$2"
    if ! test -e "$$1" ; then
      AC_MSG_ERROR([Could not find required exploded $1!])
    fi
    AC_MSG_RESULT([$$1])
    AC_SUBST($1)
  ])
])

dnl Find an AAR and expose variables representing its exploded components.
dnl AC_SUBSTs ANDROID_NAME_{AAR,AAR_RES,AAR_LIB,AAR_INTERNAL_LIB}.
dnl Arg 1: name, like play-services-base
dnl Arg 2: version, like 7.8.0
dnl Arg 3: extras subdirectory, either android or google
dnl Arg 4: package subdirectory, like com/google/android/gms
dnl Arg 5: if non-empty, expect and require internal_impl JAR.
dnl Arg 6: if non-empty, expect and require assets/ directory.
AC_DEFUN([MOZ_ANDROID_AAR],[
  define([local_aar_var_base], translit($1, [-a-z], [_A-Z]))
  define([local_aar_var], concat(ANDROID_, local_aar_var_base, _AAR))
  local_aar_var="$ANDROID_SDK_ROOT/extras/$3/m2repository/$4/$1/$2/$1-$2.aar"
  AC_MSG_CHECKING([for $1 AAR])
  if ! test -e "$local_aar_var" ; then
    AC_MSG_ERROR([You must download the $1 AAR.  Run the Android SDK tool and install the Android and Google Support Repositories under Extras.  See https://developer.android.com/tools/extras/support-library.html for more info. (Looked for $local_aar_var)])
  fi
  AC_SUBST(local_aar_var)
  AC_MSG_RESULT([$local_aar_var])

  if ! $PYTHON -m mozbuild.action.explode_aar --destdir=$MOZ_BUILD_ROOT/dist/exploded-aar $local_aar_var ; then
    AC_MSG_ERROR([Could not explode $local_aar_var!])
  fi

  define([root], $MOZ_BUILD_ROOT/dist/exploded-aar/$1-$2/)
  MOZ_ANDROID_AAR_COMPONENT(concat(local_aar_var, _LIB), concat(root, $1-$2-classes.jar), REQUIRED)
  MOZ_ANDROID_AAR_COMPONENT(concat(local_aar_var, _RES), concat(root, res), REQUIRED)
  MOZ_ANDROID_AAR_COMPONENT(concat(local_aar_var, _INTERNAL_LIB), concat(root, libs/$1-$2-internal_impl-$2.jar), $5)
  MOZ_ANDROID_AAR_COMPONENT(concat(local_aar_var, _ASSETS), concat(root, assets), $6)
])

AC_DEFUN([MOZ_ANDROID_GOOGLE_PLAY_SERVICES],
[

if test -n "$MOZ_NATIVE_DEVICES" ; then
    AC_SUBST(MOZ_NATIVE_DEVICES)

    MOZ_ANDROID_AAR(play-services-base, 8.1.0, google, com/google/android/gms)
    MOZ_ANDROID_AAR(play-services-basement, 8.1.0, google, com/google/android/gms)
    MOZ_ANDROID_AAR(play-services-cast, 8.1.0, google, com/google/android/gms)
    MOZ_ANDROID_AAR(mediarouter-v7, 23.0.1, android, com/android/support, REQUIRED_INTERNAL_IMPL)
fi

])

AC_DEFUN([MOZ_ANDROID_GOOGLE_CLOUD_MESSAGING],
[

if test -n "$MOZ_ANDROID_GCM" ; then
    AC_SUBST(MOZ_ANDROID_GCM)

    MOZ_ANDROID_AAR(play-services-base, 8.1.0, google, com/google/android/gms)
    MOZ_ANDROID_AAR(play-services-basement, 8.1.0, google, com/google/android/gms)
    MOZ_ANDROID_AAR(play-services-gcm, 8.1.0, google, com/google/android/gms)
fi

])

dnl Configure an Android SDK.
dnl Arg 1: target SDK version, like 22.
dnl Arg 2: build tools version, like 22.0.1.
AC_DEFUN([MOZ_ANDROID_SDK],
[

MOZ_ARG_WITH_STRING(android-sdk,
[  --with-android-sdk=DIR
                          location where the Android SDK can be found (like ~/.mozbuild/android-sdk-linux)],
    android_sdk_root=$withval)

android_sdk_root=${withval%/platforms/android-*}

case "$target" in
*-android*|*-linuxandroid*)
    if test -z "$android_sdk_root" ; then
        AC_MSG_ERROR([You must specify --with-android-sdk=/path/to/sdk when targeting Android.])
    fi

    # We were given an old-style
    # --with-android-sdk=/path/to/sdk/platforms/android-*.  We could warn, but
    # we'll get compliance by forcing the issue.
    if test -e "$withval"/source.properties ; then
        AC_MSG_ERROR([Including platforms/android-* in --with-android-sdk arguments is deprecated.  Use --with-android-sdk=$android_sdk_root.])
    fi

    android_target_sdk=$1
    AC_MSG_CHECKING([for Android SDK platform version $android_target_sdk])
    android_sdk=$android_sdk_root/platforms/android-$android_target_sdk
    if ! test -e "$android_sdk/source.properties" ; then
        AC_MSG_ERROR([You must download Android SDK platform version $android_target_sdk.  Try |mach bootstrap|.  (Looked for $android_sdk)])
    fi
    AC_MSG_RESULT([$android_sdk])

    android_build_tools="$android_sdk_root"/build-tools/$2
    AC_MSG_CHECKING([for Android build-tools version $2])
    if test -d "$android_build_tools" -a -f "$android_build_tools/aapt"; then
        AC_MSG_RESULT([$android_build_tools])
    else
        AC_MSG_ERROR([You must install the Android build-tools version $2.  Try |mach bootstrap|.  (Looked for $android_build_tools)])
    fi

    MOZ_PATH_PROG(ZIPALIGN, zipalign, :, [$android_build_tools])
    MOZ_PATH_PROG(DX, dx, :, [$android_build_tools])
    MOZ_PATH_PROG(AAPT, aapt, :, [$android_build_tools])
    MOZ_PATH_PROG(AIDL, aidl, :, [$android_build_tools])
    if test -z "$ZIPALIGN" -o "$ZIPALIGN" = ":"; then
      AC_MSG_ERROR([The program zipalign was not found.  Try |mach bootstrap|.])
    fi
    if test -z "$DX" -o "$DX" = ":"; then
      AC_MSG_ERROR([The program dx was not found.  Try |mach bootstrap|.])
    fi
    if test -z "$AAPT" -o "$AAPT" = ":"; then
      AC_MSG_ERROR([The program aapt was not found.  Try |mach bootstrap|.])
    fi
    if test -z "$AIDL" -o "$AIDL" = ":"; then
      AC_MSG_ERROR([The program aidl was not found.  Try |mach bootstrap|.])
    fi

    android_platform_tools="$android_sdk_root"/platform-tools
    AC_MSG_CHECKING([for Android platform-tools])
    if test -d "$android_platform_tools" -a -f "$android_platform_tools/adb"; then
        AC_MSG_RESULT([$android_platform_tools])
    else
        AC_MSG_ERROR([You must install the Android platform-tools.  Try |mach bootstrap|.  (Looked for $android_platform_tools)])
    fi

    MOZ_PATH_PROG(ADB, adb, :, [$android_platform_tools])
    if test -z "$ADB" -o "$ADB" = ":"; then
      AC_MSG_ERROR([The program adb was not found.  Try |mach bootstrap|.])
    fi

    android_tools="$android_sdk_root"/tools
    AC_MSG_CHECKING([for Android tools])
    if test -d "$android_tools" -a -f "$android_tools/emulator"; then
        AC_MSG_RESULT([$android_tools])
    else
        AC_MSG_ERROR([You must install the Android tools.  Try |mach bootstrap|.  (Looked for $android_tools)])
    fi

    MOZ_PATH_PROG(EMULATOR, emulator, :, [$android_tools])
    if test -z "$EMULATOR" -o "$EMULATOR" = ":"; then
      AC_MSG_ERROR([The program emulator was not found.  Try |mach bootstrap|.])
    fi

    ANDROID_TARGET_SDK="${android_target_sdk}"
    ANDROID_SDK="${android_sdk}"
    ANDROID_SDK_ROOT="${android_sdk_root}"
    ANDROID_TOOLS="${android_tools}"
    AC_DEFINE_UNQUOTED(ANDROID_TARGET_SDK,$ANDROID_TARGET_SDK)
    AC_SUBST(ANDROID_TARGET_SDK)
    AC_SUBST(ANDROID_SDK_ROOT)
    AC_SUBST(ANDROID_SDK)
    AC_SUBST(ANDROID_TOOLS)

    MOZ_ANDROID_AAR(appcompat-v7, 23.0.1, android, com/android/support)
    MOZ_ANDROID_AAR(design, 23.0.1, android, com/android/support)
    MOZ_ANDROID_AAR(recyclerview-v7, 23.0.1, android, com/android/support)
    MOZ_ANDROID_AAR(support-v4, 23.0.1, android, com/android/support, REQUIRED_INTERNAL_IMPL)

    ANDROID_SUPPORT_ANNOTATIONS_JAR="$ANDROID_SDK_ROOT/extras/android/m2repository/com/android/support/support-annotations/23.0.1/support-annotations-23.0.1.jar"
    AC_MSG_CHECKING([for support-annotations JAR])
    if ! test -e $ANDROID_SUPPORT_ANNOTATIONS_JAR ; then
        AC_MSG_ERROR([You must download the support-annotations lib.  Run the Android SDK tool and install the Android Support Repository under Extras.  See https://developer.android.com/tools/extras/support-library.html for more info. (looked for $ANDROID_SUPPORT_ANNOTATIONS_JAR)])
    fi
    AC_MSG_RESULT([$ANDROID_SUPPORT_ANNOTATIONS_JAR])
    AC_SUBST(ANDROID_SUPPORT_ANNOTATIONS_JAR)
    ANDROID_SUPPORT_ANNOTATIONS_JAR_LIB=$ANDROID_SUPPORT_ANNOTATIONS_JAR
    AC_SUBST(ANDROID_SUPPORT_ANNOTATIONS_JAR_LIB)
    ;;
esac

MOZ_ARG_WITH_STRING(android-min-sdk,
[  --with-android-min-sdk=[VER]     Impose a minimum Firefox for Android SDK version],
[ MOZ_ANDROID_MIN_SDK_VERSION=$withval ])

MOZ_ARG_WITH_STRING(android-max-sdk,
[  --with-android-max-sdk=[VER]     Impose a maximum Firefox for Android SDK version],
[ MOZ_ANDROID_MAX_SDK_VERSION=$withval ])

if test -n "$MOZ_ANDROID_MIN_SDK_VERSION"; then
    if test -n "$MOZ_ANDROID_MAX_SDK_VERSION"; then
        if test $MOZ_ANDROID_MAX_SDK_VERSION -lt $MOZ_ANDROID_MIN_SDK_VERSION ; then
            AC_MSG_ERROR([--with-android-max-sdk must be at least the value of --with-android-min-sdk.])
        fi
    fi

    if test $MOZ_ANDROID_MIN_SDK_VERSION -gt $ANDROID_TARGET_SDK ; then
        AC_MSG_ERROR([--with-android-min-sdk is expected to be less than $ANDROID_TARGET_SDK])
    fi

    AC_DEFINE_UNQUOTED(MOZ_ANDROID_MIN_SDK_VERSION, $MOZ_ANDROID_MIN_SDK_VERSION)
    AC_SUBST(MOZ_ANDROID_MIN_SDK_VERSION)
fi

if test -n "$MOZ_ANDROID_MAX_SDK_VERSION"; then
    AC_DEFINE_UNQUOTED(MOZ_ANDROID_MAX_SDK_VERSION, $MOZ_ANDROID_MAX_SDK_VERSION)
    AC_SUBST(MOZ_ANDROID_MAX_SDK_VERSION)
fi

])
