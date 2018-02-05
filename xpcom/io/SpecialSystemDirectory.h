/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _SPECIALSYSTEMDIRECTORY_H_
#define _SPECIALSYSTEMDIRECTORY_H_

#include "nscore.h"
#include "nsIFile.h"

#ifdef MOZ_WIDGET_COCOA
#include "nsILocalFileMac.h"
#include "prenv.h"
#endif

enum SystemDirectories {
  OS_XPCOM_SPECIFIC_START = 0,
  OS_DriveDirectory,
  OS_TemporaryDirectory,
  OS_CurrentProcessDirectory,
  OS_CurrentWorkingDirectory,
  XPCOM_CurrentProcessComponentDirectory,
  XPCOM_CurrentProcessComponentRegistry,

  MOZ_SPECIFIC_START = 100,
  Moz_BinDirectory,

  MAC_SPECIFIC_START = 200,
  Mac_SystemDirectory,
  Mac_TrashDirectory,
  Mac_StartupDirectory,
  Mac_ShutdownDirectory,
  Mac_AppleMenuDirectory,
  Mac_ControlPanelDirectory,
  Mac_ExtensionDirectory,
  Mac_FontsDirectory,
  Mac_ClassicPreferencesDirectory,
  Mac_DocumentsDirectory,
  Mac_InternetSearchDirectory,
  Mac_UserLibDirectory,
  Mac_HomeDirectory,
  Mac_DefaultDownloadDirectory,
  Mac_UserDesktopDirectory,
  Mac_LocalDesktopDirectory,
  Mac_UserApplicationsDirectory,
  Mac_LocalApplicationsDirectory,
  Mac_UserDocumentsDirectory,
  Mac_LocalDocumentsDirectory,
  Mac_UserInternetPluginDirectory,
  Mac_LocalInternetPluginDirectory,
  Mac_UserFrameworksDirectory,
  Mac_LocalFrameworksDirectory,
  Mac_UserPreferencesDirectory,
  Mac_LocalPreferencesDirectory,
  Mac_PictureDocumentsDirectory,
  Mac_MovieDocumentsDirectory,
  Mac_MusicDocumentsDirectory,
  Mac_InternetSitesDirectory,

  WIN_SPECIFIC_START = 300,
  Win_SystemDirectory,
  Win_WindowsDirectory,
  Win_HomeDirectory,
  Win_Desktop,
  Win_Programs,
  Win_Controls,
  Win_Printers,
  Win_Personal,
  Win_Favorites,
  Win_Startup,
  Win_Recent,
  Win_Sendto,
  Win_Bitbucket,
  Win_Startmenu,
  Win_Desktopdirectory,
  Win_Drives,
  Win_Network,
  Win_Nethood,
  Win_Fonts,
  Win_Templates,
  Win_Common_Startmenu,
  Win_Common_Programs,
  Win_Common_Startup,
  Win_Common_Desktopdirectory,
  Win_Appdata,
  Win_Printhood,
  Win_Cookies,
  Win_LocalAppdata,
  Win_ProgramFiles,
  Win_Downloads,
  Win_Common_AppData,
  Win_Documents,
  Win_Pictures,
  Win_Music,
  Win_Videos,
#if defined(MOZ_CONTENT_SANDBOX)
  Win_LocalAppdataLow,
#endif

  UNIX_SPECIFIC_START = 400,
  Unix_LocalDirectory,
  Unix_LibDirectory,
  Unix_HomeDirectory,
  Unix_XDG_Desktop,
  Unix_XDG_Documents,
  Unix_XDG_Download,
  Unix_XDG_Music,
  Unix_XDG_Pictures,
  Unix_XDG_PublicShare,
  Unix_XDG_Templates,
  Unix_XDG_Videos,
};

nsresult
GetSpecialSystemDirectory(SystemDirectories aSystemSystemDirectory,
                          nsIFile** aFile);
#ifdef MOZ_WIDGET_COCOA
nsresult
GetOSXFolderType(short aDomain, OSType aFolderType, nsIFile** aLocalFile);
#endif

#endif
