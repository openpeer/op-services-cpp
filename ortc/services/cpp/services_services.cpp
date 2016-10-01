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

#include <ortc/services/services.h>
#include <ortc/services/internal/services.h>

#include <zsLib/Log.h>

namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_dns) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_http) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_ice) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_stun) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_turn) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_rudp) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_mls) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_tcp_messaging) } }
namespace openpeer { namespace services { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_transport_stream) } }
namespace openpeer { namespace services { namespace wire { ZS_IMPLEMENT_SUBSYSTEM(openpeer_services_wire) } } }


namespace openpeer
{
  namespace services
  {
    namespace internal
    {
      void initSubsystems()
      {
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_http));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_ice));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_turn));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_turn));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_rudp));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_mls));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_tcp_messaging));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services, openpeer_services_transport_stream));
        ZS_GET_SUBSYSTEM_LOG_LEVEL(ZS_GET_OTHER_SUBSYSTEM(openpeer::services::wire, openpeer_services_wire));
      }
    }
  }
}
