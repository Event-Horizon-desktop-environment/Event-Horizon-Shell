// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

package handsum

import (
	"bytes"
	"image"
	"testing"
)

func makePlainColorImage(grayValue uint8) image.Image {
	m := image.NewGray(image.Rect(0, 0, 5, 5))
	for i := range m.Pix {
		m.Pix[i] = grayValue
	}
	return m
}

func testRoundTrip(tt *testing.T, src image.Image, pixelValue uint8, justCheckThatItsGray bool) {
	colors := []Color{
		ColorGray,
		ColorRGB,
		ColorRGBA,
	}

	for _, c := range colors {
		for q := QualityWorst; q <= QualityBest; q++ {
			buf := &bytes.Buffer{}
			err := Encode(buf, src, &EncodeOptions{
				Color:   c,
				Quality: q,
			})
			if err != nil {
				tt.Fatalf("c=%d: q=%d: Encode: %v", c, q, err)
			}
			dst, err := Decode(buf)
			if err != nil {
				tt.Fatalf("c=%d: q=%d: Decode: %v", c, q, err)
			}

			pix, stride := []byte(nil), 0
			switch dst := dst.(type) {
			case *image.Gray:
				pix, stride = dst.Pix, 1
			case *image.RGBA:
				pix, stride = dst.Pix, 4
			}

			if !justCheckThatItsGray {
				for i := 0; i < len(pix); i += stride {
					if pix[i] != pixelValue {
						tt.Fatalf("c=%d: q=%d: i=%d: pix: got [% 02X], want %02X",
							c, q, i, pix, pixelValue)
					}
				}

			} else if stride == 4 {
				for i := 0; i < len(pix); i += 4 {
					if (pix[i] != pix[i+1]) || (pix[i] != pix[i+2]) {
						tt.Fatalf("c=%d: q=%d: i=%d: pix: got [% 02X], want gray",
							c, q, i, pix)
					}
				}
			}
		}
	}
}

func TestRoundTripPureBlack(tt *testing.T) {
	testRoundTrip(tt, makePlainColorImage(0x00), 0x00, false)
}

func TestRoundTripPureWhite(tt *testing.T) {
	testRoundTrip(tt, makePlainColorImage(0xFF), 0xFF, false)
}

func TestRoundTripGray(tt *testing.T) {
	src := image.NewGray(image.Rect(0, 0, 16, 16))
	for i := range src.Pix {
		src.Pix[i] = uint8(37 * i)
	}
	testRoundTrip(tt, src, 0, true)
}
