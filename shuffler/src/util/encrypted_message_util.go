// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package util

import (
	"encoding/pem"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"

	"cobalt"
)

// This file contains two types for working with EncryptedMessages.
//
// EncryptedMessageMaker is not currently used on the Shuffler. It is included for
// completeness and for use in tests.
//
// MessageDecrypter is used by the Shuffler to decrypt EncryptedMessages
// containing Envelopes.

// EncryptedMessageMaker is used only in tests. The C++ version of EncryptedMessageMaker
// is used by the Encoder client to build EncryptedMessages to send to the Shuffler
// and the Analyzer.
type EncryptedMessageMaker struct {
	hybridCipher     *HybridCipher
	encryptionScheme cobalt.EncryptedMessage_EncryptionScheme
}

// Constructs and returns a new EncryptedMessageMaker or nil if |publicKeyPem| cannot be
// parsed.
//
// |scheme| specifies which encryption scheme should be used. As of this
// writing there are two schemes:
//   (i) EncryptedMessage_NONE means that messages will not be
//   encrypted: they will be sent in plain text. This scheme must
//   never be used in production Cobalt.
//
//   (ii) EncryptedMessage_HYBRID_ECDH_V1 indicates that version 1 of
//   Cobalt's Elliptic-Curve Diffie-Hellman-based hybrid
//   public-key/private-key encryption scheme should be used.
//
// |publicKeyPem| must be appropriate to |scheme|. If |scheme| is
// EncryptedMessage_NONE then |publicKeyPem| is ignored. If |scheme| is
// EncryptedMessage_HYBRID_ECDH_V1 then |publicKeyPem| must be a PEM
// encoding of a public key appropriate for that scheme.
func NewEncryptedMessageMaker(publicKeyPem string,
	scheme cobalt.EncryptedMessage_EncryptionScheme) *EncryptedMessageMaker {
	var cipher *HybridCipher
	if scheme == cobalt.EncryptedMessage_HYBRID_ECDH_V1 {
		block, _ := pem.Decode([]byte(publicKeyPem))
		if block == nil {
			glog.Errorln("Failed to decode publicKeyPem.")
			return nil
		}
		publicKey := block.Bytes
		cipher = NewHybridCipher(nil, publicKey)
		if cipher == nil {
			glog.Errorln("Failed to construct a HybridCipher.")
			return nil
		}
	}
	return &EncryptedMessageMaker{
		hybridCipher:     cipher,
		encryptionScheme: scheme,
	}
}

// Encrypts a protocol buffer |message|. Returns an EncryptedMessage and nil on success
// or nil and an error on failure.
func (m *EncryptedMessageMaker) Encrypt(message proto.Message) (*cobalt.EncryptedMessage, error) {
	if m == nil {
		return nil, grpc.Errorf(codes.InvalidArgument, "EncryptedMessageMaker is nil")
	}
	if message == nil {
		return nil, grpc.Errorf(codes.InvalidArgument, "message is nil")
	}
	serializedMessage, err := proto.Marshal(message)
	if err != nil {
		return nil, grpc.Errorf(codes.InvalidArgument, "message could not be serialized: %v", err)
	}

	var encryptedMessage cobalt.EncryptedMessage
	encryptedMessage.Scheme = m.encryptionScheme

	if m.encryptionScheme == cobalt.EncryptedMessage_NONE {
		encryptedMessage.Ciphertext = serializedMessage
		return &encryptedMessage, nil
	}

	if m.encryptionScheme != cobalt.EncryptedMessage_HYBRID_ECDH_V1 {
		// HYBRID_ECDH_V1 is the only other scheme we know about.
		return nil, grpc.Errorf(codes.Internal, "Unexpected encryption scheme: %v", m.encryptionScheme)
	}

	if m.hybridCipher == nil {
		return nil, grpc.Errorf(codes.Internal, "m.hybridCipher is nil")
	}
	ciphertext, err := m.hybridCipher.Encrypt(serializedMessage)
	if err != nil {
		return nil, err
	}
	encryptedMessage.Ciphertext = ciphertext
	// TODO(rudominer) Set encryptedMessage.PublicKeyFingerprint

	return &encryptedMessage, nil
}

type MessageDecrypter struct {
	hybridCipher *HybridCipher
}

// Constructs a new MessageDecrypter. If |privateKeyPem| is a valid PEM
// encoding of a private key for Cobalt's hybrid encryption scheme, then the
// resulting MessageDecrypter will be able to decrypt messages that use the
// HYBRID_ECDH_V1 scheme. Otherwise the resulting MessageDecrypter will only
// be able to decrypt EncryptedMessages that use the NONE scheme.
//
// TODO(rudominer) For key-rotation support this constructor
// should accept multiple (public, private) key pairs and use the
// fingerprint field of EncryptedMessage to select the appropriate private
// key.
func NewMessageDecrypter(privateKeyPem string) *MessageDecrypter {
	var hybridCipher *HybridCipher
	block, _ := pem.Decode([]byte(privateKeyPem))
	if block == nil {
		glog.V(1).Infoln("Failed to decode privateKeyPem.")
	} else {
		hybridCipher = NewHybridCipher(block.Bytes, nil)
	}
	return &MessageDecrypter{
		hybridCipher: hybridCipher,
	}
}

// Decrypts |encryptedMessage| and deserializes the result into the provided |outMessage|. Return a non-nil error if and only if this fails.
func (m *MessageDecrypter) DecryptMessage(encryptedMessage *cobalt.EncryptedMessage, outMessage proto.Message) error {
	if m == nil {
		return grpc.Errorf(codes.InvalidArgument, "MessageDecrypter is nil")
	}
	if encryptedMessage == nil {
		return grpc.Errorf(codes.InvalidArgument, "encrypedMessage is nil")
	}
	if outMessage == nil {
		return grpc.Errorf(codes.InvalidArgument, "outMessage is nil")
	}
	if encryptedMessage.Scheme == cobalt.EncryptedMessage_NONE {
		err := proto.Unmarshal(encryptedMessage.Ciphertext, outMessage)
		if err == nil {
			return nil
		}
		return grpc.Errorf(codes.InvalidArgument, "Unable to unmarshal encryptedMessage. Ciphertext: %v", err)
	}
	if encryptedMessage.Scheme != cobalt.EncryptedMessage_HYBRID_ECDH_V1 {
		// HYBRID_ECDH_V1 is the only other scheme we know about.
		return grpc.Errorf(codes.InvalidArgument, "Unrecognized encryption scheme specified in EncryptedMessage: %v", encryptedMessage.Scheme)
	}
	if m.hybridCipher == nil {
		return grpc.Errorf(codes.Internal, "m.hybridCipher is nil")
	}
	recoveredText, err := m.hybridCipher.Decrypt(encryptedMessage.Ciphertext)
	if err != nil {
		return grpc.Errorf(codes.InvalidArgument, "Decryption error: %v", err)
	}
	if err = proto.Unmarshal(recoveredText, outMessage); err != nil {
		return grpc.Errorf(codes.InvalidArgument, "Unable to unmarshal decrypted text: %v", err)
	}
	return nil

}
