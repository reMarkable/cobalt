// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements output formatting for the cobalt config parser.

package config_parser

import (
	"bytes"
	"config"
	"encoding/base64"
	"github.com/golang/protobuf/proto"
)

type OutputFormatter func(c *config.CobaltConfig) (outputBytes []byte, err error)

// Outputs the serialized proto.
func BinaryOutput(c *config.CobaltConfig) (outputBytes []byte, err error) {
	return proto.Marshal(c)
}

// Outputs the serialized proto base64 encoded.
func Base64Output(c *config.CobaltConfig) (outputBytes []byte, err error) {
	configBytes, err := BinaryOutput(c)
	if err != nil {
		return outputBytes, err
	}
	encoder := base64.StdEncoding
	outLen := encoder.EncodedLen(len(configBytes))

	outputBytes = make([]byte, outLen, outLen)
	encoder.Encode(outputBytes, configBytes)
	return outputBytes, nil
}

// Returns an output formatter that will output the contents of a C++ header
// file that contains a variable declaration for a string literal that contains
// the base64-encoding of the serialized proto.
//
// varName will be the name of the variable containing the base64-encoded serialized proto.
// namespace is a list of nested namespaces inside of which the variable will be defined.
func CppOutputFactory(varName string, namespace []string) OutputFormatter {
	return func(c *config.CobaltConfig) (outputBytes []byte, err error) {
		b64Bytes, err := Base64Output(c)
		if err != nil {
			return outputBytes, err
		}

		out := new(bytes.Buffer)
		out.WriteString("#pragma once\n")

		for _, name := range namespace {
			out.WriteString("namespace ")
			out.WriteString(name)
			out.WriteString(" {\n")
		}

		out.WriteString("const char ")
		out.WriteString(varName)
		out.WriteString("[] = \"")
		out.Write(b64Bytes)
		out.WriteString("\";\n")

		for _, name := range namespace {
			out.WriteString("} // ")
			out.WriteString(name)
			out.WriteString("\n")
		}
		return out.Bytes(), nil
	}
}
