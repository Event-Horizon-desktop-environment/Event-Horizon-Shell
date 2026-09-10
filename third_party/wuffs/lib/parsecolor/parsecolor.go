// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

// ----------------

// Package parsecolor parses an RGB or ARGB color as hex (like "b0279c") or a
// well-known name (like "purple").
package parsecolor

import (
	"image/color"
)

// Parse parses a string that's a hex color (like "#ff8" or "ffb0279c") or a
// well-known name (like "black", "purple" or "darkred"). Well-known names are
// an ad-hoc set based on the Material Design color names, optionally prefixed
// by "dark" or "light".
//
// On success, it returns ok=true. On failure, it returns ok=false and nrgba is
// the zero color.NRGBA value: transparent black.
//
// Parsing is case-insensitive.
func Parse(s string) (nrgba color.NRGBA, ok bool) {
	if (len(s) <= 0) || (16 <= len(s)) {
		return color.NRGBA{}, false
	}

	startsWithHash := s[0] == '#'
	if startsWithHash {
		s = s[1:]
		if len(s) <= 0 {
			return color.NRGBA{}, false
		}
	}

	buf := [16]byte{}
	for i := range s {
		if s[i] >= 0x80 {
			return color.NRGBA{}, false
		}
		buf[i] = s[i] | 0x20
	}

	if !startsWithHash {
		adjustment, p, n := 0, 0, len(s)
		if ((n == 4) && (string(buf[:4]) == "none")) ||
			((n == 11) && (string(buf[:11]) == "transparent")) {
			return color.NRGBA{}, true
		} else if string(buf[:4]) == "dark" {
			adjustment = 1
			p += 4
			n -= 4
		} else if string(buf[:5]) == "light" {
			adjustment = 2
			p += 5
			n -= 5
		}

		for i := 1; i < len(offsets); i++ {
			o0 := int(offsets[i-0] >> 24)
			o1 := int(offsets[i-1] >> 24)
			if (n != o0-o1) || (string(buf[p:p+n]) != names[o1:o1+n]) {
				continue
			}

			offset := offsets[i]
			r := 0xFF & (offset >> 16)
			g := 0xFF & (offset >> 8)
			b := 0xFF & (offset >> 0)

			if adjustment == 1 {
				r /= 2
				g /= 2
				b /= 2
			} else if adjustment == 2 {
				r = 0xFF - ((0xFF - r) / 2)
				g = 0xFF - ((0xFF - g) / 2)
				b = 0xFF - ((0xFF - b) / 2)
			}

			return color.NRGBA{uint8(r), uint8(g), uint8(b), 0xFF}, true
		}
	}

	ret, multiplier, shift := uint32(0), uint32(0), 0
	if len(s) == 3 {
		ret, multiplier, shift = 0xFF, 0x11, 8
	} else if len(s) == 4 {
		ret, multiplier, shift = 0x00, 0x11, 8
	} else if len(s) == 6 {
		ret, multiplier, shift = 0xFF, 0x01, 4
	} else if len(s) == 8 {
		ret, multiplier, shift = 0x00, 0x01, 4
	} else {
		return color.NRGBA{}, false
	}

	for i := 0; i < len(s); i++ {
		c := buf[i]
		if ('0' <= c) && (c <= '9') {
			c -= '0'
		} else if ('a' <= c) && (c <= 'f') {
			c -= 'a' - 10
		} else {
			return color.NRGBA{}, false
		}
		ret <<= shift
		ret |= multiplier * uint32(c)
	}

	return color.NRGBA{uint8(ret >> 16), uint8(ret >> 8), uint8(ret >> 0), uint8(ret >> 24)}, true
}

var offsets = [...]uint32{
	0x00000000, //
	0x05000000, // black
	0x0AFFFFFF, // white
	0x0DF44336, // red
	0x11E91E63, // pink
	0x179C27B0, // purple
	0x1D3F51B5, // indigo
	0x212196F3, // blue
	0x2500BCD4, // cyan
	0x29009688, // teal
	0x2E4CAF50, // green
	0x32CDDC39, // lime
	0x38FFEB3B, // yellow
	0x3DFFC107, // amber
	0x43FF9800, // orange
	0x48795548, // brown
	0x4C9E9E9E, // gray

	// Ad-hoc synonyms for some of the official Material Design names.
	0x509E9E9E, // grey
	0x579C27B0, // magenta
}

const names = "blackwhiteredpinkpurpleindigobluecyantealgreenlimeyellowamberorangebrowngraygreymagenta"
