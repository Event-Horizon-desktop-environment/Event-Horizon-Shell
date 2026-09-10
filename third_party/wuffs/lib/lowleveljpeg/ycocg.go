// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

package lowleveljpeg

import (
	"image"
)

// RGBToYCoCg converts an RGB triple to a YCoCg triple.
//
// JPEG uses YCbCr, not YCoCg, but this function allows for experimenting with
// JPEG-inspired color transformations.
func RGBToYCoCg(r, g, b uint8) (uint8, uint8, uint8) {
	// https://en.wikipedia.org/wiki/YCoCg#The_lifting-based_YCoCg-R_variation
	co := int(r) - int(b)
	tm := int(b) + (co / 2)
	cg := int(g) - tm
	yy := tm + (cg / 2)

	// Normalize to the range [0x00, 0xFF], halving (and biasing) Co and Cg.
	return uint8(yy), uint8((0x100 + co) / 2), uint8((0x100 + cg) / 2)
}

// YCoCgToRGB converts a YCoCg triple to an RGB triple.
//
// JPEG uses YCbCr, not YCoCg, but this function allows for experimenting with
// JPEG-inspired color transformations.
func YCoCgToRGB(yy, co, cg uint8) (uint8, uint8, uint8) {
	// https://en.wikipedia.org/wiki/YCoCg#The_lifting-based_YCoCg-R_variation
	// adjusted for the fact that the co and cg arguments have already been
	// halved-and-biased, per RGBToYCoCg.
	yy1 := int(yy)
	co1 := int(co) - 0x80
	cg1 := int(cg) - 0x80
	g := yy1 + cg1
	b := yy1 - cg1 - co1
	r := yy1 - cg1 + co1

	return uint8(max(0x00, min(0xFF, r))),
		uint8(max(0x00, min(0xFF, g))),
		uint8(max(0x00, min(0xFF, b)))
}

// ExtractYCoCgFrom is like ExtractYCbCrFrom but produces Gray (Luma) values
// according to the (Luma, Chroma-orange, Chroma-green) formulae instead of
// (Luma, Chroma-blue, Chroma-red).
//
// JPEG uses YCbCr, not YCoCg, but this method allows for experimenting with
// JPEG-inspired image transformations.
func (dst *Array1BlockU8) ExtractYCoCgFrom(m image.Image, topLeftX int, topLeftY int) {
	if dst == nil {
		return
	}
	dst.SetToNeutral()
	if m == nil {
		return
	}
	bounds := m.Bounds()
	mGray, _ := m.(*image.Gray)
	mRGBA64Image, _ := m.(image.RGBA64Image)

	for dy := 0; dy < 8; dy++ {
		for dx := 0; dx < 8; dx++ {
			p := image.Point{topLeftX + dx, topLeftY + dy}
			if !p.In(bounds) {
				continue
			}
			i := (8 * dy) + dx

			if mGray != nil {
				dst[0][i] = mGray.GrayAt(p.X, p.Y).Y

			} else if mRGBA64Image != nil {
				pix := mRGBA64Image.RGBA64At(p.X, p.Y)
				y, _, _ := RGBToYCoCg(
					uint8(pix.R>>8),
					uint8(pix.G>>8),
					uint8(pix.B>>8),
				)
				dst[0][i] = uint8(y)

			} else {
				r, g, b, _ := m.At(p.X, p.Y).RGBA()
				y, _, _ := RGBToYCoCg(
					uint8(r>>8),
					uint8(g>>8),
					uint8(b>>8),
				)
				dst[0][i] = uint8(y)
			}
		}
	}

	fillRightAndDown(&dst[0], topLeftX, topLeftY, bounds.Max.X, bounds.Max.Y)
}

// ExtractYCoCgFrom is like ExtractYCbCrFrom but produces (Luma, Chroma-orange,
// Chroma-green) instead of (Luma, Chroma-blue, Chroma-red).
//
// JPEG uses YCbCr, not YCoCg, but this method allows for experimenting with
// JPEG-inspired image transformations.
func (dst *Array3BlockU8) ExtractYCoCgFrom(m image.Image, topLeftX int, topLeftY int) {
	if dst == nil {
		return
	}
	dst.SetToNeutral()
	if m == nil {
		return
	}
	bounds := m.Bounds()
	mRGBA64Image, _ := m.(image.RGBA64Image)

	for dy := 0; dy < 8; dy++ {
		for dx := 0; dx < 8; dx++ {
			p := image.Point{topLeftX + dx, topLeftY + dy}
			if !p.In(bounds) {
				continue
			}
			i := (8 * dy) + dx

			if mRGBA64Image != nil {
				pix := mRGBA64Image.RGBA64At(p.X, p.Y)
				y, co, cg := RGBToYCoCg(
					uint8(pix.R>>8),
					uint8(pix.G>>8),
					uint8(pix.B>>8),
				)
				dst[0][i] = y
				dst[1][i] = co
				dst[2][i] = cg

			} else {
				r, g, b, _ := m.At(p.X, p.Y).RGBA()
				y, co, cg := RGBToYCoCg(
					uint8(r>>8),
					uint8(g>>8),
					uint8(b>>8),
				)
				dst[0][i] = y
				dst[1][i] = co
				dst[2][i] = cg
			}
		}
	}

	fillRightAndDown(&dst[0], topLeftX, topLeftY, bounds.Max.X, bounds.Max.Y)
	fillRightAndDown(&dst[1], topLeftX, topLeftY, bounds.Max.X, bounds.Max.Y)
	fillRightAndDown(&dst[2], topLeftX, topLeftY, bounds.Max.X, bounds.Max.Y)
}

// ExtractYCoCgFrom is like ExtractYCbCrFrom but produces (Luma, Chroma-orange,
// Chroma-green) instead of (Luma, Chroma-blue, Chroma-red).
//
// JPEG uses YCbCr, not YCoCg, but this method allows for experimenting with
// JPEG-inspired image transformations.
func (dst *Array6BlockU8) ExtractYCoCgFrom(m image.Image, topLeftX int, topLeftY int) {
	if dst == nil {
		return
	}
	if m == nil {
		dst.SetToNeutral()
		return
	}

	tmp := [4]Array3BlockU8{}
	tmp[0].ExtractYCoCgFrom(m, topLeftX+0, topLeftY+0)
	tmp[1].ExtractYCoCgFrom(m, topLeftX+8, topLeftY+0)
	tmp[2].ExtractYCoCgFrom(m, topLeftX+0, topLeftY+8)
	tmp[3].ExtractYCoCgFrom(m, topLeftX+8, topLeftY+8)

	dst[0] = tmp[0][0]
	dst[1] = tmp[1][0]
	dst[2] = tmp[2][0]
	dst[3] = tmp[3][0]

	downsample4x4(dst[4][0x00:], &tmp[0][1])
	downsample4x4(dst[4][0x04:], &tmp[1][1])
	downsample4x4(dst[4][0x20:], &tmp[2][1])
	downsample4x4(dst[4][0x24:], &tmp[3][1])

	downsample4x4(dst[5][0x00:], &tmp[0][2])
	downsample4x4(dst[5][0x04:], &tmp[1][2])
	downsample4x4(dst[5][0x20:], &tmp[2][2])
	downsample4x4(dst[5][0x24:], &tmp[3][2])
}
