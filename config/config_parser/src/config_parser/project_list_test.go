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

package config_parser

import (
	yaml "github.com/go-yaml/yaml"
	"reflect"
	"testing"
)

// Basic test case for parseCustomerList.
func TestParseCustomerList(t *testing.T) {
	y := `
- customer_name: fuchsia
  customer_id: 20
  projects:
  - name: ledger
    id: 1
    contact: ben
- customer_name: test_project
  customer_id: 25
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	e := []projectConfig{
		projectConfig{
			customerName: "fuchsia",
			customerId:   20,
			projectName:  "ledger",
			projectId:    1,
			contact:      "ben",
		},
		projectConfig{
			customerName: "test_project",
			customerId:   25,
			projectName:  "ledger",
			projectId:    1,
			contact:      "ben",
		},
	}

	l := []projectConfig{}
	if err := parseCustomerList(y, &l); err != nil {
		t.Error(err)
	}

	if !reflect.DeepEqual(e, l) {
		t.Errorf("%v != %v", e, l)
	}
}

// Tests that duplicated customer names and ids result in errors.
func TestParseCustomerListDuplicateValues(t *testing.T) {
	var y string
	l := []projectConfig{}

	// Checks that an error is returned if a duplicate customer name is used.
	y = `
- customer_name: fuchsia
  customer_id: 10
  projects:
  - name: ledger
    id: 1
    contact: ben
- customer_name: fuchsia
  customer_id: 11
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer list with duplicate customer names.")
	}

	// Checks that an error is returned if a duplicate customer id is used.
	y = `
- customer_name: fuchsia
  customer_id: 10
  projects:
  - name: ledger
    id: 1
    contact: ben
- customer_name: test_project
  customer_id: 10
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer list with duplicate customer ids.")
	}
}

// Tests the customer name validation logic.
func TestParseCustomerListNameValidation(t *testing.T) {
	var y string
	l := []projectConfig{}

	// Checks that an error is returned if no customer name is specified.
	y = `
- customer_id: 20
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer without a name.")
	}

	// Checks that an error is returned if the customer name has an invalid type.
	y = `
- customer_name: 20
  customer_id: 10
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with invalid name type.")
	}

	// Checks that an error is returned if a name is invalid.
	y = `
- customer_name: hello world
  customer_id: 10
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with invalid name.")
	}
}

// Tests the customer id validation logic.
func TestParseCustomerListIdValidation(t *testing.T) {
	var y string
	l := []projectConfig{}

	// Checks that an error is returned if no customer id is specified.
	y = `
- customer_name: fuchsia
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer without an id.")
	}

	// Checks that an error is returned if a non-numeric customer id is specified.
	y = `
- customer_id: fuchsia
  customer_name: fuchsia
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with non-numeric id.")
	}

	// Checks that an error is returned if a negative customer id is specified.
	y = `
- customer_id: -10
  customer_name: fuchsia
  projects:
  - name: ledger
    id: 1
    contact: ben
`

	if err := parseCustomerList(y, &l); err == nil {
		t.Error("Accepted customer with negative id.")
	}
}

// Allows tests to specify input data in yaml when testing populateProjectList.
func parseProjectListForTest(content string, l *[]projectConfig) (err error) {
	var y []interface{}
	if err := yaml.Unmarshal([]byte(content), &y); err != nil {
		panic(err)
	}

	return populateProjectList(y, l)
}

// Basic test case for populateProjectList.
func TestPopulateProjectList(t *testing.T) {
	y := `
- name: ledger
  id: 1
  contact: ben,etienne
- name: zircon
  id: 2
  contact: yvonne
`

	l := []projectConfig{}
	if err := parseProjectListForTest(y, &l); err != nil {
		t.Error(err)
	}

	e := []projectConfig{
		projectConfig{
			projectName: "ledger",
			projectId:   1,
			contact:     "ben,etienne",
		},
		projectConfig{
			projectName: "zircon",
			projectId:   2,
			contact:     "yvonne",
		},
	}
	if !reflect.DeepEqual(e, l) {
		t.Errorf("%v != %v", e, l)
	}
}

// Test duplicate project name or id validation logic.
func TestDuplicateProjectValuesValidation(t *testing.T) {
	var y string
	var l []projectConfig
	// Checks that an error is returned if a name is duplicated.
	y = `
- name: ledger
  id: 1
  contact: ben
- name: ledger
  id: 2
  contact: yvonne
`

	l = []projectConfig{}
	if err := parseProjectListForTest(y, &l); err == nil {
		t.Errorf("Accepted list with duplicate project name.")
	}

	// Checks that an error is returned if the id duplicated.
	y = `
- name: ledger
  id: 1
  contact: ben
- name: zircon
  id: 1
  contact: yvonne
`

	l = []projectConfig{}
	if err := parseProjectListForTest(y, &l); err == nil {
		t.Errorf("Accepted list with duplicate project id.")
	}
}

// Allows tests to specify inputs in yaml when testing populateProjectConfig.
func parseProjectConfigForTest(content string, c *projectConfig) (err error) {
	var y map[string]interface{}
	if err := yaml.Unmarshal([]byte(content), &y); err != nil {
		panic(err)
	}

	return populateProjectConfig(y, c)
}

// Checks validation for the name field.
func TestPopulateProjectListNameValidation(t *testing.T) {
	var y string
	var c projectConfig
	// Checks that an error is returned if a name is the wrong type.
	y = `
name: 10
id: 1
contact: ben
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with numeric name.")
	}

	// Checks that an error is returned if a name is invalid.
	y = `
name: hello world
id: 1
contact: ben
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with invalid name.")
	}

	// Checks that an error is returned if no name is provided for a project.
	y = `
id: 1
contact: ben
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project without name.")
	}
}

// Checks validation for the id field.
func TestPopulateProjectListIdValidation(t *testing.T) {
	var y string
	var c projectConfig

	// Checks that an error is returned if the id missing.
	y = `
name: ledger
contact: ben
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project without id.")
	}

	// Checks that an error is returned if the id is an invalid type.
	y = `
name: ledger
id: ledger
contact: ben
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with non-integer id.")
	}

	// Checks that an error is returned if the id is negative.
	y = `
name: ledger
id: -10
contact: ben
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with negative id.")
	}
}

// Checks validation for the contact field.
func TestPopulateProjectListContactValidation(t *testing.T) {
	var y string
	var c projectConfig

	// Checks that an error is returned if a contact is the wrong type.
	y = `
name: ledger
id: 1
contact: 10
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project with numeric contact.")
	}

	// Checks that an error is returned if a contact is missing.
	y = `
name: ledger
id: 10
`
	c = projectConfig{}
	if err := parseProjectConfigForTest(y, &c); err == nil {
		t.Errorf("Accepted project without contact.")
	}
}
