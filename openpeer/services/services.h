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

#include <openpeer/services/types.h>
#include <openpeer/services/IBackgrounding.h>
#include <openpeer/services/IBackOffTimer.h>
#include <openpeer/services/IBackOffTimerPattern.h>
#include <openpeer/services/ICache.h>
#include <openpeer/services/ICanonicalXML.h>
#include <openpeer/services/IDecryptor.h>
#include <openpeer/services/IDHKeyDomain.h>
#include <openpeer/services/IDHPrivateKey.h>
#include <openpeer/services/IDHPublicKey.h>
#include <openpeer/services/IDNS.h>
#include <openpeer/services/IEncryptor.h>
#include <openpeer/services/IHelper.h>
#include <openpeer/services/IHTTP.h>
#include <openpeer/services/IICESocket.h>
#include <openpeer/services/IICESocketSession.h>
#include <openpeer/services/ILogger.h>
#include <openpeer/services/IMessageLayerSecurityChannel.h>
#include <openpeer/services/IMessageQueueManager.h>
#include <openpeer/services/IReachability.h>
#include <openpeer/services/IRSAPrivateKey.h>
#include <openpeer/services/IRSAPublicKey.h>
#include <openpeer/services/IRUDPChannel.h>
#include <openpeer/services/IRUDPListener.h>
#include <openpeer/services/IRUDPMessaging.h>
#include <openpeer/services/IRUDPTransport.h>
#include <openpeer/services/ISettings.h>
#include <openpeer/services/ISTUNDiscovery.h>
#include <openpeer/services/ISTUNRequester.h>
#include <openpeer/services/ISTUNRequesterManager.h>
#include <openpeer/services/ITCPMessaging.h>
#include <openpeer/services/ITransportStream.h>
#include <openpeer/services/ITURNSocket.h>
#include <openpeer/services/IWakeDelegate.h>
#include <openpeer/services/STUNPacket.h>
#include <openpeer/services/RUDPPacket.h>
#include <openpeer/services/RUDPProtocol.h>
#include <openpeer/services/RUDPProtocol.h>
