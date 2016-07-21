import base64
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
  """ An object to assist in performing public key encryption/decription in
  the Cobalt prototype pipeline. Data is encrypted by the randomizers using
  the public key of the analyzer and decrypted by the analyzer using the
  analyzer's private key. The keys are generated once per installation
  and cached in the |cache| directory. If either key file is deleted then
  both keys will be regenerated.

  Uses the RSA encryption protocol according to PKCS#1 OAEP.

  Example:

  # In a randomizer:
  ch = CryptoHelper()
  ciphertext1 = ch.encryptForSendingToAnalyzer("Hello world!")
  ciphertext2 = ch.encryptForSendingToAnalyzer("Goodbye world!")

  # Later in an analyzer:
  ch = CryptoHelper()
  print(ch.decryptOnAnalyzer(ciphertext1)) # prints "Hello world!"
  print(ch.decryptOnAnalyzer(ciphertext2)) # prints "Goodbye world!"

  The ciphertexts are base64 encoded so they may be conveniently included
  in a human-readable file.
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
    """ Encrypts the plain text using the analyzer's public key.
    This function should be invoked from a randomizer. The returned value
    is a base64 encoding of the bytes of the ciphertext.

    Args:
      plaintext {string} The plain text to encrypt.

    Returns:
      {string} The base64 encoding of the encrypted ciphertext
    """
    return base64.b64encode(self._analyzer_public_key_cipher.encrypt(plaintext))

  def decryptOnAnalyzer(self, ciphtertext):
    """ Decrypts the ciphertext using the analyzer's private key.
    This function should be invoked from an analyzer.

    Args:
      ciphertext: {string} The base64 encoding of the ciphertext to decrypt.

    Returns:
      {string} The decrypted plaintext.
    """
    return self._analyzer_private_key_cipher.decrypt(
        base64.b64decode(ciphtertext))

def main():
  # This main() function is a manual test of the code in this file.
  ch = CryptoHelper()
  for i in xrange(10):
    plain_text = "message number %i" % i
    cipher_text = ch.encryptForSendingToAnalyzer(plain_text)
    print cipher_text
    print(ch.decryptOnAnalyzer(cipher_text))

  ch = CryptoHelper()
  for i in xrange(10):
    plain_text = "message number %i" % i
    cipher_text = ch.encryptForSendingToAnalyzer(plain_text)
    print(ch.decryptOnAnalyzer(cipher_text))

if __name__ == '__main__':
  sys.exit(main())

