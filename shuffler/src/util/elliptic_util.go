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
	"crypto/elliptic"
	"math/big"
)

// Routines for working with Elliptic curves.
//
// The code in this file was largely cribbed from some code written by Bryan Ford
// in https://go-review.googlesource.com/c/1883/.

// Use the curve equation to calculate y² given x
func ySquared(curve *elliptic.CurveParams, x *big.Int) *big.Int {

	// y² = x³ - 3x + b
	x3 := new(big.Int).Mul(x, x)
	x3.Mul(x3, x)

	threeX := new(big.Int).Lsh(x, 1)
	threeX.Add(threeX, x)

	x3.Sub(x3, threeX)
	x3.Add(x3, curve.B)
	x3.Mod(x3, curve.P)
	return x3
}

// MarshalCompressed returns the X9.62 compressed serialization of the point
// (x, y), which must be a point on the given |curve|. Returns nil on failure.
func MarshalCompressed(curve elliptic.Curve, x, y *big.Int) []byte {
	byteLen := (curve.Params().BitSize + 7) >> 3

	ret := make([]byte, 1+byteLen)
	ret[0] = 2 // compressed point

	xBytes := x.Bytes()
	if len(xBytes) > byteLen {
		return nil
	}
	copy(ret[1+byteLen-len(xBytes):], xBytes)
	// The first byte of the encoding indicates which square root of y² y is.
	ret[0] += byte(y.Bit(0))
	return ret
}

// Unmarshal returns the point (x, y) on the given |curve| that is encoded
// by |data|, which must be either the compressed or uncompressed X9.62
// serialization of a point on the curve. Returns nil on failure.
func Unmarshal(curve elliptic.Curve, data []byte) (x, y *big.Int) {
	// If the data is uncompressed just invoke elliptic.Unmarshal.
	x, y = elliptic.Unmarshal(curve, data)
	if x != nil && y != nil {
		return
	}
	x = nil
	y = nil
	byteLen := (curve.Params().BitSize + 7) >> 3
	if len(data) != 1+byteLen {
		return
	}
	if (data[0] &^ 1) != 2 {
		return // unrecognized point encoding
	}

	// Based on Routine 2.2.4 in NIST Mathematical Routines paper
	params := curve.Params()
	tx := new(big.Int).SetBytes(data[1 : 1+byteLen])
	y2 := ySquared(params, tx)

	// Compute the square root of y2.
	ty := new(big.Int).ModSqrt(y2, params.P)
	if ty == nil {
		return // y2 is not a quadratic residue: invalid point
	}

	// data[0] encodes which of the two square roots of y2 we want.
	if ty.Bit(0) != uint(data[0]&1) {
		ty.Sub(params.P, ty)
	}

	x, y = tx, ty // valid point: return it
	return
}
