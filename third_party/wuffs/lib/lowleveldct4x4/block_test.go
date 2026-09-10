// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

package lowleveldct4x4

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
		0xFF, 0xFF, 0xAF, 0x40,
		0xFF, 0xEF, 0xEF, 0xFF,
		0xFF, 0x60, 0xDF, 0xFF,
		0xA0, 0x00, 0x90, 0x70,
	}

	got := src.ForwardDCT()
	want := BlockI16{
		+0x00EA, +0x0034, +0x003C, +0x005F,
		+0x0082, +0x006E, -0x0064, -0x005D,
		-0x00A3, +0x0057, -0x0034, +0x000C,
		-0x0007, +0x0013, +0x0014, +0x0019,
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
