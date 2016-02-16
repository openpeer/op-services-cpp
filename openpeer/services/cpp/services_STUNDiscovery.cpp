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

#include <openpeer/services/internal/services_STUNDiscovery.h>
#include <openpeer/services/internal/services_Tracing.h>
#include <openpeer/services/ISTUNRequesterManager.h>
#include <openpeer/services/IHelper.h>

#include <zsLib/Exception.h>
#include <zsLib/helpers.h>

#include <algorithm>

#include <zsLib/Log.h>
#include <zsLib/XML.h>
#include <zsLib/Stringize.h>


namespace openpeer { namespace services { ZS_DECLARE_SUBSYSTEM(openpeer_services_ice) } }

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
      #pragma mark STUNDiscovery
      #pragma mark

      //-----------------------------------------------------------------------
      STUNDiscovery::STUNDiscovery(
                                   const make_private &,
                                   IMessageQueuePtr queue,
                                   ISTUNDiscoveryDelegatePtr delegate,
                                   Seconds keepWarmPingTime
                                   ) :
        MessageQueueAssociator(queue),
        mDelegate(ISTUNDiscoveryDelegateProxy::createWeak(queue, delegate)),
        mKeepWarmPingTime(keepWarmPingTime)
      {
        EventWriteOpServicesStunDiscoveryCreate(__func__, mID, keepWarmPingTime.count());
        ZS_LOG_DEBUG(log("created"))
      }

      //-----------------------------------------------------------------------
      void STUNDiscovery::init(
                               IDNS::SRVResultPtr service,
                               const char *srvName,
                               IDNS::SRVLookupTypes lookupType
                               )
      {
        ZS_THROW_INVALID_USAGE_IF((!service) && (!srvName))

        AutoRecursiveLock lock(mLock);

        mSRVResult = IDNS::cloneSRV(service);
        if (!mSRVResult) {
          // attempt a DNS lookup on the name
          mSRVQuery = IDNS::lookupSRV(mThisWeak.lock(), srvName, "stun", "udp", 3478, 10, 0, lookupType);
          EventWriteOpServicesStunDiscoveryLookupSrv(__func__, mID, ((bool)mSRVQuery) ? mSRVQuery->getID() : 0, srvName, "stun", "udp", 3478, 10, 0, zsLib::to_underlying(lookupType));
        }
        step();
      }

      //-----------------------------------------------------------------------
      STUNDiscovery::~STUNDiscovery()
      {
        if (isNoop()) return;
        
        mThisWeak.reset();
        ZS_LOG_DEBUG(log("destroyed"))
        cancel();
        EventWriteOpServicesStunDiscoveryDestroy(__func__, mID);
      }

      //-----------------------------------------------------------------------
      STUNDiscoveryPtr STUNDiscovery::convert(ISTUNDiscoveryPtr object)
      {
        return ZS_DYNAMIC_PTR_CAST(STUNDiscovery, object);
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNDiscovery => ISTUNDiscovery
      #pragma mark

      //-----------------------------------------------------------------------
      ElementPtr STUNDiscovery::toDebug(STUNDiscoveryPtr discovery)
      {
        if (!discovery) return ElementPtr();
        return discovery->toDebug();
      }

      //-----------------------------------------------------------------------
      STUNDiscoveryPtr STUNDiscovery::create(
                                             IMessageQueuePtr queue,
                                             ISTUNDiscoveryDelegatePtr delegate,
                                             IDNS::SRVResultPtr service,
                                             Seconds keepWarmPingTime
                                             )
      {
        ZS_THROW_INVALID_USAGE_IF(!queue)
        ZS_THROW_INVALID_USAGE_IF(!delegate)
        ZS_THROW_INVALID_USAGE_IF(!service)

        STUNDiscoveryPtr pThis(make_shared<STUNDiscovery>(make_private {}, queue, delegate, keepWarmPingTime));
        pThis->mThisWeak = pThis;
        pThis->init(service, NULL, IDNS::SRVLookupType_AutoLookupAll);
        return pThis;
      }

      //-----------------------------------------------------------------------
      STUNDiscoveryPtr STUNDiscovery::create(
                                             IMessageQueuePtr queue,
                                             ISTUNDiscoveryDelegatePtr delegate,
                                             const char *srvName,
                                             IDNS::SRVLookupTypes lookupType,
                                             Seconds keepWarmPingTime
                                             )
      {
        ZS_THROW_INVALID_USAGE_IF(!queue)
        ZS_THROW_INVALID_USAGE_IF(!delegate)
        ZS_THROW_INVALID_USAGE_IF(!srvName)

        STUNDiscoveryPtr pThis(make_shared<STUNDiscovery>(make_private {}, queue, delegate, keepWarmPingTime));
        pThis->mThisWeak = pThis;
        pThis->init(IDNS::SRVResultPtr(), srvName, lookupType);
        return pThis;
      }

      //-----------------------------------------------------------------------
      bool STUNDiscovery::isComplete() const
      {
        AutoRecursiveLock lock(mLock);
        if (!mDelegate) return true;
        if (!mMapppedAddress.isAddressEmpty()) return true;
        return false;
      }

      //-----------------------------------------------------------------------
      void STUNDiscovery::cancel()
      {
        EventWriteOpServicesStunDiscoveryCancel(__func__, mID);

        AutoRecursiveLock lock(mLock);
        if (mSRVQuery) {
          mSRVQuery->cancel();
          mSRVQuery.reset();
        }
        mSRVResult.reset();
        if (mSTUNRequester) {
          mSTUNRequester->cancel();
          mSTUNRequester.reset();
        }
        mDelegate.reset();
        mPreviouslyContactedServers.clear();
      }

      //-----------------------------------------------------------------------
      IPAddress STUNDiscovery::getMappedAddress() const
      {
        AutoRecursiveLock lock(mLock);
        return mMapppedAddress;
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNDiscovery => IDNSDelegate
      #pragma mark

      //-----------------------------------------------------------------------
      void STUNDiscovery::onLookupCompleted(IDNSQueryPtr query)
      {
        EventWriteOpServicesStunDiscoveryInternalLookupCompleteEventFired(__func__, mID, query->getID());

        AutoRecursiveLock lock(mLock);
        if (query != mSRVQuery) return;

        mSRVResult = query->getSRV();
        mSRVQuery.reset();
        step();
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNDiscovery => IDNSDelegate
      #pragma mark

      //-----------------------------------------------------------------------
      void STUNDiscovery::onTimer(TimerPtr timer)
      {
        EventWriteOpServicesStunDiscoveryInternalTimerEventFired(__func__, mID, timer->getID());

        AutoRecursiveLock lock(mLock);

        if (timer != mKeepWarmPingTimer) {
          ZS_LOG_WARNING(Trace, log("notified about obsolete timer") + ZS_PARAM("timer", timer->getID()))
          return;
        }

        ZS_LOG_TRACE(log("keep alive timer fired"))

        mKeepWarmPingTimer->cancel();
        mKeepWarmPingTimer.reset();

        step();
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNDiscovery => ISTUNRequesterDelegate
      #pragma mark

      //-----------------------------------------------------------------------
      void STUNDiscovery::onSTUNRequesterSendPacket(
                                                    ISTUNRequesterPtr requester,
                                                    IPAddress destination,
                                                    SecureByteBlockPtr packet
                                                    )
      {
        AutoRecursiveLock lock(mLock);
        if (requester != mSTUNRequester) return;

        try {
          EventWriteOpServicesStunDiscoveryRequestSendPacket(__func__, mID, requester->getID(), destination.string(), ((bool)packet) ? packet->SizeInBytes() : 0, ((bool)packet) ? packet->BytePtr() : NULL);
          mDelegate->onSTUNDiscoverySendPacket(mThisWeak.lock(), mServer, packet);
        } catch(ISTUNDiscoveryDelegateProxy::Exceptions::DelegateGone &) {
          cancel(); return;
        }
      }

      //-----------------------------------------------------------------------
      bool STUNDiscovery::handleSTUNRequesterResponse(
                                                      ISTUNRequesterPtr requester,
                                                      IPAddress fromIPAddress,
                                                      STUNPacketPtr response
                                                      )
      {
        EventWriteOpServicesStunDiscoveryReceivedResponsePacket(__func__, mID, requester->getID(), fromIPAddress.string(), ((bool)response) ? sizeof(response->mTransactionID) : 0, ((bool)response) ? (&(response->mTransactionID[0])) : NULL);

        AutoRecursiveLock lock(mLock);

        if (requester != mSTUNRequester) return true;               // if true, this must be for an old request so stop trying to handle it now...

        if (response->hasAttribute(STUNPacket::Attribute_ErrorCode)) {

          // this was reporting as a successful reply but it's actually errored
          if (STUNPacket::Class_ErrorResponse != response->mClass) {
            EventWriteOpServicesStunDiscoveryError(__func__, mID, requester->getID(), response->mErrorCode);
            return false; // handled the packet but its not valid so retry the request
          }

          switch (response->mErrorCode) {
            case STUNPacket::ErrorCode_TryAlternate:      {
              mServer = response->mAlternateServer;

              EventWriteOpServicesStunDiscoveryErrorUseAlternativeServer(__func__, mID, requester->getID(), mServer.string());

              // make sure it has a valid port
              if (mServer.isPortEmpty()) {
                mServer.clear();
                break;
              }

              if (hasContactedServerBefore(mServer)) {
                mServer.clear();
                break;
              }

              // remember that this server has been attempted
              mPreviouslyContactedServers.push_back(mServer);
              break;
            }
            case STUNPacket::ErrorCode_BadRequest:
            case STUNPacket::ErrorCode_Unauthorized:
            case STUNPacket::ErrorCode_UnknownAttribute:
            case STUNPacket::ErrorCode_StaleNonce:
            case STUNPacket::ErrroCode_ServerError:
            default:                                      {
              // we should stop trying to contact this server
              EventWriteOpServicesStunDiscoveryError(__func__, mID, requester->getID(), response->mErrorCode);
              mServer.clear();
              break;
            }
          }

          mSTUNRequester.reset();
          step();
          return true;
        }

        ZS_LOG_BASIC(log("found mapped address") + ZS_PARAM("mapped address", response->mMappedAddress.string()) + response->toDebug())

        IPAddress oldMappedAddress = mMapppedAddress;
        mMapppedAddress = response->mMappedAddress;

        EventWriteOpServicesStunDiscoveryFoundMappedAddress(__func__, mID, requester->getID(), mMapppedAddress.string(), oldMappedAddress.string());

        if (oldMappedAddress != mMapppedAddress) {
          // we now have a reply, inform the delegate
          try {
            mDelegate->onSTUNDiscoveryCompleted(mThisWeak.lock());  // this is a success! yay! inform the delegate
          } catch(ISTUNDiscoveryDelegateProxy::Exceptions::DelegateGone &) {
          }
        }

        mSTUNRequester.reset();

        if (Seconds() == mKeepWarmPingTime) {
          ZS_LOG_TRACE(log("shutting down stun discovery"))
          cancel();
          return  true;
        }

        if (!mKeepWarmPingTimer) {
          ZS_LOG_TRACE(log("setup server ping timer") + ZS_PARAM("keep alive (s)", mKeepWarmPingTime))
          mKeepWarmPingTimer = Timer::create(mThisWeak.lock(), zsLib::now() + mKeepWarmPingTime);
        }
        return true;
      }

      //-----------------------------------------------------------------------
      void STUNDiscovery::onSTUNRequesterTimedOut(ISTUNRequesterPtr requester)
      {
        EventWriteOpServicesStunDiscoveryErrorTimeout(__func__, mID, requester->getID());

        AutoRecursiveLock lock(mLock);
        if (requester != mSTUNRequester) return;

        // clear out the server to try so that we grab the next in the list
        mServer.clear();

        // generate a new request since we are talking to a different server
        mSTUNRequester.reset();
        step();
      }
      
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNDiscovery => (internal)
      #pragma mark

      //-----------------------------------------------------------------------
      Log::Params STUNDiscovery::log(const char *message) const
      {
        ElementPtr objectEl = Element::create("STUNDiscovery");
        IHelper::debugAppend(objectEl, "id", mID);
        return Log::Params(message, objectEl);
      }

      //-----------------------------------------------------------------------
      ElementPtr STUNDiscovery::toDebug() const
      {
        ElementPtr resultEl = Element::create("openpeer::services::STUNDiscovery");

        IHelper::debugAppend(resultEl, "srv query", mSRVQuery ? mSRVQuery->getID() : 0);
        IHelper::debugAppend(resultEl, "srv result", (bool)mSRVResult);

        IHelper::debugAppend(resultEl, "delegate", (bool)mDelegate);
        IHelper::debugAppend(resultEl, "stun requester", mSTUNRequester ? mSTUNRequester->getID(): 0);

        IHelper::debugAppend(resultEl, "server", mServer.isEmpty() ? String() : mServer.string());
        IHelper::debugAppend(resultEl, "mapped address", mMapppedAddress.isEmpty() ? String() : mMapppedAddress.string());

        IHelper::debugAppend(resultEl, "previously contacted servers", mPreviouslyContactedServers.size());

        IHelper::debugAppend(resultEl, "keep warm ping time (s)", mKeepWarmPingTime);
        IHelper::debugAppend(resultEl, "keep warm ping timer", mKeepWarmPingTimer ? mKeepWarmPingTimer->getID() : 0);

        return resultEl;
      }

      //-----------------------------------------------------------------------
      void STUNDiscovery::step()
      {
        if (!mDelegate) return;                                                 // if there is no delegate then the request has completed or is cancelled
        if (mSRVQuery) return;                                                  // if an outstanding SRV lookup is being done then do nothing

        // if there is no timer then we grab extract the next SRV server to try of the SRV record
        while (mServer.isAddressEmpty()) {
          bool found = IDNS::extractNextIP(mSRVResult, mServer);
          if (!found) {
            // no more results could be found, inform of the failure...
            try {
              ZS_LOG_BASIC(log("failed to contact any STUN server"))
              mMapppedAddress.clear();

              mDelegate->onSTUNDiscoveryCompleted(mThisWeak.lock());   // sorry, nothing to report as this was a failure condition
            } catch(ISTUNDiscoveryDelegateProxy::Exceptions::DelegateGone &) {
            }
            cancel();
            return;
          }

          if (mServer.isAddressEmpty())
            continue;

          if (mServer.isPortEmpty()) {
            mServer.clear();
            continue;
          }

          if (hasContactedServerBefore(mServer)) {
            mServer.clear();
            continue;
          }

          // remember this server as being contacted before
          mPreviouslyContactedServers.push_back(mServer);
        }

        if (!mSTUNRequester) {
          // we have a server but no request, create a STUN request now...
          STUNPacketPtr request = STUNPacket::createRequest(STUNPacket::Method_Binding);
          request->mLogObject = "STUNDiscovery";
          request->mLogObjectID = mID;
          mSTUNRequester = ISTUNRequester::create(
                                                  getAssociatedMessageQueue(),
                                                  mThisWeak.lock(),
                                                  mServer,
                                                  request,
                                                  ISTUNDiscovery::usingRFC()
                                                  );
          EventWriteOpServicesStunDiscoveryRequestCreate(__func__, mID, ((bool)mSTUNRequester) ? mSTUNRequester->getID() : 0, mServer.string(), sizeof(request->mTransactionID), (&(request->mTransactionID[0])));
        }

        // nothing more to do... sit back, relax and enjoy the ride!
      }

      //-----------------------------------------------------------------------
      bool STUNDiscovery::hasContactedServerBefore(const IPAddress &server)
      {
        return mPreviouslyContactedServers.end() != find(mPreviouslyContactedServers.begin(), mPreviouslyContactedServers.end(), server);
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark ISTUNDiscoveryFactory
      #pragma mark

      //-----------------------------------------------------------------------
      ISTUNDiscoveryFactory &ISTUNDiscoveryFactory::singleton()
      {
        return STUNDiscoveryFactory::singleton();
      }

      //-----------------------------------------------------------------------
      STUNDiscoveryPtr ISTUNDiscoveryFactory::create(
                                                     IMessageQueuePtr queue,
                                                     ISTUNDiscoveryDelegatePtr delegate,
                                                     IDNS::SRVResultPtr service,
                                                     Seconds keepWarmPingTime
                                                     )
      {
        if (this) {}
        return STUNDiscovery::create(queue, delegate, service, keepWarmPingTime);
      }

      //-----------------------------------------------------------------------
      STUNDiscoveryPtr ISTUNDiscoveryFactory::create(
                                                     IMessageQueuePtr queue,
                                                     ISTUNDiscoveryDelegatePtr delegate,
                                                     const char *srvName,
                                                     IDNS::SRVLookupTypes lookupType,
                                                     Seconds keepWarmPingTime
                                                     )
      {
        if (this) {}
        return STUNDiscovery::create(queue, delegate, srvName, lookupType, keepWarmPingTime);
      }
      
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark ISTUNDiscovery
    #pragma mark

    //-------------------------------------------------------------------------
    ElementPtr ISTUNDiscovery::toDebug(ISTUNDiscoveryPtr discovery)
    {
      return internal::STUNDiscovery::toDebug(internal::STUNDiscovery::convert(discovery));
    }

    //-------------------------------------------------------------------------
    ISTUNDiscoveryPtr ISTUNDiscovery::create(
                                             IMessageQueuePtr queue,
                                             ISTUNDiscoveryDelegatePtr delegate,
                                             IDNS::SRVResultPtr service,
                                             Seconds keepWarmPingTime
                                             )
    {
      return internal::ISTUNDiscoveryFactory::singleton().create(queue, delegate, service, keepWarmPingTime);
    }

    //-------------------------------------------------------------------------
    ISTUNDiscoveryPtr ISTUNDiscovery::create(
                                             IMessageQueuePtr queue,
                                             ISTUNDiscoveryDelegatePtr delegate,
                                             const char *srvName,
                                             IDNS::SRVLookupTypes lookupType,
                                             Seconds keepWarmPingTime
                                             )
    {
      return internal::ISTUNDiscoveryFactory::singleton().create(queue, delegate, srvName, lookupType, keepWarmPingTime);
    }

    //-------------------------------------------------------------------------
    STUNPacket::RFCs ISTUNDiscovery::usingRFC()
    {
      return STUNPacket::RFC_5389_STUN;
    }

    //-------------------------------------------------------------------------
    bool ISTUNDiscovery::handleSTUNPacket(
                                          IPAddress fromIPAddress,
                                          STUNPacketPtr stun
                                          )
    {
      ISTUNRequesterPtr requester = ISTUNRequesterManager::handleSTUNPacket(fromIPAddress, stun);
      return (bool)requester;
    }

    //-------------------------------------------------------------------------
    bool ISTUNDiscovery::handlePacket(
                                      IPAddress fromIPAddress,
                                      BYTE *packet,
                                      size_t packetLengthInBytes
                                      )
    {
      ISTUNRequesterPtr requester = ISTUNRequesterManager::handlePacket(fromIPAddress, packet, packetLengthInBytes, ISTUNDiscovery::usingRFC());
      return (bool)requester;
    }
  }
}
