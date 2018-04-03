// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"testing"
)

func TestRunCommonValidations(t *testing.T) {
	t.Skip("TODO(azani): Re-enable when the C++ validation is reenabled.")
	config := &config.CobaltConfig{}
	if err := runCommonValidations(config); err == nil {
		t.Error("Empty config was accepted.")
	}
}
