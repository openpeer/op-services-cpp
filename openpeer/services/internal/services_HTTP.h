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
#include <openpeer/services/IHTTP.h>

#include <zsLib/IPAddress.h>
#include <zsLib/Socket.h>
#include <cryptopp/secblock.h>
#include <cryptopp/queue.h>

#include <curl/curl.h>

#define OPENPEER_SERVICES_SETTING_HELPER_HTTP_THREAD_PRIORITY "openpeer/services/http-thread-priority"

namespace openpeer
{
  namespace services
  {
    namespace internal
    {
      class HTTPGlobalSafeReference;

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark HTTP
      #pragma mark

      class HTTP : public Noop,
                   public SharedRecursiveLock,
                   public IHTTP
      {
      public:
        friend class HTTPGlobalSafeReference;
        friend interaction IHTTPFactory;

        ZS_DECLARE_CLASS_PTR(HTTPQuery)

        friend class HTTPQuery;

      protected:
        HTTP();
        HTTP(Noop) :
          Noop(true),
          SharedRecursiveLock(SharedRecursiveLock::create())
        {}

        void init();

      public:
        ~HTTP();

      protected:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark HTTP => IHTTP
        #pragma mark

        static HTTPQueryPtr get(
                                IHTTPQueryDelegatePtr delegate,
                                const char *userAgent,
                                const char *url,
                                Milliseconds timeout
                                );

        static HTTPQueryPtr post(
                                 IHTTPQueryDelegatePtr delegate,
                                 const char *userAgent,
                                 const char *url,
                                 const BYTE *postData,
                                 size_t postDataLengthInBytes,
                                 const char *postDataMimeType,
                                 Milliseconds timeout
                                 );

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark HTTP => friend HTTPQuery
        #pragma mark

        // (duplicate) void monitorEnd(HTTPQueryPtr query);

      protected:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark HTTP => (internal)
        #pragma mark

        static HTTPPtr singleton();
        static HTTPPtr create();

        Log::Params log(const char *message) const;
        static Log::Params slog(const char *message);

        void cancel();

        void wakeUp();
        void createWakeUpSocket();

        void processWaiting();

        void monitorBegin(HTTPQueryPtr query);
        void monitorEnd(HTTPQueryPtr query);

      public:
        void operator()();

      public:
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark HTTP => class HTTPQuery
        #pragma mark

        class HTTPQuery : public SharedRecursiveLock,
                          public IHTTPQuery
        {
        protected:
          HTTPQuery(
                    HTTPPtr outer,
                    IHTTPQueryDelegatePtr delegate,
                    bool isPost,
                    const char *userAgent,
                    const char *url,
                    const BYTE *postData,
                    size_t postDataLengthInBytes,
                    const char *postDataMimeType,
                    Milliseconds timeout
                    );

          void init();

        public:
          ~HTTPQuery();

          //-------------------------------------------------------------------
          #pragma mark
          #pragma mark HTTP::HTTPQuery => IHTTPQuery
          #pragma mark

          virtual PUID getID() const {return mID;}

          virtual void cancel();

          virtual bool isComplete() const;
          virtual bool wasSuccessful() const;
          virtual HTTPStatusCodes getStatusCode() const;
          virtual long getResponseCode() const;

          virtual size_t getHeaderReadSizeAvailableInBytes() const;
          virtual size_t readHeader(
                                    BYTE *outResultData,
                                    size_t bytesToRead
                                    );

          virtual size_t readHeaderAsString(String &outHeader);

          virtual size_t getReadDataAvailableInBytes() const;

          virtual size_t readData(
                                  BYTE *outResultData,
                                  size_t bytesToRead
                                  );

          virtual size_t readDataAsString(String &outResultData);

          //-------------------------------------------------------------------
          #pragma mark
          #pragma mark HTTP::HTTPQuery => friend HTTP
          #pragma mark

          static HTTPQueryPtr create(
                                     HTTPPtr outer,
                                     IHTTPQueryDelegatePtr delegate,
                                     bool isPost,
                                     const char *userAgent,
                                     const char *url,
                                     const BYTE *postData,
                                     size_t postDataLengthInBytes,
                                     const char *postDataMimeType,
                                     Milliseconds timeout
                                     );

          // (duplicate) PUID getID() const;

          void prepareCurl();
          void cleanupCurl();

          CURL *getCURL() const;

          void notifyComplete(CURLcode result);

        protected:
          //-------------------------------------------------------------------
          #pragma mark
          #pragma mark HTTP::HTTPQuery => (internal)
          #pragma mark

          Log::Params log(const char *message) const;

          static size_t writeHeader(
                                    void *ptr,
                                    size_t size,
                                    size_t nmemb,
                                    void *userdata
                                    );

          static size_t writeData(
                                  char *ptr,
                                  size_t size,
                                  size_t nmemb,
                                  void *userdata
                                  );

          static int debug(
                           CURL *handle,
                           curl_infotype type,
                           char *data,
                           size_t size,
                           void *userdata
                           );

        protected:
          //-------------------------------------------------------------------
          #pragma mark
          #pragma mark HTTP::HTTPQuery => (data)
          #pragma mark

          AutoPUID mID;
          HTTPQueryWeakPtr mThisWeak;

          HTTPWeakPtr mOuter;
          IHTTPQueryDelegatePtr mDelegate;

          bool mIsPost;
          String mUserAgent;
          String mURL;
          String mMimeType;
          Milliseconds mTimeout;

          SecureByteBlock mPostData;
          SecureByteBlock mErrorBuffer;

          CURL *mCurl;
          long mResponseCode;
          CURLcode mResultCode;
          struct curl_slist *mHeaders;

          ByteQueue mHeader;
          ByteQueue mBody;
        };

      protected:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark HTTP => (data)
        #pragma mark

        AutoPUID mID;
        HTTPWeakPtr mThisWeak;
        HTTPPtr mGracefulShutdownReference;

        ThreadPtr mThread;
        bool mShouldShutdown;

        IPAddress mWakeUpAddress;
        SocketPtr mWakeUpSocket;

        CURLM *mMultiCurl;

        typedef std::list<EventPtr> EventList;
        EventList mWaitingForRebuildList;

        typedef PUID QueryID;
        typedef std::map<QueryID, HTTPQueryPtr> HTTPQueryMap;
        HTTPQueryMap mQueries;
        HTTPQueryMap mPendingAddQueries;
        HTTPQueryMap mPendingRemoveQueries;

        typedef std::map<CURL *, HTTPQueryPtr> HTTPCurlMap;
        HTTPCurlMap mCurlMap;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark IHTTPFactory
      #pragma mark

      interaction IHTTPFactory
      {
        static IHTTPFactory &singleton();

        virtual IHTTPQueryPtr get(
                                  IHTTPQueryDelegatePtr delegate,
                                  const char *userAgent,
                                  const char *url,
                                  Milliseconds timeout
                                  );

        virtual IHTTPQueryPtr post(
                                   IHTTPQueryDelegatePtr delegate,
                                   const char *userAgent,
                                   const char *url,
                                   const BYTE *postData,
                                   size_t postDataLengthInBytes,
                                   const char *postDataMimeType,
                                   Milliseconds timeout
                                   );
      };

      class HTTPFactory : public IFactory<IHTTPFactory> {};
    }
  }
}
