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

#include <openpeer/services/internal/services_Logger.h>
#include <openpeer/services/internal/services_Tracing.h>
#include <openpeer/services/IBackgrounding.h>
#include <openpeer/services/IDNS.h>
#include <openpeer/services/IHelper.h>
#include <openpeer/services/ISettings.h>
#include <openpeer/services/IWakeDelegate.h>

#include <cryptopp/osrng.h>

#include <zsLib/Stringize.h>
#include <zsLib/helpers.h>
#include <zsLib/Log.h>
#include <zsLib/Socket.h>
#include <zsLib/MessageQueueThread.h>
#include <zsLib/Timer.h>
#include <zsLib/XML.h>
#include <zsLib/Numeric.h>

#include <iostream>
#include <fstream>
#include <ctime>

#ifdef HAVE_GMTIME_S
#include <time.h>
#endif //HAVE_GMTIME_S

#ifndef _WIN32
#include <pthread.h>
#endif //ndef _WIN32

#ifdef __QNX__
#ifndef NDEBUG
#include <QDebug>
#endif //ndef NDEBUG
#endif //__QNX__

namespace openpeer { namespace services { ZS_DECLARE_SUBSYSTEM(openpeer_services) } }

#define OPENPEER_SERVICES_DEFAULT_OUTGOING_TELNET_PORT (59999)
#define OPENPEER_SERVICES_MAX_TELNET_LOGGER_PENDING_CONNECTIONBACKLOG_TIME_SECONDS (60)

#define OPENPEER_SERVICES_SEQUENCE_ESCAPE                    "\x1B"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET              OPENPEER_SERVICES_SEQUENCE_ESCAPE "[0m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_THREAD             OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[33m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_TIME               OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[33m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_INFO      OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[36m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_WARNING   OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[35m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_ERROR     OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[31m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_FATAL     OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[31m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY           OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[36m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_BASIC      OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[1m" OPENPEER_SERVICES_SEQUENCE_ESCAPE "[30m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_DETAIL     OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[1m" OPENPEER_SERVICES_SEQUENCE_ESCAPE "[30m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_DEBUG      OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[30m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_TRACE      OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[34m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_INSANE     OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[36m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_FILENAME           OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[32m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_LINENUMBER         OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[32m"
#define OPENPEER_SERVICES_SEQUENCE_COLOUR_FUNCTION           OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET OPENPEER_SERVICES_SEQUENCE_ESCAPE "[36m"


namespace openpeer
{
  namespace services
  {
    using zsLib::Numeric;
    using zsLib::AutoRecursiveLock;
    using zsLib::Seconds;
    using zsLib::Milliseconds;
    using zsLib::Microseconds;

    namespace internal
    {

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark (helpers)
      #pragma mark

      //-----------------------------------------------------------------------
      static String currentThreadIDAsString()
      {
#ifdef _WIN32
        return string(GetCurrentThreadId());
#else
#ifdef __APPLE__
        return string(((PTRNUMBER)pthread_mach_thread_np(pthread_self())));
#else
        return string(((PTRNUMBER)pthread_self()));
#endif //APPLE
#endif //_WIN32
      }

      //-----------------------------------------------------------------------
      static String getMessageString(
                                     const Log::Params &params,
                                     bool prettyPrint
                                     )
      {
        static const char *wires[] = {"wire in", "wire out", NULL};
        static const char *jsons[] = {"json in", "json out", "json", NULL};

        String objectString;

        ElementPtr objectEl = params.object();

        if (objectEl) {
          objectString = objectEl->getValue();

          ElementPtr idEl = objectEl->findFirstChildElement("id");
          if (idEl) {
            String objectID = idEl->getTextDecoded();
            if (objectID.hasData()) {
              objectString += " [" + objectID + "] ";
            } else {
              objectString += " [] ";
            }

            if ((objectEl->getFirstChild() == idEl) &&
                (objectEl->getLastChild() == idEl)) {
              objectEl.reset(); // this is now an empty object that we don't need anymore
            }

          } else {
            objectString += " [] ";
          }
        }
        if (objectEl) {
          if (!objectEl->hasChildren()) {
            objectEl.reset();
          }
        }

        String message = objectString + params.message();

        String alt;

        ElementPtr paramsEl = params.params();
        if (paramsEl) {
          for (int index = 0; wires[index]; ++index) {
            ElementPtr childEl = paramsEl->findFirstChildElement(wires[index]);
            if (!childEl) continue;

            SecureByteBlockPtr buffer = IHelper::convertFromBase64(childEl->getTextDecoded());
            if (IHelper::isEmpty(buffer)) continue;

            alt += "\n\n" + IHelper::getDebugString(*buffer) + "\n\n";
          }

          for (int index = 0; jsons[index]; ++index) {
            ElementPtr childEl = paramsEl->findFirstChildElement(jsons[index]);
            if (!childEl) continue;

            String json = childEl->getTextDecoded();
            if (json.isEmpty()) continue;

            if (prettyPrint) {
              DocumentPtr doc = Document::createFromParsedJSON(json);
              std::unique_ptr<char[]> output = doc->writeAsJSON(true);
              alt += "\n\n" + String((CSTR)output.get()) + "\n\n";
            } else {
              alt += "\n\n" + json + "\n\n";
            }
          }
        }

        if (alt.hasData()) {
          // strip out the wire stuff
          paramsEl = paramsEl->clone()->toElement();

          for (int index = 0; wires[index]; ++index) {
            ElementPtr childEl = paramsEl->findFirstChildElement(wires[index]);
            if (!childEl) continue;

            childEl->orphan();
          }

          for (int index = 0; jsons[index]; ++index) {
            ElementPtr childEl = paramsEl->findFirstChildElement(jsons[index]);
            if (!childEl) continue;

            childEl->orphan();
          }
        }

        // strip out empty params
        if (paramsEl) {
          if (!paramsEl->hasChildren()) {
            paramsEl.reset();
          }
        }

        if (paramsEl) {
          message += " " + IHelper::toString(paramsEl);
        }
        if (objectEl) {
          message += " " + IHelper::toString(objectEl);
        }
        message += alt;

        return message;
      }

      //-----------------------------------------------------------------------
      static std::string getNowTime()
      {
        Time now = zsLib::now();

        time_t tt = std::chrono::system_clock::to_time_t(now);
        Time secOnly = std::chrono::system_clock::from_time_t(tt);

        Microseconds remainder = std::chrono::duration_cast<Microseconds>(now - secOnly);

        std::tm ttm {};
#ifdef HAVE_GMTIME_S
        auto error = gmtime_s(&ttm, &tt);
        ZS_THROW_BAD_STATE_IF(0 != error)
#else
        gmtime_r(&tt, &ttm);
#endif //_WIN32

        //HH:MM:SS.123456
        char buffer[100] {};

#ifdef HAVE_SPRINTF_S
        sprintf_s(
#else
        snprintf(
#endif //HAVE_SPRINTF_S
          &(buffer[0]),
          sizeof(buffer),
          "%02u:%02u:%02u:%06u", ((UINT)ttm.tm_hour), ((UINT)ttm.tm_min), ((UINT)ttm.tm_sec), static_cast<UINT>(remainder.count())
        );

        return buffer;
      }

      //-----------------------------------------------------------------------
      static String toColorString(
                                  const Subsystem &inSubsystem,
                                  Log::Severity inSeverity,
                                  Log::Level inLevel,
                                  const Log::Params &params,
                                  CSTR inFunction,
                                  CSTR inFilePath,
                                  ULONG inLineNumber,
                                  bool prettyPrint,
                                  bool eol = true
                                  )
      {
        const char *posBackslash = strrchr(inFilePath, '\\');
        const char *posSlash = strrchr(inFilePath, '/');

        const char *fileName = inFilePath;

        if (!posBackslash)
          posBackslash = posSlash;

        if (!posSlash)
          posSlash = posBackslash;

        if (posSlash) {
          if (posBackslash > posSlash)
            posSlash = posBackslash;
          fileName = posSlash + 1;
        }

        std::string current = getNowTime();

        const char *colorSeverity = OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_INFO;
        const char *severity = "NONE";
        switch (inSeverity) {
          case Log::Informational:   severity = "i:"; colorSeverity = OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_INFO; break;
          case Log::Warning:         severity = "W:"; colorSeverity = OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_WARNING; break;
          case Log::Error:           severity = "E:"; colorSeverity = OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_ERROR; break;
          case Log::Fatal:           severity = "F:"; colorSeverity = OPENPEER_SERVICES_SEQUENCE_COLOUR_SEVERITY_FATAL; break;
        }

        const char *colorLevel = OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_TRACE;
        switch (inLevel) {
          case Log::Basic:           colorLevel = OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_BASIC; break;
          case Log::Detail:          colorLevel = OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_DETAIL; break;
          case Log::Debug:           colorLevel = OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_DEBUG; break;
          case Log::Trace:           colorLevel = OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_TRACE; break;
          case Log::Insane:          colorLevel = OPENPEER_SERVICES_SEQUENCE_COLOUR_MESSAGE_INSANE; break;
          case Log::None:            break;
        }

//        const Log::Params &params;

        String result = String(OPENPEER_SERVICES_SEQUENCE_COLOUR_TIME) + current
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET + " "
                      + colorSeverity + severity
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET + " "
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_THREAD + "<" + currentThreadIDAsString() + ">"
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET + " "
                      + colorLevel + getMessageString(params, prettyPrint)
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET + " "
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_FILENAME + "@" + fileName
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_LINENUMBER + "(" + string(inLineNumber) + ")"
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET + " "
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_FUNCTION + "[" + inFunction + "]"
                      + OPENPEER_SERVICES_SEQUENCE_COLOUR_RESET + (eol ? "\n" : "");

        return result;
      }

      //-----------------------------------------------------------------------
      static String toBWString(
                               const Subsystem &inSubsystem,
                               Log::Severity inSeverity,
                               Log::Level inLevel,
                               const Log::Params &params,
                               CSTR inFunction,
                               CSTR inFilePath,
                               ULONG inLineNumber,
                               bool prettyPrint,
                               bool eol = true
                               )
      {
        const char *posBackslash = strrchr(inFilePath, '\\');
        const char *posSlash = strrchr(inFilePath, '/');

        const char *fileName = inFilePath;

        if (!posBackslash)
          posBackslash = posSlash;

        if (!posSlash)
          posSlash = posBackslash;

        if (posSlash) {
          if (posBackslash > posSlash)
            posSlash = posBackslash;
          fileName = posSlash + 1;
        }

        std::string current = getNowTime();

        const char *severity = "NONE";
        switch (inSeverity) {
          case Log::Informational:   severity = "i:"; break;
          case Log::Warning:         severity = "W:"; break;
          case Log::Error:           severity = "E:"; break;
          case Log::Fatal:           severity = "F:"; break;
        }

        String result = current + " " + severity + " <"  + currentThreadIDAsString() + "> " + getMessageString(params, prettyPrint) + " " + "@" + fileName + "(" + string(inLineNumber) + ")" + " " + "[" + inFunction + "]" + (eol ? "\n" : "");
        return result;
      }

      //-----------------------------------------------------------------------
      static String toWindowsString(
                                    const Subsystem &inSubsystem,
                                    Log::Severity inSeverity,
                                    Log::Level inLevel,
                                    const Log::Params &params,
                                    CSTR inFunction,
                                    CSTR inFilePath,
                                    ULONG inLineNumber,
                                    bool prettyPrint,
                                    bool eol = true
                                    )
      {
        std::string current = getNowTime();

        const char *severity = "NONE";
        switch (inSeverity) {
          case Log::Informational:   severity = "i:"; break;
          case Log::Warning:         severity = "W:"; break;
          case Log::Error:           severity = "E:"; break;
          case Log::Fatal:           severity = "F:"; break;
        }

        String result = String(inFilePath) + "(" + string(inLineNumber) + "): " + severity + " T" + currentThreadIDAsString() + ": " + current + getMessageString(params, prettyPrint) + (eol ? "\n" : "");
        return result;
      }

      //-----------------------------------------------------------------------
      static void appendToDoc(
                              DocumentPtr &doc,
                              const Log::Param param
                              )
      {
        if (!param.param()) return;
        doc->adoptAsLastChild(param.param());
      }

      //-----------------------------------------------------------------------
      static void appendToDoc(
                              DocumentPtr &doc,
                              const ElementPtr &childEl
                              )
      {
        if (!childEl) return;

        ZS_THROW_INVALID_ASSUMPTION_IF(childEl->getParent())

        doc->adoptAsLastChild(childEl);
      }

      //-----------------------------------------------------------------------
      static String toRawJSON(
                              const Subsystem &inSubsystem,
                              Log::Severity inSeverity,
                              Log::Level inLevel,
                              const Log::Params &params,
                              CSTR inFunction,
                              CSTR inFilePath,
                              ULONG inLineNumber,
                              bool eol = true
                              )
      {
        const char *posBackslash = strrchr(inFilePath, '\\');
        const char *posSlash = strrchr(inFilePath, '/');

        const char *fileName = inFilePath;

        if (!posBackslash)
          posBackslash = posSlash;

        if (!posSlash)
          posSlash = posBackslash;

        if (posSlash) {
          if (posBackslash > posSlash)
            posSlash = posBackslash;
          fileName = posSlash + 1;
        }

        DocumentPtr message = Document::create();
        ElementPtr objecEl = Element::create("object");
        ElementPtr timeEl = Element::create("time");
        TextPtr timeText = Text::create();

        std::string current = getNowTime();

        timeText->setValue(current);
        timeEl->adoptAsLastChild(timeText);

        appendToDoc(message, Log::Param("submodule", inSubsystem.getName()));
        appendToDoc(message, Log::Param("severity", Log::toString(inSeverity)));
        appendToDoc(message, Log::Param("level", Log::toString(inLevel)));
        appendToDoc(message, Log::Param("thread", currentThreadIDAsString()));
        appendToDoc(message, Log::Param("function", inFunction));
        appendToDoc(message, Log::Param("file", fileName));
        appendToDoc(message, Log::Param("line", inLineNumber));
        appendToDoc(message, Log::Param("message", params.message()));
        message->adoptAsLastChild(timeEl);

        IHelper::debugAppend(objecEl, params.object());
        if (objecEl->hasChildren()) {
          appendToDoc(message, objecEl);
        }
        appendToDoc(message, params.params());

        GeneratorPtr generator = Generator::createJSONGenerator();
        std::unique_ptr<char[]> output = generator->write(message);

        String result = (CSTR)output.get();
        if (eol) {
          result += "\n";
        }

        if (params.object()) {
          params.object()->orphan();
        }
        if (params.params()) {
          params.params()->orphan();
        }

        return result;
      }

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark LoggerReferenceHolder<T>
      #pragma mark

      template <typename T>
      struct LoggerReferenceHolder
      {
        ZS_DECLARE_TYPEDEF_PTR(T, Logger)
        ZS_DECLARE_TYPEDEF_PTR(LoggerReferenceHolder<T>, Holder)

        LoggerPtr mLogger;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark LoggerSingletonAndLockHolder<T>
      #pragma mark

      template <typename T>
      class LoggerSingletonAndLockHolder
      {
      public:
        ZS_DECLARE_TYPEDEF_PTR(LoggerReferenceHolder<T>, ReferenceHolder)
        ZS_DECLARE_TYPEDEF_PTR(LoggerSingletonAndLockHolder<T>, Self)
        ZS_DECLARE_TYPEDEF_PTR(SingletonLazySharedPtr< Self >, SingletonLazySelf)

        LoggerSingletonAndLockHolder() :
          mLock(make_shared<RecursiveLock>()),
          mSingleton(make_shared<ReferenceHolder>())
        {
        }

        RecursiveLockPtr lock() {return mLock;}
        ReferenceHolderPtr reference() {return mSingleton;}

      protected:
        RecursiveLockPtr mLock;
        ReferenceHolderPtr mSingleton;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark LoggerSingletonLazySharedPtr<T>
      #pragma mark

      template <typename T>
      class LoggerSingletonLazySharedPtr : SingletonLazySharedPtr< LoggerSingletonAndLockHolder<T> >
      {
      public:
        ZS_DECLARE_TYPEDEF_PTR(LoggerReferenceHolder<T>, ReferenceHolder)
        ZS_DECLARE_TYPEDEF_PTR(LoggerSingletonAndLockHolder<T>, Holder)
        ZS_DECLARE_TYPEDEF_PTR(LoggerSingletonLazySharedPtr<T>, SingletonLazySelf)

      public:
        LoggerSingletonLazySharedPtr() :
          SingletonLazySharedPtr< LoggerSingletonAndLockHolder<T> >(make_shared<Holder>())
        {
        }

        static RecursiveLockPtr lock(SingletonLazySelf &singleton)
        {
          HolderPtr result = singleton.singleton();
          if (!result) {
            return make_shared<RecursiveLock>();
          }
          return result->lock();
        }

        static ReferenceHolderPtr logger(SingletonLazySelf &singleton)
        {
          HolderPtr result = singleton.singleton();
          if (!result) {
            return make_shared<ReferenceHolder>();
          }
          return result->reference();
        }
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark LogLevelLogger
      #pragma mark

      ZS_DECLARE_CLASS_PTR(LogLevelLogger)

      //-----------------------------------------------------------------------
      class LogLevelLogger : public ILogDelegate
      {
        //---------------------------------------------------------------------
        void init()
        {
          Log::addListener(mThisWeak.lock());
        }

      public:
        //---------------------------------------------------------------------
        LogLevelLogger() :
          mDefaultLogLevelSet(false),
          mDefaultLogLevel(Log::None)
        {}

        //---------------------------------------------------------------------
        static LogLevelLoggerPtr create()
        {
          LogLevelLoggerPtr pThis(make_shared<LogLevelLogger>());
          pThis->mThisWeak = pThis;
          pThis->init();
          return pThis;
        }

        //---------------------------------------------------------------------
        static LogLevelLoggerPtr singleton() {
          static SingletonLazySharedPtr<LogLevelLogger> singleton(LogLevelLogger::create());
          return singleton.singleton();
        }

        //---------------------------------------------------------------------
        void setLogLevel(Log::Level level)
        {
          AutoRecursiveLock lock(mLock);

          mLevels.clear();

          mDefaultLogLevelSet = true;
          mDefaultLogLevel = level;
          for (SubsystemMap::iterator iter = mSubsystems.begin(); iter != mSubsystems.end(); ++iter) {
            Subsystem * &subsystem = (*iter).second;
            (*subsystem).setOutputLevel(level);
          }
        }

        //---------------------------------------------------------------------
        void setLogLevel(const char *component, Log::Level level)
        {
          AutoRecursiveLock lock(mLock);
          mLevels[component] = level;

          SubsystemMap::iterator found = mSubsystems.find(component);
          if (found == mSubsystems.end()) return;

          Subsystem * &subsystem = (*found).second;
          (*subsystem).setOutputLevel(level);
        }

        //---------------------------------------------------------------------
        virtual void onNewSubsystem(Subsystem &inSubsystem)
        {
          AutoRecursiveLock lock(mLock);

          const char *name = inSubsystem.getName();
          mSubsystems[name] = &(inSubsystem);

          LevelMap::iterator found = mLevels.find(name);
          if (found == mLevels.end()) {
            if (!mDefaultLogLevelSet) return;
            inSubsystem.setOutputLevel(mDefaultLogLevel);
            return;
          }

          Log::Level level = (*found).second;
          inSubsystem.setOutputLevel(level);
        }

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark LogLevelLogger => ILogDelegate
        #pragma mark

        // ignored

      private:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark LogLevelLogger => (data)
        #pragma mark

        LogLevelLoggerWeakPtr mThisWeak;

        mutable RecursiveLock mLock;

        typedef std::map<String, Subsystem *> SubsystemMap;
        typedef std::map<String, Log::Level> LevelMap;

        SubsystemMap mSubsystems;
        LevelMap mLevels;

        bool mDefaultLogLevelSet;
        Log::Level mDefaultLogLevel;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark StdOutLogger
      #pragma mark

      ZS_DECLARE_CLASS_PTR(StdOutLogger)

      class StdOutLogger : public ILogDelegate
      {
        //---------------------------------------------------------------------
        void init()
        {
          Log::addListener(mThisWeak.lock());
        }

      public:
        //---------------------------------------------------------------------
        StdOutLogger(
                     bool colorizeOutput,
                     bool prettyPrint
                     ) :
          mColorizeOutput(colorizeOutput),
          mPrettyPrint(prettyPrint)
          {}

        //---------------------------------------------------------------------
        static StdOutLoggerPtr create(
                                      bool colorizeOutput,
                                      bool prettyPrint
                                      )
        {
          StdOutLoggerPtr pThis(make_shared<StdOutLogger>(colorizeOutput, prettyPrint));
          pThis->mThisWeak = pThis;
          pThis->init();
          return pThis;
        }

        //---------------------------------------------------------------------
        static StdOutLoggerPtr singleton(
                                         bool colorizeOutput,
                                         bool prettyPrint,
                                         bool reset = false
                                         )
        {
          static LoggerSingletonLazySharedPtr< StdOutLogger > singleton;

          RecursiveLockPtr locker = LoggerSingletonLazySharedPtr< StdOutLogger >::lock(singleton);
          LoggerReferenceHolder<StdOutLogger>::HolderPtr holder = (LoggerSingletonLazySharedPtr< StdOutLogger >::logger(singleton));
          StdOutLoggerPtr &logger = holder->mLogger;

          AutoRecursiveLock lock(*locker);
          if ((reset) &&
              (logger)) {
            Log::removeListener(logger->mThisWeak.lock());
            logger.reset();
          }

          if (!logger) {
            logger = StdOutLogger::create(colorizeOutput, prettyPrint);
          }
          return logger;
        }

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark StdOutLogger => ILogDelegate
        #pragma mark

        virtual void onNewSubsystem(Subsystem &)
        {
        }

        //---------------------------------------------------------------------
        // notification of a log event
        virtual void onLog(
                           const Subsystem &inSubsystem,
                           Log::Severity inSeverity,
                           Log::Level inLevel,
                           CSTR inFunction,
                           CSTR inFilePath,
                           ULONG inLineNumber,
                           const Log::Params &params
                           )
        {
          if (mColorizeOutput) {
            std::cout << toColorString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint);
            std::cout.flush();
          } else {
            std::cout << toBWString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint);
            std::cout.flush();
          }
        }

      private:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark StdOutLogger => (data)
        #pragma mark

        StdOutLoggerWeakPtr mThisWeak;
        bool mColorizeOutput;
        bool mPrettyPrint;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark FileLogger
      #pragma mark

      ZS_DECLARE_CLASS_PTR(FileLogger)

      class FileLogger : public ILogDelegate
      {
        //---------------------------------------------------------------------
        void init(const char *fileName)
        {
          mFile.open(fileName, std::ios::out | std::ios::binary);
          Log::addListener(mThisWeak.lock());
        }

      public:
        //---------------------------------------------------------------------
        FileLogger(bool colorizeOutput) :
          mColorizeOutput(colorizeOutput),
          mPrettyPrint(colorizeOutput)
          {}

        //---------------------------------------------------------------------
        static FileLoggerPtr create(const char *fileName, bool colorizeOutput)
        {
          FileLoggerPtr pThis(make_shared<FileLogger>(colorizeOutput));
          pThis->mThisWeak = pThis;
          pThis->init(fileName);
          return pThis;
        }

        //---------------------------------------------------------------------
        static FileLoggerPtr singleton(const char *fileName, bool colorizeOutput, bool reset = false)
        {
          static LoggerSingletonLazySharedPtr< FileLogger > singleton;

          RecursiveLockPtr locker = LoggerSingletonLazySharedPtr< FileLogger >::lock(singleton);
          LoggerReferenceHolder<FileLogger>::HolderPtr holder = (LoggerSingletonLazySharedPtr< FileLogger >::logger(singleton));
          FileLoggerPtr &logger = holder->mLogger;

          AutoRecursiveLock lock(*locker);
          if ((reset) &&
              (logger)) {
            Log::removeListener(logger->mThisWeak.lock());
            logger.reset();
          }
          if (!logger) {
            logger = FileLogger::create(fileName, colorizeOutput);
          }
          return logger;
        }

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark FileLogger => ILogDelegate
        #pragma mark

        virtual void onNewSubsystem(Subsystem &)
        {
        }

        //---------------------------------------------------------------------
        // notification of a log event
        virtual void onLog(
                           const Subsystem &inSubsystem,
                           Log::Severity inSeverity,
                           Log::Level inLevel,
                           CSTR inFunction,
                           CSTR inFilePath,
                           ULONG inLineNumber,
                           const Log::Params &params
                           )
        {
          if (mFile.is_open()) {
            String output;
            if (mColorizeOutput) {
              output = toColorString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint);
            } else {
              output = toBWString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint);
            }
            mFile << output;
            mFile.flush();
          }
        }

      private:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark FileLogger => (data)
        #pragma mark

        FileLoggerWeakPtr mThisWeak;
        bool mColorizeOutput;
        bool mPrettyPrint;

        std::ofstream mFile;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DebuggerLogger
      #pragma mark

      ZS_DECLARE_CLASS_PTR(DebuggerLogger)

      class DebuggerLogger : public ILogDelegate
      {
        //---------------------------------------------------------------------
        void init()
        {
          Log::addListener(mThisWeak.lock());
        }

      public:
        //---------------------------------------------------------------------
        DebuggerLogger(bool colorizeOutput) :
          mColorizeOutput(colorizeOutput),
          mPrettyPrint(colorizeOutput)
          {}

        //---------------------------------------------------------------------
        static DebuggerLoggerPtr create(bool colorizeOutput)
        {
          DebuggerLoggerPtr pThis(make_shared<DebuggerLogger>(colorizeOutput));
          pThis->mThisWeak = pThis;
          pThis->init();
          return pThis;
        }

        //---------------------------------------------------------------------
        static DebuggerLoggerPtr singleton(bool colorizeOutput, bool reset = false)
        {
#if (defined(_WIN32)) || ((defined(__QNX__) && (!defined(NDEBUG))))
          static LoggerSingletonLazySharedPtr< DebuggerLogger > singleton;

          RecursiveLockPtr locker = LoggerSingletonLazySharedPtr< DebuggerLogger >::lock(singleton);
          LoggerReferenceHolder<DebuggerLogger>::HolderPtr holder = (LoggerSingletonLazySharedPtr< DebuggerLogger >::logger(singleton));
          DebuggerLoggerPtr &logger = holder->mLogger;

          AutoRecursiveLock lock(*locker);
          if ((reset) &&
              (logger)) {
            Log::removeListener(logger->mThisWeak.lock());
            logger.reset();
          }
          if (!logger) {
            logger = DebuggerLogger::create(colorizeOutput);
          }
          return logger;
#else
          return DebuggerLoggerPtr();
#endif //_WIN32
        }

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark DebuggerLogger => ILogDelegate
        #pragma mark

        //---------------------------------------------------------------------
        virtual void onNewSubsystem(Subsystem &)
        {
        }

        //---------------------------------------------------------------------
        // notification of a log event
        virtual void onLog(
                           const Subsystem &inSubsystem,
                           Log::Severity inSeverity,
                           Log::Level inLevel,
                           CSTR inFunction,
                           CSTR inFilePath,
                           ULONG inLineNumber,
                           const Log::Params &params
                           )
        {
#ifdef __QNX__
#ifndef NDEBUG
          String output;
          if (mColorizeOutput)
            output = toColorString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint, false);
          else
            output = toBWString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint, false);
          qDebug() << output.c_str();
#endif //ndef NDEBUG
#endif //__QNX__
          String output = toWindowsString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint);
#ifdef _WIN32
          OutputDebugStringW(output.wstring().c_str());
#endif //_WIN32
          EventWriteOpServicesDebugLogger(inSubsystem.getName(), Log::toString(inSeverity), Log::toString(inLevel), inFunction, inFilePath, inLineNumber, output);
        }

      private:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark DebuggerLogger => (data)
        #pragma mark

        DebuggerLoggerWeakPtr mThisWeak;
        bool mColorizeOutput;
        bool mPrettyPrint;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark TelnetLogger
      #pragma mark

      ZS_DECLARE_CLASS_PTR(TelnetLogger)

      class TelnetLogger : public ILogDelegate,
                           public MessageQueueAssociator,
                           public ISocketDelegate,
                           public IDNSDelegate,
                           public ITimerDelegate,
                           public IWakeDelegate,
                           public IBackgroundingDelegate
      {
        //---------------------------------------------------------------------
        void init(
                  USHORT listenPort,
                  ULONG maxSecondsWaitForSocketToBeAvailable
                  )
        {
          mListenPort = listenPort;
          mMaxWaitTimeForSocketToBeAvailable = Seconds(maxSecondsWaitForSocketToBeAvailable);

          mBackgroundingSubscription = IBackgrounding::subscribe(mThisWeak.lock(), ISettings::getUInt(OPENPEER_SERVICES_SETTING_TELNET_LOGGER_PHASE));

          Log::addListener(mThisWeak.lock());

          // do this from outside the stack to prevent this from happening during any kind of lock
          IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
        }

        //---------------------------------------------------------------------
        void init(
                  const char *serverHostWithPort,
                  const char *sendStringUponConnection
                  )
        {
          mOriginalStringToSendUponConnection = String(sendStringUponConnection);
          mOriginalServer = mServerLookupName = String(serverHostWithPort);

          String::size_type pos = mServerLookupName.find(":");
          if (pos != mServerLookupName.npos) {
            String portStr = mServerLookupName.substr(pos+1);
            mServerLookupName = mServerLookupName.substr(0, pos);

            try {
              mListenPort = Numeric<WORD>(portStr);
            } catch(Numeric<WORD>::ValueOutOfRange &) {
            }
          }

          if (0 == mListenPort) {
            mListenPort = OPENPEER_SERVICES_DEFAULT_OUTGOING_TELNET_PORT;
          }

          mBackgroundingSubscription = IBackgrounding::subscribe(mThisWeak.lock(), ISettings::getUInt(OPENPEER_SERVICES_SETTING_TELNET_LOGGER_PHASE));

          Log::addListener(mThisWeak.lock());

          // do this from outside the stack to prevent this from happening during any kind of lock
          IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
        }

      public:
        //---------------------------------------------------------------------
        TelnetLogger(
                     IMessageQueuePtr queue,
                     bool colorizeOutput
                     ) :
          MessageQueueAssociator(queue),
          mColorizeOutput(colorizeOutput),
          mPrettyPrint(colorizeOutput),
          mConnected(false),
          mListenPort(0),
          mMaxWaitTimeForSocketToBeAvailable(Seconds(60)),
          mBacklogDataUntil(zsLib::now() + Seconds(OPENPEER_SERVICES_MAX_TELNET_LOGGER_PENDING_CONNECTIONBACKLOG_TIME_SECONDS))
        {
          IHelper::setSocketThreadPriority();
          IHelper::setTimerThreadPriority();
        }

        //---------------------------------------------------------------------
        ~TelnetLogger()
        {
          mThisWeak.reset();
          close();
        }

        //---------------------------------------------------------------------
        bool isColourizedOutput() const
        {
          return mColorizeOutput;
        }

        //---------------------------------------------------------------------
        WORD getListenPort() const
        {
          return mListenPort;
        }

        //---------------------------------------------------------------------
        String getServer() const
        {
          return mOriginalServer;
        }

        //---------------------------------------------------------------------
        String getSendStringUponConnection() const
        {
          return mOriginalStringToSendUponConnection;
        }

        //---------------------------------------------------------------------
        void close()
        {
          AutoRecursiveLock lock(mLock);

          if (mClosed) {
            // already closed
            return;
          }

          mClosed = true;

          TelnetLoggerPtr pThis = mThisWeak.lock();
          if (pThis) {
            Log::removeListener(mThisWeak.lock());
            mThisWeak.reset();
          }

          mBufferedList.clear();
          mConnected = false;

          if (mOutgoingServerQuery) {
            mOutgoingServerQuery->cancel();
            mOutgoingServerQuery.reset();
          }

          mServers.reset();

          if (mTelnetSocket) {
            mTelnetSocket->close();
            mTelnetSocket.reset();
          }
          if (mListenSocket) {
            mListenSocket->close();
            mListenSocket.reset();
          }
          if (mRetryTimer) {
            mRetryTimer->cancel();
            mRetryTimer.reset();
          }
        }

        //---------------------------------------------------------------------
        bool isClosed()
        {
          AutoRecursiveLock lock(mLock);
          return mClosed;
        }

        //---------------------------------------------------------------------
        bool isListening()
        {
          AutoRecursiveLock lock(mLock);
          return (bool)mListenSocket;
        }

        //---------------------------------------------------------------------
        bool isConnected()
        {
          AutoRecursiveLock lock(mLock);
          if (!mTelnetSocket) return false;
          if (isOutgoing()) return mConnected;
          return true;
        }

        //---------------------------------------------------------------------
        static TelnetLoggerPtr create(USHORT listenPort, ULONG maxSecondsWaitForSocketToBeAvailable, bool colorizeOutput)
        {
          TelnetLoggerPtr pThis(make_shared<TelnetLogger>(IHelper::getLoggerQueue(), colorizeOutput));
          pThis->mThisWeak = pThis;
          pThis->init(listenPort, maxSecondsWaitForSocketToBeAvailable);
          return pThis;
        }

        //---------------------------------------------------------------------
        static TelnetLoggerPtr create(
                                      const char *serverHostWithPort,
                                      bool colorizeOutput,
                                      const char *sendStringUponConnection
                                      )
        {
          TelnetLoggerPtr pThis(make_shared<TelnetLogger>(IHelper::getLoggerQueue(), colorizeOutput));
          pThis->mThisWeak = pThis;
          pThis->init(serverHostWithPort, sendStringUponConnection);
          return pThis;
        }

        //---------------------------------------------------------------------
        static LoggerSingletonLazySharedPtr< TelnetLogger > &privateSingletonListener()
        {
          static LoggerSingletonLazySharedPtr< TelnetLogger > singleton;
          return singleton;
        }

        //---------------------------------------------------------------------
        static LoggerSingletonLazySharedPtr< TelnetLogger > &privateSingletonOutgoing()
        {
          static LoggerSingletonLazySharedPtr< TelnetLogger > singleton;
          return singleton;
        }
        
        //---------------------------------------------------------------------
        static RecursiveLockPtr lockListener()
        {
          RecursiveLockPtr locker = LoggerSingletonLazySharedPtr< TelnetLogger >::lock(privateSingletonListener());
          return locker;
        }

        //---------------------------------------------------------------------
        static LoggerReferenceHolder<TelnetLogger>::HolderPtr singletonListener()
        {
          return (LoggerSingletonLazySharedPtr< TelnetLogger >::logger(privateSingletonListener()));
        }

        //---------------------------------------------------------------------
        static RecursiveLockPtr lockOutgoing()
        {
          RecursiveLockPtr locker = LoggerSingletonLazySharedPtr< TelnetLogger >::lock(privateSingletonOutgoing());
          return locker;
        }

        //---------------------------------------------------------------------
        static LoggerReferenceHolder<TelnetLogger>::HolderPtr singletonOutgoing()
        {
          return (LoggerSingletonLazySharedPtr< TelnetLogger >::logger(privateSingletonOutgoing()));
        }

        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TelnetLogger => ILogDelegate
        #pragma mark

        //---------------------------------------------------------------------
        virtual void onNewSubsystem(Subsystem &)
        {
        }

        //---------------------------------------------------------------------
        virtual void onLog(
                           const Subsystem &inSubsystem,
                           Log::Severity inSeverity,
                           Log::Level inLevel,
                           CSTR inFunction,
                           CSTR inFilePath,
                           ULONG inLineNumber,
                           const Log::Params &params
                           )
        {
          if (0 == strcmp(inSubsystem.getName(), "zsLib_socket")) {
            // ignore events from the socket monitor to prevent recursion
            return;
          }

          // scope: quick exit is not logging
          {
            AutoRecursiveLock lock(mLock);

            if (!isConnected()) {
              Time tick = zsLib::now();

              if (tick > mBacklogDataUntil) {
                // clear out any pending data since we can't backlog any longer
                mBufferedList.clear();
                return;
              }
            }
          }

          String output;
          if (mColorizeOutput) {
            output = toColorString(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber, mPrettyPrint);
          } else {
            output = toRawJSON(inSubsystem, inSeverity, inLevel, params, inFunction, inFilePath, inLineNumber);
          }

          AutoRecursiveLock lock(mLock);

          bool okayToSend = (isConnected()) && (mBufferedList.size() < 1);
          size_t sent = 0;

          if (okayToSend) {
            int errorCode = 0;
            bool wouldBlock = false;
            sent = mTelnetSocket->send((const BYTE *)(output.c_str()), output.length(), &wouldBlock, 0, &errorCode);
            if (!wouldBlock) {
              if (0 != errorCode) {
                onException(mTelnetSocket);
                return;
              }
            }
          }

          if (sent < output.length()) {
            // we need to buffer the data for later...
            size_t length = (output.length() - sent);
            BufferedData data;
            std::shared_ptr<BYTE> buffer(new BYTE[length], std::default_delete<BYTE[]>() );
            memcpy(buffer.get(), output.c_str() + sent, length);

            data.first = buffer;
            data.second = length;

            mBufferedList.push_back(data);
          }
        }

        //---------------------------------------------------------------------
        virtual void onReadReady(SocketPtr inSocket)
        {
          AutoRecursiveLock lock(mLock);

          if (inSocket == mListenSocket) {
            if (mTelnetSocket) {
              mTelnetSocket->close();
              mTelnetSocket.reset();
            }

            IPAddress ignored;
            int noThrowError = 0;
            mTelnetSocket = mListenSocket->accept(ignored, NULL, &noThrowError);
            if (!mTelnetSocket)
              return;

            try {
#ifndef __QNX__
              mTelnetSocket->setOptionFlag(Socket::SetOptionFlag::IgnoreSigPipe, true);
#endif //ndef __QNX__
            } catch(Socket::Exceptions::UnsupportedSocketOption &) {
            }

            mTelnetSocket->setOptionFlag(Socket::SetOptionFlag::NonBlocking, true);
            mTelnetSocket->setDelegate(mThisWeak.lock());
          }

          if (inSocket == mTelnetSocket) {
            char buffer[1024+1];
            memset(&(buffer[0]), 0, sizeof(buffer));
            size_t length = 0;

            bool wouldBlock = false;
            int errorCode = 0;
            length = mTelnetSocket->receive((BYTE *)(&(buffer[0])), sizeof(buffer)-sizeof(buffer[0]), &wouldBlock, 0, &errorCode);

            if (wouldBlock)
              return;

            if ((length < 1) ||
                (0 != errorCode)) {
              onException(inSocket);
              return;
            }

            mCommand += (CSTR)(&buffer[0]);
            if (mCommand.size() > (sizeof(buffer)*3)) {
              mCommand.clear();
            }
            while (true) {
              const char *posLineFeed = strchr(mCommand, '\n');
              const char *posCarrageReturn = strchr(mCommand, '\r');

              if ((NULL == posLineFeed) &&
                  (NULL == posCarrageReturn)) {
                return;
              }

              if (NULL == posCarrageReturn)
                posCarrageReturn = posLineFeed;
              if (NULL == posLineFeed)
                posLineFeed = posCarrageReturn;

              if (posCarrageReturn < posLineFeed)
                posLineFeed = posCarrageReturn;

              String command = mCommand.substr(0, (posLineFeed - mCommand.c_str()));
              mCommand = mCommand.substr((posLineFeed - mCommand.c_str()) + 1);

              if (command.size() > 0) {
                handleCommand(command);
              }
            }
          }
        }


        //---------------------------------------------------------------------
        virtual void onWriteReady(SocketPtr socket)
        {
          AutoRecursiveLock lock(mLock);
          if (socket != mTelnetSocket) return;

          if (isOutgoing()) {
            if (!mConnected) {
              mConnected = true;
              IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
            }
          }

          if (!mStringToSendUponConnection.isEmpty()) {

            size_t length = mStringToSendUponConnection.length();

            BufferedData data;
            std::shared_ptr<BYTE> buffer(new BYTE[length], std::default_delete<BYTE[]>());
            memcpy(buffer.get(), mStringToSendUponConnection.c_str(), length);

            data.first = buffer;
            data.second = length;

            mBufferedList.push_front(data);

            mStringToSendUponConnection.clear();
          }

          while (mBufferedList.size() > 0) {
            BufferedData &data = mBufferedList.front();
            bool wouldBlock = false;
            size_t sent = 0;

            int errorCode = 0;
            sent = mTelnetSocket->send(data.first.get(), data.second, &wouldBlock, 0, &errorCode);
            if (!wouldBlock) {
              if (0 != errorCode) {
                onException(socket);
                return;
              }
            }

            if (sent == data.second) {
              mBufferedList.pop_front();
              continue;
            }

            size_t length = (data.second - sent);
            memmove(data.first.get(), data.first.get() + sent, length);
            data.second = length;
            break;
          }
        }

        //---------------------------------------------------------------------
        virtual void onException(SocketPtr inSocket)
        {
          AutoRecursiveLock lock(mLock);
          if (inSocket == mListenSocket) {
            mListenSocket->close();
            mListenSocket.reset();

            handleConnectionFailure();
          }

          if (inSocket == mTelnetSocket) {
            mBufferedList.clear();
            mConnected = false;

            if (mTelnetSocket) {
              mTelnetSocket->close();
              mTelnetSocket.reset();
            }

            handleConnectionFailure();
          }

          IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
        }

        //---------------------------------------------------------------------
        virtual void onLookupCompleted(IDNSQueryPtr query)
        {
          AutoRecursiveLock lock(mLock);

          if (query != mOutgoingServerQuery) return;

          IDNS::AResult::IPAddressList list;
          IDNS::AResultPtr resultA = query->getA();
          if (resultA) {
            list = resultA->mIPAddresses;
          }
          IDNS::AAAAResultPtr resultAAAA = query->getAAAA();
          if (resultAAAA) {
            if (list.size() < 1) {
              list = resultAAAA->mIPAddresses;
            } else if (resultAAAA->mIPAddresses.size() > 0) {
              list.merge(resultAAAA->mIPAddresses);
            }
          }

          mOutgoingServerQuery.reset();

          if (list.size() > 0) {
            mServers = IDNS::convertIPAddressesToSRVResult("logger", "tcp", list, mListenPort);
          } else {
            handleConnectionFailure();
          }

          IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
        }

        //---------------------------------------------------------------------
        virtual void onWake()
        {
          step();
        }

        //---------------------------------------------------------------------
        virtual void onTimer(TimerPtr timer)
        {
          step();
        }

        //---------------------------------------------------------------------
        virtual void onBackgroundingGoingToBackground(
                                                      IBackgroundingSubscriptionPtr subscription,
                                                      IBackgroundingNotifierPtr notifier
                                                      ) {}

        //---------------------------------------------------------------------
        virtual void onBackgroundingGoingToBackgroundNow(
                                                         IBackgroundingSubscriptionPtr subscription
                                                         ) {}

        //---------------------------------------------------------------------
        virtual void onBackgroundingReturningFromBackground(
                                                            IBackgroundingSubscriptionPtr subscription
                                                            )
        {
          SocketPtr socket;
          {
            AutoRecursiveLock lock(mLock);
            socket = mTelnetSocket;
          }

          if (!socket) return;

          // fake a read ready to retest socket
          onReadReady(mTelnetSocket);
        }

        //---------------------------------------------------------------------
        virtual void onBackgroundingApplicationWillQuit(
                                                        IBackgroundingSubscriptionPtr subscription
                                                        )
        {
          close();
        }

      protected:

        //---------------------------------------------------------------------
        bool isOutgoing() const
        {
          return mOriginalServer.hasData();
        }

        //---------------------------------------------------------------------
        bool isIncoming() const
        {
          return mOriginalServer.isEmpty();
        }

        //---------------------------------------------------------------------
        void handleCommand(String command)
        {
          String input = command;

          typedef std::list<String> StringList;
          StringList split;

          // split the command by the space character...
          while (true) {
            const char *posSpace = strchr(command, ' ');
            if (NULL == posSpace) {
              if (command.size() > 0) {
                split.push_back(command);
              }
              break;
            }
            String sub = command.substr(0, (posSpace - command.c_str()));
            command = command.substr((posSpace - command.c_str()) + 1);

            if (sub.size() > 0) {
              split.push_back(sub);
            }
          }

          bool output = false;
          String subsystem;
          String level;
          String echo;

          if (split.size() > 0) {
            command = split.front(); split.pop_front();
            if ((command == "set") && (split.size() > 0)) {
              command = split.front(); split.pop_front();
              if ((command == "log") && (split.size() > 0)) {
                level = split.front(); split.pop_front();
                output = true;
                if (level == "insane") {
                  ILogger::setLogLevel(Log::Insane);
                } else if (level == "trace") {
                  ILogger::setLogLevel(Log::Trace);
                } else if (level == "debug") {
                  ILogger::setLogLevel(Log::Debug);
                } else if (level == "detail") {
                  ILogger::setLogLevel(Log::Detail);
                } else if (level == "basic") {
                  ILogger::setLogLevel(Log::Basic);
                } else if (level == "none") {
                  ILogger::setLogLevel(Log::None);
                } else if (level == "pretty") {
                  String mode = split.front(); split.pop_front();
                  if (mode == "on") {
                    mPrettyPrint = true;
                    echo = "==> Setting pretty print on\n";
                  } else if (mode == "off") {
                    mPrettyPrint = false;
                    echo = "==> Setting pretty print off\n";
                  }
                } else if ((level == "color") || (level == "colour")) {
                  String mode = split.front(); split.pop_front();
                  if (mode == "on") {
                    mColorizeOutput = true;
                    echo = "==> Setting colourization on\n";
                  } else if (mode == "off") {
                    mColorizeOutput = false;
                    echo = "==> Setting colourization off\n";
                  }
                } else if (split.size() > 0) {
                  subsystem = level;
                  level = split.front(); split.pop_front();
                  if (level == "insane") {
                    ILogger::setLogLevel(subsystem, Log::Insane);
                  } else if (level == "trace") {
                    ILogger::setLogLevel(subsystem, Log::Trace);
                  } else if (level == "debug") {
                    ILogger::setLogLevel(subsystem, Log::Debug);
                  } else if (level == "detail") {
                    ILogger::setLogLevel(subsystem, Log::Detail);
                  } else if (level == "basic") {
                    ILogger::setLogLevel(subsystem, Log::Basic);
                  } else if (level == "none") {
                    ILogger::setLogLevel(subsystem, Log::None);
                  } else {
                    output = false;
                  }
                } else {
                  output = false;
                }
              }
            }
          }

          if (echo.isEmpty()) {
            if (output) {
              if (subsystem.size() > 0) {
                echo = "==> Setting log level for \"" + subsystem + "\" to \"" + level + "\"\n";
              } else {
                echo = "==> Setting all log compoment levels to \"" + level + "\"\n";
              }
            } else {
              echo = "==> Command not recognized \"" + input + "\"\n";
            }
          }
          bool wouldBlock = false;
          int errorCode = 0;
          mTelnetSocket->send((const BYTE *)(echo.c_str()), echo.length(), &wouldBlock, 0, &errorCode);
        }

        //---------------------------------------------------------------------
        void handleConnectionFailure()
        {
          AutoRecursiveLock lock(mLock);

          if (!mRetryTimer) {
            // offer a bit of buffering
            mBacklogDataUntil = zsLib::now() + Seconds(OPENPEER_SERVICES_MAX_TELNET_LOGGER_PENDING_CONNECTIONBACKLOG_TIME_SECONDS);

            mRetryWaitTime = Seconds(1);
            mNextRetryTime = zsLib::now();
            mRetryTimer = Timer::create(mThisWeak.lock(), Seconds(1));
            return;
          }

          mNextRetryTime = zsLib::now() + mRetryWaitTime;
          mRetryWaitTime = mRetryWaitTime + mRetryWaitTime;
          if (mRetryWaitTime > Seconds(60)) {
            mRetryWaitTime = Seconds(60);
          }
        }

        //---------------------------------------------------------------------
        void step()
        {
          if (isClosed()) return;

          {
            AutoRecursiveLock lock(mLock);

            if (Time() != mNextRetryTime) {
              if (zsLib::now() < mNextRetryTime) return;
            }
          }

          if (!isOutgoing()) {
            if (!stepListen()) goto step_cleanup;

          } else {
            if (!stepDNS()) goto step_cleanup;
            if (!stepConnect()) goto step_cleanup;
          }

          goto step_complete;

        step_complete:
          {
            AutoRecursiveLock lock(mLock);
            if (mRetryTimer) {
              mRetryTimer->cancel();
              mRetryTimer.reset();
            }
            mNextRetryTime = Time();
            mRetryWaitTime = Milliseconds();
          }

        step_cleanup:
          {
          }
        }

        //---------------------------------------------------------------------
        bool stepDNS()
        {
          String serverName;
          IDNSQueryPtr query;

          {
            AutoRecursiveLock lock(mLock);
            if (mOutgoingServerQuery) return false;

            if (mServers) return true;

            if (mServerLookupName.isEmpty()) return false;
            serverName = mServerLookupName;
          }

          // not safe to call services level method from within lock...
          query = IDNS::lookupAorAAAA(mThisWeak.lock(), serverName);

          // DNS is not created during any kind of lock at all...
          {
            AutoRecursiveLock lock(mLock);
            mOutgoingServerQuery = query;
          }

          return false;
        }

        //---------------------------------------------------------------------
        bool stepConnect()
        {
          AutoRecursiveLock lock(mLock);

          if (mTelnetSocket) {
            return isConnected();
          }

          IPAddress result;
          if (!IDNS::extractNextIP(mServers, result)) {
            mServers.reset();

            handleConnectionFailure();
            IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
            return false;
          }

          mStringToSendUponConnection = mOriginalStringToSendUponConnection;

          mTelnetSocket = Socket::createTCP();
          try {
#ifndef __QNX__
            mTelnetSocket->setOptionFlag(Socket::SetOptionFlag::IgnoreSigPipe, true);
#endif //ndef __QNX__
          } catch(Socket::Exceptions::UnsupportedSocketOption &) {
          }
          mTelnetSocket->setBlocking(false);

          bool wouldBlock = false;
          int errorCode = 0;
          mTelnetSocket->connect(result, &wouldBlock, &errorCode);
          mTelnetSocket->setDelegate(mThisWeak.lock()); // set delegate must happen after connect is issued

          if (0 != errorCode) {
            mTelnetSocket->close();
            mTelnetSocket.reset();

            handleConnectionFailure();
            IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
            return false;
          }

          return false;
        }

        //---------------------------------------------------------------------
        bool stepListen()
        {
          AutoRecursiveLock lock(mLock);

          if (mListenSocket) return true;

          if (Time() == mStartListenTime)
            mStartListenTime = zsLib::now();

          mListenSocket = Socket::createTCP();
          try {
#ifndef __QNX__
            mListenSocket->setOptionFlag(Socket::SetOptionFlag::IgnoreSigPipe, true);
#endif //ndef __QNX__
          } catch(Socket::Exceptions::UnsupportedSocketOption &) {
          }
          mListenSocket->setOptionFlag(Socket::SetOptionFlag::NonBlocking, true);

          IPAddress any = IPAddress::anyV4();
          any.setPort(mListenPort);

          int error = 0;

          std::cout << "TELNET LOGGER: Attempting to listen for client connections on port: " << mListenPort << " (start time=" << string(mStartListenTime) << ")...\n";
          mListenSocket->bind(any, &error);

          Time tick = zsLib::now();

          if (0 != error) {
            mListenSocket->close();
            mListenSocket.reset();

            if (mStartListenTime + mMaxWaitTimeForSocketToBeAvailable < tick) {
              std::cout << "TELNET LOGGER: ***ABANDONED***\n";
              close();
              return false;
            }

            handleConnectionFailure();

            std::cout << "TELNET LOGGER: Failed to listen...\n";
            IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
            return false;
          }

          std::cout << "TELNET LOGGER: Succeeded.\n\n";

          mListenSocket->listen();
          mListenSocket->setDelegate(mThisWeak.lock()); // set delegate must happen after the listen

          mStartListenTime = Time();
          return true;
        }
        
      private:
        //---------------------------------------------------------------------
        #pragma mark
        #pragma mark TelnetLogger => (data)
        #pragma mark

        mutable RecursiveLock mLock;

        TelnetLoggerWeakPtr mThisWeak;
        bool mColorizeOutput;
        bool mPrettyPrint;

        IBackgroundingSubscriptionPtr mBackgroundingSubscription;

        SocketPtr mListenSocket;
        SocketPtr mTelnetSocket;

        Time mBacklogDataUntil;

        bool mClosed {};

        String mCommand;

        typedef std::pair< std::shared_ptr<BYTE>, size_t> BufferedData;
        typedef std::list<BufferedData> BufferedDataList;

        BufferedDataList mBufferedList;

        WORD mListenPort {};
        Time mStartListenTime;
        Milliseconds mMaxWaitTimeForSocketToBeAvailable {};

        TimerPtr mRetryTimer;
        Time mNextRetryTime;
        Milliseconds mRetryWaitTime {};

        bool mConnected {};
        IDNSQueryPtr mOutgoingServerQuery;
        String mStringToSendUponConnection;

        String mServerLookupName;
        IDNS::SRVResultPtr mServers;

        String mOriginalServer;
        String mOriginalStringToSendUponConnection;
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark Logger
      #pragma mark


    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark services::ILogger
    #pragma mark

    //-------------------------------------------------------------------------
    void ILogger::installStdOutLogger(bool colorizeOutput)
    {
      internal::StdOutLogger::singleton(colorizeOutput, true);
    }

    //-------------------------------------------------------------------------
    void ILogger::installFileLogger(const char *fileName, bool colorizeOutput)
    {
      internal::FileLogger::singleton(fileName, colorizeOutput);
    }

    //-------------------------------------------------------------------------
    void ILogger::installTelnetLogger(WORD listenPort, ULONG maxSecondsWaitForSocketToBeAvailable,  bool colorizeOutput)
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockListener());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonListener();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (singleton) {
        bool change = false;

        change = change || (colorizeOutput != singleton->isColourizedOutput());
        change = change || (listenPort != singleton->getListenPort());

        change = change || (singleton->isClosed());

        if (!change) return;

        singleton->close();
        singleton.reset();
      }

      singleton = internal::TelnetLogger::create(listenPort, maxSecondsWaitForSocketToBeAvailable, colorizeOutput);
    }

    //-------------------------------------------------------------------------
    void ILogger::installOutgoingTelnetLogger(
                                              const char *serverHostWithPort,
                                              bool colorizeOutput,
                                              const char *sendStringUponConnection
                                              )
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockOutgoing());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonOutgoing();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (singleton) {
        bool change = false;

        change = change || (colorizeOutput != singleton->isColourizedOutput());
        change = change || (String(serverHostWithPort) != singleton->getServer());
        change = change || (String(sendStringUponConnection) != singleton->getSendStringUponConnection());

        change = change || (singleton->isClosed());

        if (!change) return;

        singleton->close();
        singleton.reset();
      }
      singleton = internal::TelnetLogger::create(serverHostWithPort, colorizeOutput, sendStringUponConnection);
    }

    //-------------------------------------------------------------------------
    void ILogger::installDebuggerLogger(bool colorizeOutput)
    {
      internal::DebuggerLogger::singleton(colorizeOutput);
    }

    //-------------------------------------------------------------------------
    bool ILogger::isTelnetLoggerListening()
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockListener());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonListener();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (!singleton) return false;

      return singleton->isListening();
    }

    //-------------------------------------------------------------------------
    bool ILogger::isTelnetLoggerConnected()
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockListener());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonListener();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (!singleton) return false;

      return singleton->isConnected();
    }

    //-------------------------------------------------------------------------
    bool ILogger::isOutgoingTelnetLoggerConnected()
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockOutgoing());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonOutgoing();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (!singleton) return false;

      return singleton->isConnected();
    }

    //-------------------------------------------------------------------------
    void ILogger::uninstallStdOutLogger()
    {
      internal::StdOutLogger::singleton(false, false, true);
    }

    //-------------------------------------------------------------------------
    void ILogger::uninstallFileLogger()
    {
      internal::FileLogger::singleton(NULL, false, true);
    }

    //-------------------------------------------------------------------------
    void ILogger::uninstallTelnetLogger()
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockListener());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonListener();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (singleton) {
        singleton->close();
        singleton.reset();
      }
    }

    //-------------------------------------------------------------------------
    void ILogger::uninstallOutgoingTelnetLogger()
    {
      AutoRecursiveLock lock(*internal::TelnetLogger::lockOutgoing());

      internal::LoggerReferenceHolder<internal::TelnetLogger>::HolderPtr holder = internal::TelnetLogger::singletonOutgoing();
      internal::TelnetLoggerPtr &singleton = holder->mLogger;

      if (singleton) {
        singleton->close();
        singleton.reset();
      }
    }

    //-------------------------------------------------------------------------
    void ILogger::uninstallDebuggerLogger()
    {
      internal::DebuggerLogger::singleton(false, true);
    }

    //-------------------------------------------------------------------------
    void ILogger::setLogLevel(Log::Level logLevel)
    {
      internal::LogLevelLoggerPtr logger = internal::LogLevelLogger::singleton();
      if (!logger) return;
      logger->setLogLevel(logLevel);
    }

    //-------------------------------------------------------------------------
    void ILogger::setLogLevel(const char *component, Log::Level logLevel)
    {
      internal::LogLevelLoggerPtr logger = internal::LogLevelLogger::singleton();
      if (!logger) return;
      logger->setLogLevel(component, logLevel);
    }

  }
}
