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

#include <ortc/services/internal/services_Decryptor.h>

#include <cryptopp/modes.h>
#include <cryptopp/aes.h>

namespace ortc { namespace services { ZS_DECLARE_SUBSYSTEM(org_ortc_services) } }

namespace ortc
{
  namespace services
  {
    namespace internal
    {
      using CryptoPP::CFB_Mode;
      using CryptoPP::AES;

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //
      // EncryptorData
      //

      struct DecryptorData
      {
        CFB_Mode<AES>::Decryption decryptor;

        DecryptorData(
                      const BYTE *key,
                      size_t keySize,
                      const BYTE *iv
                      ) noexcept :
          decryptor(key, keySize, iv)
        {
        }
      };

      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //-----------------------------------------------------------------------
      //
      // Encryptor
      //

      //-----------------------------------------------------------------------
      Decryptor::Decryptor(
                           const make_private &,
                           const SecureByteBlock &key,
                           const SecureByteBlock &iv,
                           ZS_MAYBE_USED() EncryptionAlgorthms algorithm
                           ) noexcept
      {
        ZS_MAYBE_USED(algorithm);
        mData = new DecryptorData(key, key.SizeInBytes(), iv);
      }

      //-----------------------------------------------------------------------
      Decryptor::~Decryptor()
      {
        delete mData;
        mData = NULL;
      }

      //-----------------------------------------------------------------------
      DecryptorPtr Decryptor::create(
                                     const SecureByteBlock &key,
                                     const SecureByteBlock &iv,
                                     EncryptionAlgorthms algorithm
                                     ) noexcept
      {
        DecryptorPtr pThis(make_shared<Decryptor>(make_private {}, key, iv, algorithm));
        return pThis;
      }

      //-----------------------------------------------------------------------
      size_t Decryptor::getOptimalBlockSize() const noexcept
      {
        return mData->decryptor.OptimalBlockSize();
      }

      //-----------------------------------------------------------------------
      SecureByteBlockPtr Decryptor::decrypt(
                                            const BYTE *inBuffer,
                                            size_t inBufferSizeInBytes
                                            ) noexcept
      {
        SecureByteBlockPtr output(make_shared<SecureByteBlock>(inBufferSizeInBytes));
        mData->decryptor.ProcessData(*output, inBuffer, inBufferSizeInBytes);
        return output;
      }

      //-----------------------------------------------------------------------
      SecureByteBlockPtr Decryptor::decrypt(const SecureByteBlock &input) noexcept
      {
        SecureByteBlockPtr output(make_shared<SecureByteBlock>(input.SizeInBytes()));
        mData->decryptor.ProcessData(*output, input.BytePtr(), input.SizeInBytes());
        return output;
      }

      //-----------------------------------------------------------------------
      SecureByteBlockPtr Decryptor::finalize(bool *outWasSuccessful) noexcept
      {
        if (outWasSuccessful) {
          *outWasSuccessful = true;
        }
        return SecureByteBlockPtr();
      }
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //
    // IEncryptor
    //

    //-------------------------------------------------------------------------
    IDecryptorPtr IDecryptor::create(
                                     const SecureByteBlock &key,
                                     const SecureByteBlock &iv,
                                     EncryptionAlgorthms algorithm
                                     ) noexcept
    {
      return internal::Decryptor::create(key, iv, algorithm);
    }
  }
}
