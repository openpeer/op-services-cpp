/*

 Copyright (c) 2014, Hookflash Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The views and conclusions contained in the software and documentation are those
 of the authors and should not be interpreted as representing official policies,
 either expressed or implied, of the FreeBSD Project.

 */

#pragma once

#include <openpeer/services/internal/types.h>
#include <openpeer/services/internal/services_Helper.h>

#include <openpeer/services/IBackgrounding.h>
#include <openpeer/services/ITURNSocket.h>
#include <openpeer/services/ISTUNRequester.h>
#include <openpeer/services/IDNS.h>
#include <openpeer/services/IWakeDelegate.h>

#include <zsLib/MessageQueueAssociator.h>
#include <zsLib/Socket.h>
#include <zsLib/Timer.h>

#define OPENPEER_SERVICES_TURN_MAX_CHANNEL_DATA_IN_BYTES ((1 << (sizeof(WORD)*8)) - 1)

#include <list>
#include <map>
#include <utility>

#define OPENPEER_SERVICES_SETTING_TURN_BACKGROUNDING_PHASE "openpeer/services/backgrounding-phase-turn"

#define OPENPEER_SERVICES_SETTING_FORCE_TURN_TO_USE_UDP "openpeer/services/debug/force-turn-to-use-udp"
#define OPENPEER_SERVICES_SETTING_FORCE_TURN_TO_USE_TCP "openpeer/services/debug/force-turn-to-use-tcp"
#define OPENPEER_SERVICES_SETTING_ONLY_ALLOW_TURN_TO_RELAY_DATA_TO_SPECIFIC_IPS "openpeer/services/debug/only-allow-turn-to-relay-data-sent-to-specific-ips"

namespace openpeer
{
  namespace services
  {
    namespace internal
    {
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark TURNSocket
      #pragma mark

      class TURNSocket : public Noop,
                         public MessageQueueAssociator,
                         public ITURNSocket,
                         public IWakeDelegate,
                         public ISTUNRequesterDelegate,
                         public IDNSDelegate,
                         public ISocketDelegate,
                         public ITimerDelegate,
                         public IBackgroundingDelegate
      {
      public:
        friend interaction ITURNSocket;
        friend interaction ITURNSocketFactory;

        typedef std::list<IPAddress> IPAddressList;
        typedef IDNS::SRVResultPtr SRVResultPtr;

        ZS_DECLARE_STRUCT_PTR(Server)
        ZS_DECLARE_STRUCT_PTR(Permission)
        ZS_DECLARE_STRUCT_PTR(ChannelInfo)

        typedef std::list<ServerPtr> ServerList;

        class CompareIP;

        typedef std::map<IPAddress, PermissionPtr, CompareIP> PermissionMap;

        typedef std::map<IPAddress, ChannelInfoPtr, CompareIP> ChannelIPMap;
        typedef std::map<WORD, ChannelInfoPtr> ChannelNumberMap;

        typedef Helper::IPAddressMap IPAddressMap;

      protected:

        TURNSocket(
                   IMessageQueuePtr queue,
                   ITURNSocketDelegatePtr delegate,
                   const char *turnServer,
                   const char *turnServerUsername,
                   const char *turnServerPassword,
                   bool useChannelBinding,
                   WORD limitChannelToRangeStart,
                   WORD limitChannelRoRangeEnd
                   );

        TURNSocket(
                   IMessageQueuePtr queue,
                   ITURNSocketDelegatePtr delegate,
                   IDNS::SRVResultPtr srvTURNUDP,
                   IDNS::SRVResultPtr srvTURNTCP,
                   const char *turnServerUsername,
                   const char *turnServerPassword,
                   bool useChannelBinding,
                   WORD limitChannelToRangeStart,
                   WORD limitChannelRoRangeEnd
                   );
        
        TURNSocket(Noop) : Noop(true), MessageQueueAssociator(IMessageQueuePtr()) {};

        void init();

      public:
        ~TURNSocket();

        static TURNSocketPtr convert(ITURNSocketPtr socket);

      protected:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => ITURNSocket
        #pragma mark

        static TURNSocketPtr create(
                                    IMessageQueuePtr queue,
                                    ITURNSocketDelegatePtr delegate,
                                    const char *turnServer,
                                    const char *turnServerUsername,
                                    const char *turnServerPassword,
                                    bool useChannelBinding = false,
                                    WORD limitChannelToRangeStart = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_START,
                                    WORD limitChannelRoRangeEnd = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_END
                                    );

        static TURNSocketPtr create(
                                    IMessageQueuePtr queue,
                                    ITURNSocketDelegatePtr delegate,
                                    IDNS::SRVResultPtr srvTURNUDP,
                                    IDNS::SRVResultPtr srvTURNTCP,
                                    const char *turnServerUsername,
                                    const char *turnServerPassword,
                                    bool useChannelBinding = false,
                                    WORD limitChannelToRangeStart = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_START,
                                    WORD limitChannelRoRangeEnd = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_END
                                    );

        static ElementPtr toDebug(ITURNSocketPtr socket);

        virtual PUID getID() const {return mID;}

        virtual TURNSocketStates getState() const;
        virtual TURNSocketErrors getLastError() const;

        virtual bool isRelayingUDP() const;

        virtual void shutdown();

        virtual bool sendPacket(
                                IPAddress destination,
                                const BYTE *buffer,
                                size_t bufferLengthInBytes,
                                bool bindChannelIfPossible = false
                                );

        virtual IPAddress getActiveServerIP() const;
        virtual IPAddress getRelayedIP() const;
        virtual IPAddress getReflectedIP() const;
        virtual IPAddress getServerResponseIP() const;

        virtual bool handleSTUNPacket(
                                      IPAddress fromIPAddress,
                                      STUNPacketPtr turnPacket
                                      );

        virtual bool handleChannelData(
                                       IPAddress fromIPAddress,
                                       const BYTE *buffer,
                                       size_t bufferLengthInBytes
                                       );

        virtual void notifyWriteReady();

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => IWakeDelegate
        #pragma mark

        virtual void onWake();

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => ISTUNRequesterDelegate
        #pragma mark

        virtual void onSTUNRequesterSendPacket(
                                               ISTUNRequesterPtr requester,
                                               IPAddress destination,
                                               SecureByteBlockPtr packet
                                               );

        virtual bool handleSTUNRequesterResponse(
                                                 ISTUNRequesterPtr requester,
                                                 IPAddress fromIPAddress,
                                                 STUNPacketPtr response
                                                 );

        virtual void onSTUNRequesterTimedOut(ISTUNRequesterPtr requester);

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => IDNSDelegate
        #pragma mark

        virtual void onLookupCompleted(IDNSQueryPtr query);

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => ISocketDelegate
        #pragma mark

        virtual void onReadReady(SocketPtr socket);
        virtual void onWriteReady(SocketPtr socket);
        virtual void onException(SocketPtr socket);

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => ITimer
        #pragma mark

        virtual void onTimer(TimerPtr timer);

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => IBackgroundingDelegate
        #pragma mark

        virtual void onBackgroundingGoingToBackground(
                                                      IBackgroundingSubscriptionPtr subscription,
                                                      IBackgroundingNotifierPtr notifier
                                                      );

        virtual void onBackgroundingGoingToBackgroundNow(IBackgroundingSubscriptionPtr subscription);

        virtual void onBackgroundingReturningFromBackground(IBackgroundingSubscriptionPtr subscription);

        virtual void onBackgroundingApplicationWillQuit(IBackgroundingSubscriptionPtr subscription);

      protected:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => (internal)
        #pragma mark

        bool isReady() const {return ITURNSocket::TURNSocketState_Ready ==  mCurrentState;}
        bool isShuttingDown() const {return ITURNSocket::TURNSocketState_ShuttingDown ==  mCurrentState;}
        bool isShutdown() const {return ITURNSocket::TURNSocketState_Shutdown ==  mCurrentState;}

        Log::Params log(const char *message) const;
        Log::Params debug(const char *message) const;
        virtual ElementPtr toDebug() const;

        void fix(STUNPacketPtr stun) const;

        IPAddress stepGetNextServer(
                                    IPAddressList &previouslyAdded,
                                    SRVResultPtr &srv
                                    );
        bool stepPrepareServers();
        void step();
        void cancel();

        void setState(TURNSocketStates newState);

        void consumeBuffer(
                           ServerPtr &server,
                           size_t bufferSizeInBytes
                           );

        bool handleAllocateRequester(
                                     ISTUNRequesterPtr requester,
                                     IPAddress fromIPAddress,
                                     STUNPacketPtr response
                                     );

        bool handleRefreshRequester(
                                    ISTUNRequesterPtr requester,
                                    STUNPacketPtr response
                                    );

        bool handleDeallocRequester(
                                    ISTUNRequesterPtr requester,
                                    STUNPacketPtr response
                                    );

        bool handlePermissionRequester(
                                       ISTUNRequesterPtr requester,
                                       STUNPacketPtr response
                                       );

        bool handleChannelRequester(
                                    ISTUNRequesterPtr requester,
                                    STUNPacketPtr response
                                    );

        void requestPermissionsNow();

        void refreshNow();

        void refreshChannels();

        bool sendPacketOrDopPacketIfBufferFull(
                                               ServerPtr server,
                                               const BYTE *buffer,
                                               size_t bufferSizeInBytes
                                               );

        bool sendPacketOverTCPOrDropIfBufferFull(
                                                 ServerPtr server,
                                                 const BYTE *buffer,
                                                 size_t bufferSizeInBytes
                                                 );

        void informWriteReady();

        WORD getNextChannelNumber();

        ISTUNRequesterPtr handleAuthorizationErrors(ISTUNRequesterPtr requester, STUNPacketPtr response);

        void clearBackgroundingNotifierIfPossible();
        void clearRefreshRequester()      {if (mRefreshRequester) { mRefreshRequester->cancel(); mRefreshRequester.reset(); } clearBackgroundingNotifierIfPossible();}
        void clearPermissionRequester()   {if (mPermissionRequester) { mPermissionRequester->cancel(); mPermissionRequester.reset(); } clearBackgroundingNotifierIfPossible();}
        void clearDeallocateRequester()   {if (mDeallocateRequester) { mDeallocateRequester->cancel(); mDeallocateRequester.reset(); } clearBackgroundingNotifierIfPossible();}

      public:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket::Server
        #pragma mark

        struct Server
        {
          Server();
          ~Server();

          static ServerPtr create();

          bool mIsUDP;  // true for UDP, false for TCP
          IPAddress mServerIP;

          SocketPtr mTCPSocket;
          bool mIsConnected;
          bool mInformedWriteReady;

          Time mActivateAfter;

          ISTUNRequesterPtr mAllocateRequester;

          BYTE mReadBuffer[OPENPEER_SERVICES_TURN_MAX_CHANNEL_DATA_IN_BYTES+sizeof(DWORD)];
          size_t mReadBufferFilledSizeInBytes;

          BYTE mWriteBuffer[OPENPEER_SERVICES_TURN_MAX_CHANNEL_DATA_IN_BYTES+sizeof(DWORD)];
          size_t mWriteBufferFilledSizeInBytes;
        };

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket::CompareIP
        #pragma mark

        class CompareIP { // simple comparison function
        public:
          bool operator()(const IPAddress &op1, const IPAddress &op2) const;
        };

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket::Permission
        #pragma mark

        struct Permission
        {
          static PermissionPtr create();

          bool mInstalled;
          IPAddress mPeerAddress;
          Time mLastSentDataAt;
          ISTUNRequesterPtr mInstallingWithRequester;

          typedef std::list<SecureByteBlockPtr> PendingDataList;
          PendingDataList mPendingData;
        };

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket::ChannelInfo
        #pragma mark

        struct ChannelInfo
        {
          static ChannelInfoPtr create();

          bool mBound;
          WORD mChannelNumber;
          IPAddress mPeerAddress;
          Time mLastSentDataAt;
          TimerPtr mRefreshTimer;
          ISTUNRequesterPtr mChannelBindRequester;
        };

      protected:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TURNSocket => (data)
        #pragma mark

        mutable RecursiveLock mLock;
        TURNSocketWeakPtr mThisWeak;
        TURNSocketPtr mGracefulShutdownReference;
        PUID mID;

        TURNSocketStates mCurrentState;
        TURNSocketErrors mLastError;

        IBackgroundingSubscriptionPtr mBackgroundingSubscription;
        IBackgroundingNotifierPtr mBackgroundingNotifier;

        WORD mLimitChannelToRangeStart;
        WORD mLimitChannelToRangeEnd;

        ITURNSocketDelegatePtr mDelegate;

        String mServerName;
        String mUsername;
        String mPassword;
        String mRealm;
        String mNonce;

        IDNSQueryPtr mTURNUDPQuery;
        IDNSQueryPtr mTURNTCPQuery;

        SRVResultPtr mTURNUDPSRVResult;
        SRVResultPtr mTURNTCPSRVResult;

        bool mUseChannelBinding;

        IPAddress mAllocateResponseIP;
        IPAddress mRelayedIP;
        IPAddress mReflectedIP;

        ServerPtr mActiveServer;

        DWORD mLifetime;

        ISTUNRequesterPtr mRefreshRequester;

        TimerPtr mRefreshTimer;
        Time mLastSentDataToServer;
        Time mLastRefreshTimerWasSentAt;

        ISTUNRequesterPtr mDeallocateRequester;
        TimerPtr mDeallocTimer;

        ServerList mServers;
        TimerPtr mActivationTimer;

        PermissionMap mPermissions;
        TimerPtr mPermissionTimer;
        ISTUNRequesterPtr mPermissionRequester;
        ULONG mPermissionRequesterMaxCapacity;

        ChannelIPMap mChannelIPMap;
        ChannelNumberMap mChannelNumberMap;

        bool          mForceTURNUseTCP;
        bool          mForceTURNUseUDP;
        IPAddressMap  mRestrictedIPs;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark ITURNSocketFactory
      #pragma mark

      interaction ITURNSocketFactory
      {
        static ITURNSocketFactory &singleton();

        virtual TURNSocketPtr create(
                                     IMessageQueuePtr queue,
                                     ITURNSocketDelegatePtr delegate,
                                     const char *turnServer,
                                     const char *turnServerUsername,
                                     const char *turnServerPassword,
                                     bool useChannelBinding = false,
                                     WORD limitChannelToRangeStart = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_START,
                                     WORD limitChannelRoRangeEnd = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_END
                                     );

        virtual TURNSocketPtr create(
                                     IMessageQueuePtr queue,
                                     ITURNSocketDelegatePtr delegate,
                                     IDNS::SRVResultPtr srvTURNUDP,
                                     IDNS::SRVResultPtr srvTURNTCP,
                                     const char *turnServerUsername,
                                     const char *turnServerPassword,
                                     bool useChannelBinding = false,
                                     WORD limitChannelToRangeStart = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_START,
                                     WORD limitChannelRoRangeEnd = OPENPEER_SERVICES_TURN_CHANNEL_RANGE_END
                                     );
      };

      class TURNSocketFactory : public IFactory<ITURNSocketFactory> {};
      
    }
  }
}
