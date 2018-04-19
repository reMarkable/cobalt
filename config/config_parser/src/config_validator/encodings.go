// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
)

func validateConfiguredEncodings(config *config.CobaltConfig) (err error) {
	// Set of encoding ids. Used detect duplicates.
	encodingIds := map[string]bool{}

	for i, encoding := range config.EncodingConfigs {
		if encoding.Id == 0 {
			return fmt.Errorf("Encoding id '0' is invalid.")
		}

		encodingKey := formatId(encoding.CustomerId, encoding.ProjectId, encoding.Id)
		if encodingIds[encodingKey] {
			return fmt.Errorf("Encoding id %s is repeated in encoding config entry number %v. Encoding ids must be unique.", encodingKey, i+1)
		}
		encodingIds[encodingKey] = true
	}

	return nil
}
