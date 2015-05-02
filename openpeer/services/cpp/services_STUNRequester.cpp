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

#include <openpeer/services/internal/services_STUNRequester.h>
#include <openpeer/services/internal/services_STUNRequesterManager.h>

#include <openpeer/services/IHelper.h>

#include <zsLib/Exception.h>
#include <zsLib/helpers.h>
#include <zsLib/Log.h>
#include <zsLib/XML.h>
#include <zsLib/Stringize.h>

#define OPENPEER_SERVICES_STUN_REQUESTER_MAX_RETRANSMIT_STUN_ATTEMPTS (6)
#define OPENPEER_SERVICES_STUN_REQUESTER_FIRST_ATTEMPT_TIMEOUT_IN_MILLISECONDS (500)
#define OPENPEER_SERVICES_STUN_REQUESTER_MAX_REQUEST_TIME_IN_MILLISECONDS (39500)


namespace openpeer { namespace services { ZS_DECLARE_SUBSYSTEM(openpeer_services_ice) } }

namespace openpeer
{
  namespace services
  {
    namespace internal
    {
      ZS_DECLARE_TYPEDEF_PTR(ISTUNRequesterManagerForSTUNRequester, UseSTUNRequesterManager)

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNRequester
      #pragma mark

      //-----------------------------------------------------------------------
      STUNRequester::STUNRequester(
                                   IMessageQueuePtr queue,
                                   ISTUNRequesterDelegatePtr delegate,
                                   IPAddress serverIP,
                                   STUNPacketPtr stun,
                                   STUNPacket::RFCs usingRFC,
                                   Milliseconds maxTimeout
                                   ) :
        MessageQueueAssociator(queue),
        mID(zsLib::createPUID()),
        mDelegate(ISTUNRequesterDelegateProxy::createWeak(queue, delegate)),
        mSTUNRequest(stun),
        mServerIP(serverIP),
        mUsingRFC(usingRFC),
        mCurrentTimeout(Milliseconds(OPENPEER_SERVICES_STUN_REQUESTER_FIRST_ATTEMPT_TIMEOUT_IN_MILLISECONDS)),
        mRequestStartTime(zsLib::now()),
        mMaxTimeout(Milliseconds() != maxTimeout ? maxTimeout : Milliseconds(OPENPEER_SERVICES_STUN_REQUESTER_MAX_REQUEST_TIME_IN_MILLISECONDS))
      {
        IHelper::setTimerThreadPriority();
      }

      //-----------------------------------------------------------------------
      STUNRequester::~STUNRequester()
      {
        if(isNoop()) return;
        
        mThisWeak.reset();
        cancel();
      }

      //-----------------------------------------------------------------------
      void STUNRequester::init()
      {
        AutoRecursiveLock lock(mLock);
        UseSTUNRequesterManagerPtr manager = UseSTUNRequesterManager::singleton();
        if (manager) {
          manager->monitorStart(mThisWeak.lock(), mSTUNRequest);
        }
        step();
      }

      //-----------------------------------------------------------------------
      STUNRequesterPtr STUNRequester::convert(ISTUNRequesterPtr object)
      {
        return ZS_DYNAMIC_PTR_CAST(STUNRequester, object);
      }

      //-----------------------------------------------------------------------
      STUNRequesterPtr STUNRequester::convert(ForSTUNRequesterManagerPtr object)
      {
        return ZS_DYNAMIC_PTR_CAST(STUNRequester, object);
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNRequester => STUNRequester
      #pragma mark

      //-----------------------------------------------------------------------
      STUNRequesterPtr STUNRequester::create(
                                             IMessageQueuePtr queue,
                                             ISTUNRequesterDelegatePtr delegate,
                                             IPAddress serverIP,
                                             STUNPacketPtr stun,
                                             STUNPacket::RFCs usingRFC,
                                             Milliseconds maxTimeout
                                             )
      {
        ZS_THROW_INVALID_USAGE_IF(!delegate)
        ZS_THROW_INVALID_USAGE_IF(!stun)
        ZS_THROW_INVALID_USAGE_IF(serverIP.isAddressEmpty())
        ZS_THROW_INVALID_USAGE_IF(serverIP.isPortEmpty())

        STUNRequesterPtr pThis(new STUNRequester(queue, delegate, serverIP, stun, usingRFC, maxTimeout));
        pThis->mThisWeak = pThis;
        pThis->init();
        return pThis;
      }

      //-----------------------------------------------------------------------
      bool STUNRequester::isComplete() const
      {
        AutoRecursiveLock lock(mLock);
        if (!mDelegate) return true;
        return false;
      }

      //-----------------------------------------------------------------------
      void STUNRequester::cancel()
      {
        AutoRecursiveLock lock(mLock);

        if (mDelegate) {
          if (ZS_IS_LOGGING(Trace)) {
            ZS_LOG_BASIC(log("cancelled") + ZS_PARAM("stun packet", mSTUNRequest->toDebug()))
          }
        }

        internalCancel();
      }

      //-----------------------------------------------------------------------
      void STUNRequester::retryRequestNow()
      {
        AutoRecursiveLock lock(mLock);
        if (!mDelegate) return;
        if (mServerIP.isAddressEmpty()) return;
        if (!mSTUNRequest) return;

        mRequestStartTime = zsLib::now();
        mCurrentTimeout = Milliseconds(OPENPEER_SERVICES_STUN_REQUESTER_FIRST_ATTEMPT_TIMEOUT_IN_MILLISECONDS);
        mTryNumber = 0;
        if (mTimer) {
          mTimer->cancel();
          mTimer.reset();
        }
        if (mMaxTimeTimer) {
          mMaxTimeTimer->cancel();
          mMaxTimeTimer.reset();
        }

        step();
      }

      //-----------------------------------------------------------------------
      IPAddress STUNRequester::getServerIP() const
      {
        AutoRecursiveLock lock(mLock);
        return mServerIP;
      }

      //-----------------------------------------------------------------------
      STUNPacketPtr STUNRequester::getRequest() const
      {
        AutoRecursiveLock lock(mLock);
        return mSTUNRequest;
      }

      //-----------------------------------------------------------------------
      Milliseconds STUNRequester::getMaxTimeout() const
      {
        AutoRecursiveLock lock(mLock);
        return mMaxTimeout;
      }

      //-----------------------------------------------------------------------
      size_t STUNRequester::getTotalTries() const
      {
        AutoRecursiveLock lock(mLock);
        return mTotalTries;
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNRequester => ISTUNRequesterForSTUNRequesterManager
      #pragma mark

      //-----------------------------------------------------------------------
      bool STUNRequester::handleSTUNPacket(
                                           IPAddress fromIPAddress,
                                           STUNPacketPtr packet
                                           )
      {
        ISTUNRequesterDelegatePtr delegate;

        // find out if this was truly handled
        {
          AutoRecursiveLock lock(mLock);

          if (!mSTUNRequest) return false;

          if (!packet->isValidResponseTo(mSTUNRequest, mUsingRFC)) {
            ZS_LOG_TRACE(log("determined this response is not a proper validated response") + packet->toDebug())
            return false;
          }

          delegate = mDelegate;
        }

        bool success = true;

        // we now have a reply, inform the delegate...
        // NOTE:  We inform the delegate syncrhonously thus we cannot call
        //        the delegate from inside a lock in case the delegate is
        //        calling us (that would cause a potential deadlock).
        try {
          success = delegate->handleSTUNRequesterResponse(mThisWeak.lock(), fromIPAddress, packet);  // this is a success! yay! inform the delegate
        } catch(ISTUNDiscoveryDelegateProxy::Exceptions::DelegateGone &) {
        }

        if (!success)
          return false;

        // clear out the request since it's now complete
        {
          AutoRecursiveLock lock(mLock);
          internalCancel();
        }
        return true;
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNRequester => ITimerDelegate
      #pragma mark

      //-----------------------------------------------------------------------
      void STUNRequester::onTimer(TimerPtr timer)
      {
        Time tick = zsLib::now();
        Milliseconds totalTime = zsLib::toMilliseconds(tick - mRequestStartTime);

        {
          AutoRecursiveLock lock(mLock);
          if (!mDelegate) return;

          if (timer == mMaxTimeTimer) goto timed_out;

          if (timer != mTimer) {
            ZS_LOG_WARNING(Debug, log("received timer event from an obsolete timer"))
            return;
          }

          // the timer fired, that means the STUN lookup timed out
          mTimer.reset();

          ++mTryNumber;

          if ((mTryNumber >= OPENPEER_SERVICES_STUN_REQUESTER_MAX_RETRANSMIT_STUN_ATTEMPTS) ||
              (totalTime > mMaxTimeout)) {
            return;
          }

          mCurrentTimeout = mCurrentTimeout + mCurrentTimeout + Milliseconds(OPENPEER_SERVICES_STUN_REQUESTER_FIRST_ATTEMPT_TIMEOUT_IN_MILLISECONDS);

          // create a new timer using the new timeout
          step();
          return;
        }

      timed_out:
        {
          AutoRecursiveLock lock(mLock);
          ZS_LOG_WARNING(Detail, log("request timed out") + ZS_PARAM("on try number", mTryNumber) + ZS_PARAM("timeout duration (ms)", totalTime))
          if (mSTUNRequest) {
            ZS_LOG_TRACE(log("timed-out") + ZS_PARAM("stun packet", mSTUNRequest->toDebug()))
          }
          try {
            if (mDelegate) {
              mDelegate->onSTUNRequesterTimedOut(mThisWeak.lock());
            }
          } catch(ISTUNDiscoveryDelegateProxy::Exceptions::DelegateGone &) {
          }
        }

        internalCancel();
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark STUNRequester => (internal)
      #pragma mark

      //-----------------------------------------------------------------------
      Log::Params STUNRequester::log(const char *message) const
      {
        ElementPtr objectEl = Element::create("STUNRequester");
        IHelper::debugAppend(objectEl, "id", mID);
        return Log::Params(message, objectEl);
      }

      //-----------------------------------------------------------------------
      void STUNRequester::internalCancel()
      {
        AutoRecursiveLock lock(mLock);

        mServerIP.clear();

        if (mDelegate) {
          mDelegate.reset();

          // tie the lifetime of the monitoring to the delegate
          UseSTUNRequesterManagerPtr manager = UseSTUNRequesterManager::singleton();
          if (manager) {
            manager->monitorStop(*this);
          }
        }

        if (mTimer) {
          mTimer->cancel();
          mTimer.reset();
        }
        if (mMaxTimeTimer) {
          mMaxTimeTimer->cancel();
          mMaxTimeTimer.reset();
        }
      }

      //-----------------------------------------------------------------------
      void STUNRequester::step()
      {
        if (!mDelegate) return;                                                 // if there is no delegate then the request has completed or is cancelled

        if (mServerIP.isAddressEmpty()) return;

        if (!mSTUNRequest) return;

        if (!mMaxTimeTimer) {
          mMaxTimeTimer = Timer::create(mThisWeak.lock(), mMaxTimeout, false);
        }

        if (!mTimer) {
          // we have a stun request but not a timer, setup the timer now...
          mTimer = Timer::create(mThisWeak.lock(), mCurrentTimeout, false);

          ZS_LOG_TRACE(log("sending packet now") + ZS_PARAM("ip", mServerIP.string()) + ZS_PARAM("try number", mTryNumber) + ZS_PARAM("timeout duration (ms)", mCurrentTimeout) + ZS_PARAM("stun packet", mSTUNRequest->toDebug()))

          // send off the packet NOW
          SecureByteBlockPtr packet = mSTUNRequest->packetize(mUsingRFC);

          try {
            ++mTotalTries;
            mDelegate->onSTUNRequesterSendPacket(mThisWeak.lock(), mServerIP, packet);
          } catch(ISTUNDiscoveryDelegateProxy::Exceptions::DelegateGone &) {
            ZS_LOG_WARNING(Trace, log("delegate gone thus cancelling requester"))
            cancel();
            return;
          }
          // we have sent out the request, nothing more we can do but sit back and wait...
          return;
        }

        // nothing more to do... sit back, relax and enjoy the ride!
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark ISTUNRequesterFactory
      #pragma mark

      //-----------------------------------------------------------------------
      ISTUNRequesterFactory &ISTUNRequesterFactory::singleton()
      {
        return STUNRequesterFactory::singleton();
      }

      //-----------------------------------------------------------------------
      STUNRequesterPtr ISTUNRequesterFactory::create(
                                                     IMessageQueuePtr queue,
                                                     ISTUNRequesterDelegatePtr delegate,
                                                     IPAddress serverIP,
                                                     STUNPacketPtr stun,
                                                     STUNPacket::RFCs usingRFC,
                                                     Milliseconds maxTimeout
                                                     )
      {
        if (this) {}
        return STUNRequester::create(queue, delegate, serverIP, stun, usingRFC, maxTimeout);
      }

    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark ISTUNRequester
    #pragma mark

    //-------------------------------------------------------------------------
    ISTUNRequesterPtr ISTUNRequester::create(
                                             IMessageQueuePtr queue,
                                             ISTUNRequesterDelegatePtr delegate,
                                             IPAddress serverIP,
                                             STUNPacketPtr stun,
                                             STUNPacket::RFCs usingRFC,
                                             Milliseconds maxTimeout
                                             )
    {
      return internal::ISTUNRequesterFactory::singleton().create(queue, delegate, serverIP, stun, usingRFC, maxTimeout);
    }

    //-------------------------------------------------------------------------
    bool ISTUNRequester::handlePacket(
                                      IPAddress fromIPAddress,
                                      const BYTE *packet,
                                      size_t packetLengthInBytes,
                                      STUNPacket::RFCs allowedRFCs
                                      )
    {
      return (bool)ISTUNRequesterManager::handlePacket(fromIPAddress, packet, packetLengthInBytes, allowedRFCs);
    }

    //-------------------------------------------------------------------------
    bool ISTUNRequester::handleSTUNPacket(
                                          IPAddress fromIPAddress,
                                          STUNPacketPtr stun
                                          )
    {
      return (bool)ISTUNRequesterManager::handleSTUNPacket(fromIPAddress, stun);
    }
  }
}
