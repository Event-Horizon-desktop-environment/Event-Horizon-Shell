// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

package lowleveldct2x2

import (
	"testing"
)

func diffU8(a uint8, b uint8) uint8 {
	if a < b {
		return b - a
	}
	return a - b
}

func TestRoundTrip(tt *testing.T) {
	// src is an excerpt of pjw8x8 in lowleveljpeg's unit tests.
	src := BlockU8{
		0x50, 0xBF,
		0x40, 0x00,
	}

	got := src.ForwardDCT()
	want := BlockI16{
		-0x0058, -0x0017,
		+0x0068, -0x0057,
	}
	if got != want {
		tt.Fatalf("incorrect DCT\n\n%v\n%v", got, want)
	}

	dst := got.InverseDCT()
	const toleranceU8 = 1
	for i := range src {
		if diffU8(src[i], dst[i]) > toleranceU8 {
			tt.Fatalf("difference was too big at (%d, %d)\n\n%v\n%v",
				i&3, i>>2, src, dst)
		}
	}
}
