// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

package parsecolor

import (
	"image/color"
	"testing"
)

func TestParse(tt *testing.T) {
	testCases := []struct {
		wantColor color.NRGBA
		wantOK    bool
		input     string
	}{
		{color.NRGBA{0x00, 0x00, 0x00, 0xff}, true, "black"},
		{color.NRGBA{0xff, 0xff, 0xff, 0xff}, true, "white"},
		{color.NRGBA{0xf4, 0x43, 0x36, 0xff}, true, "red"},
		{color.NRGBA{0x7a, 0x21, 0x1b, 0xff}, true, "darkred"},
		{color.NRGBA{0xfa, 0xa1, 0x9b, 0xff}, true, "lightred"},
		{color.NRGBA{0x26, 0x57, 0x28, 0xff}, true, "darkgreen"},
		{color.NRGBA{0xff, 0x98, 0x00, 0xff}, true, "orange"},
		{color.NRGBA{0xcf, 0xcf, 0xcf, 0xff}, true, "lightgray"},
		{color.NRGBA{0x9e, 0x9e, 0x9e, 0xff}, true, "grey"},
		{color.NRGBA{0x9c, 0x27, 0xb0, 0xff}, true, "magenta"},

		{color.NRGBA{0xab, 0xc1, 0x23, 0xff}, true, "#abc123"},
		{color.NRGBA{0xab, 0xc1, 0x23, 0xff}, true, "abc123"},
		{color.NRGBA{0xc1, 0x23, 0x45, 0xab}, true, "abc12345"},

		{color.NRGBA{0x77, 0x88, 0x99, 0xff}, true, "789"},
		{color.NRGBA{0x88, 0x99, 0xaa, 0x77}, true, "789a"},

		{color.NRGBA{0x00, 0x00, 0x00, 0xff}, true, "000"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, true, "0000"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, true, "none"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, true, "transparent"},

		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, false, "#asdf"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, false, "darkabc123"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, false, "dark#abc123"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, false, "deadchannel"},
		{color.NRGBA{0x00, 0x00, 0x00, 0x00}, false, "thetruthofthematter"},
	}

	for _, tc := range testCases {
		gotColor, gotOK := Parse(tc.input)
		if (gotColor != tc.wantColor) || (gotOK != tc.wantOK) {
			tt.Errorf("Parse(%q): got %#v, %v, want %#v, %v",
				tc.input, gotColor, gotOK, tc.wantColor, tc.wantOK)
		}
	}
}
