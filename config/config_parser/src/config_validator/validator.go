// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
)

func formatId(customer, project, id uint32) string {
	return fmt.Sprintf("(%d, %d, %d)", customer, project, id)
}

func ValidateConfig(config *config.CobaltConfig) (err error) {
	if err = validateConfiguredMetrics(config); err != nil {
		return
	}

	if err = validateConfiguredReports(config); err != nil {
		return
	}

	if err = validateSystemProfileFields(config); err != nil {
		return
	}

	if err = runCommonValidations(config); err != nil {
		return
	}

	return nil
}
