#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import base64
from cryptography.fernet import Fernet
from Crypto.Cipher import PKCS1_OAEP
from Crypto.PublicKey import RSA
import os
import sys

import file_util

# The names of the files in the cache directory where the analyzer's public
# and private key will be stored.
ANALYZER_PUBLIC_KEY_FILE = "analyzer_public_key.pem"
ANALYZER_PRIVATE_KEY_FILE = "analyzer_private_key.pem"

class CryptoHelper(object):
  """ An object to assist in performing hybrid encryption/decription in
  the Cobalt prototype pipeline. Data is encrypted by the randomizers using
  symmetric, authenticated, non-determinsitic encryption, and then the
  symmetric key is encrypted using the public key of the analyzer. The
  sealed envelope consisting of the pair (ciphertext, encrypted_key) is sent to
  the shuffler which shuffles the sealed envelopes while being unable to
  decrypt the data. The shuffler forwards the shuffled data to the analyzer
  which decrypts the symmetric key using its private key and then uses
  the symmetric key to decrypt the paylod.

  A new symmetric key is generated for each encryption. The analyzer's public/
  private key pair is generated once per installation
  and cached in the |cache| directory. If either key file is deleted then
  both keys will be regenerated.

  The public key encryption uses the RSA encryption protocol according to
  PKCS#1 OAEP.

  The symmetric authenticated encryption uses AES 128 in CBC mode with HMAC,
  as implemented by the Fernet library from cryptography.io. Fernet uses
  encrypt-then-mac authenticated encryption.
  See https://github.com/fernet/spec/blob/master/Spec.md.

  Example:

  # In a randomizer:
  ch = CryptoHelper()
  ciphertext1, encrypted_key1 = ch.encryptForSendingToAnalyzer("Hello world!")
  ciphertext2, encrypted_key2 = ch.encryptForSendingToAnalyzer("Goodbye world!")

  # Later in an analyzer:
  ch = CryptoHelper()
  # prints "Hello world!"
  print(ch.decryptOnAnalyzer(ciphertext1, encrypted_key1))
  # prints "Goodbye world!"
  print(ch.decryptOnAnalyzer(ciphertext2, encrypted_key2))

  The ciphertexts and encrypted keys are base64 encoded so they may be
  conveniently included in a human-readable file.
  """

  def __init__(self):
    public_key_file = os.path.join(file_util.CACHE_DIR,
                                   ANALYZER_PUBLIC_KEY_FILE)
    private_key_file = os.path.join(file_util.CACHE_DIR,
                                    ANALYZER_PRIVATE_KEY_FILE)

    if (not os.path.exists(public_key_file)
        or not os.path.exists(private_key_file)):
      try:
        os.remove(public_key_file)
      except:
        pass
      try:
        os.remove(private_key_file)
      except:
        pass
      self._generateAnalyzerKeyPair()

    self._analyzer_public_key_cipher = PKCS1_OAEP.new(self._readKey(
        public_key_file))
    self._analyzer_private_key_cipher = PKCS1_OAEP.new(self._readKey(
      private_key_file))

  def _generateAnalyzerKeyPair(self):
    """ Generates a public/private key pair for the analyzer and writes
    the keys into two files in the |cache| directory.
    """
    new_key = RSA.generate(2048)

    with file_util.openFileForWriting(ANALYZER_PUBLIC_KEY_FILE,
                                      file_util.CACHE_DIR) as f1:
      f1.write(new_key.publickey().exportKey("PEM"))

    with file_util.openFileForWriting(ANALYZER_PRIVATE_KEY_FILE,
                                      file_util.CACHE_DIR) as f2:
      f2.write(new_key.exportKey("PEM"))

  def _readKey(self, file_name):
    """ Reads and returns a key from the specified file.

    Args:
      file_name {string} The full path of a key file to read.

    Returns:
      An RSA key object based on the data read from the file.
    """
    with open(file_name) as f:
      return RSA.importKey(f.read())

  def encryptForSendingToAnalyzer(self, plaintext):
    """Encrypts plaintext for sending to the analyzer.

    A new symmetric key is generated and then the plaintext is encrypted
    into a ciphertext using this key. Then the symmetric key is encrypted
    using the analyzer's public key. The pair consisting of the ciphertext
    and the encrypted symmetric key is returned.

    This function should be invoked from a randomizer.

    Args:
      plaintext {string} The plain text to encrypt.

    Returns:
      {pair of string} (ciphertext, encrypted_key) Both values are base64
      encoded.
    """
    symmetric_key = Fernet.generate_key()
    symmetric_encryption = Fernet(symmetric_key)
    ciphertext = symmetric_encryption.encrypt(plaintext)
    encrypted_key = base64.b64encode(self._analyzer_public_key_cipher.encrypt(
      symmetric_key))
    return (ciphertext, encrypted_key)

  def decryptOnAnalyzer(self, ciphtertext, encrypted_key):
    """ Decrypts the ciphertext on the analyzer.

    First the encrypted_key is decrypted using the analyzer's private key.
    Then the ciphertext is decrypted using the decrypted symmetric key.

    This function should be invoked from an analyzer.

    Args:
      ciphertext: {string} Ciphertext to decrypt
      encrypted_key: {string} The encryption of the symmetric key.


    Returns:
      {string} The decrypted plaintext.
    """
    symmetric_key = self._analyzer_private_key_cipher.decrypt(
        base64.b64decode(encrypted_key))
    symmetric_encryption = Fernet(symmetric_key)
    return symmetric_encryption.decrypt(ciphtertext)

def main():
  # This main() function is a manual test of the code in this file.
  ch = CryptoHelper()
  for i in xrange(10):
    plain_text = "message number %i" % i
    cipher_text, encrypted_key = ch.encryptForSendingToAnalyzer(plain_text)
    print("_______________________")
    print "cipher_text=%s"%cipher_text
    print("_______________________")
    print "encrypted_key=%s"%encrypted_key
    print("_______________________")
    print(ch.decryptOnAnalyzer(cipher_text, encrypted_key))

  ch = CryptoHelper()
  for i in xrange(10):
    plain_text = "message number %i" % i
    cipher_text, encrypted_key = ch.encryptForSendingToAnalyzer(plain_text)
    print(ch.decryptOnAnalyzer(cipher_text, encrypted_key))

  plain_text = "A very long message " * 100
  cipher_text, encrypted_key = ch.encryptForSendingToAnalyzer(plain_text)
  ch = CryptoHelper()
  print(ch.decryptOnAnalyzer(cipher_text, encrypted_key))

if __name__ == '__main__':
  sys.exit(main())

