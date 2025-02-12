/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluedroid_BluetoothDaemonSocketInterface_h
#define mozilla_dom_bluetooth_bluedroid_BluetoothDaemonSocketInterface_h

#include "BluetoothDaemonHelpers.h"
#include "BluetoothInterface.h"
#include "mozilla/ipc/DaemonRunnables.h"

BEGIN_BLUETOOTH_NAMESPACE

using mozilla::ipc::DaemonSocketPDU;
using mozilla::ipc::DaemonSocketPDUHeader;
using mozilla::ipc::DaemonSocketResultHandler;

class BluetoothDaemonSocketModule
{
public:
  enum {
    SERVICE_ID = 0x02
  };

  enum {
    OPCODE_ERROR = 0x00,
    OPCODE_LISTEN = 0x01,
    OPCODE_CONNECT = 0x02
  };

  static const int MAX_NUM_CLIENTS;

  virtual nsresult Send(DaemonSocketPDU* aPDU,
                        DaemonSocketResultHandler* aRes) = 0;

  // Commands
  //

  nsresult ListenCmd(BluetoothSocketType aType,
                     const BluetoothServiceName& aServiceName,
                     const BluetoothUuid& aServiceUuid,
                     int aChannel, bool aEncrypt, bool aAuth,
                     BluetoothSocketResultHandler* aRes);

  nsresult ConnectCmd(const BluetoothAddress& aBdAddr,
                      BluetoothSocketType aType,
                      const BluetoothUuid& aServiceUuid,
                      int aChannel, bool aEncrypt, bool aAuth,
                      BluetoothSocketResultHandler* aRes);

  nsresult AcceptCmd(int aFd, BluetoothSocketResultHandler* aRes);

  nsresult CloseCmd(BluetoothSocketResultHandler* aRes);

protected:

  void HandleSvc(const DaemonSocketPDUHeader& aHeader,
                 DaemonSocketPDU& aPDU, DaemonSocketResultHandler* aRes);

private:
  class AcceptWatcher;
  class ConnectWatcher;
  class ListenInitOp;

  uint8_t SocketFlags(bool aEncrypt, bool aAuth);

  // Responses
  //

  typedef mozilla::ipc::DaemonResultRunnable3<
    BluetoothSocketResultHandler, void, int, BluetoothAddress, int, int,
    const BluetoothAddress&, int>
    AcceptResultRunnable;

  typedef mozilla::ipc::DaemonResultRunnable3<
    BluetoothSocketResultHandler, void, int, BluetoothAddress, int, int,
    const BluetoothAddress&, int>
    ConnectResultRunnable;

  typedef mozilla::ipc::DaemonResultRunnable1<
    BluetoothSocketResultHandler, void, BluetoothStatus, BluetoothStatus>
    ErrorRunnable;

  typedef mozilla::ipc::DaemonResultRunnable1<
    BluetoothSocketResultHandler, void, int, int>
    ListenResultRunnable;

  typedef mozilla::ipc::DaemonResultRunnable0<
    BluetoothSocketResultHandler, void>
    ResultRunnable;

  void ErrorRsp(const DaemonSocketPDUHeader& aHeader,
                DaemonSocketPDU& aPDU,
                BluetoothSocketResultHandler* aRes);

  void ListenRsp(const DaemonSocketPDUHeader& aHeader,
                 DaemonSocketPDU& aPDU,
                 BluetoothSocketResultHandler* aRes);

  void ConnectRsp(const DaemonSocketPDUHeader& aHeader,
                  DaemonSocketPDU& aPDU,
                  BluetoothSocketResultHandler* aRes);

  template<typename... PacketArgs>
  nsresult
  PackAndSend(uint8_t aOpcode, uint16_t aPayloadSize,
              BluetoothSocketResultHandler* aRes,
              PacketArgs... aArgs)
  {
    nsAutoPtr<DaemonSocketPDU> pdu =
      new DaemonSocketPDU(SERVICE_ID, aOpcode, aPayloadSize);

    nsresult rv = PackPDU(aArgs..., *pdu);
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = Send(pdu, aRes);
    if (NS_FAILED(rv)) {
      return rv;
    }
    Unused << pdu.forget();
    return rv;
  }
};

class BluetoothDaemonSocketInterface final
  : public BluetoothSocketInterface
{
public:
  BluetoothDaemonSocketInterface(BluetoothDaemonSocketModule* aModule);
  ~BluetoothDaemonSocketInterface();

  void Listen(BluetoothSocketType aType,
              const BluetoothServiceName& aServiceName,
              const BluetoothUuid& aServiceUuid,
              int aChannel, bool aEncrypt, bool aAuth,
              BluetoothSocketResultHandler* aRes) override;

  void Connect(const BluetoothAddress& aBdAddr,
               BluetoothSocketType aType,
               const BluetoothUuid& aServiceUuid,
               int aChannel, bool aEncrypt, bool aAuth,
               BluetoothSocketResultHandler* aRes) override;

  void Accept(int aFd, BluetoothSocketResultHandler* aRes) override;

  void Close(BluetoothSocketResultHandler* aRes) override;

private:
  void DispatchError(BluetoothSocketResultHandler* aRes,
                     BluetoothStatus aStatus);
  void DispatchError(BluetoothSocketResultHandler* aRes, nsresult aRv);

  BluetoothDaemonSocketModule* mModule;
};

END_BLUETOOTH_NAMESPACE

#endif // mozilla_dom_bluetooth_bluedroid_BluetoothDaemonSocketInterface_h
