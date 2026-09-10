// Copyright 2025 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

// ----------------

// handsum decodes and encodes the Handsum lossy image file format.
package main

import (
	"bytes"
	"errors"
	"flag"
	"image"
	"image/draw"
	"image/png"
	"io"
	"os"

	"github.com/google/wuffs/lib/handsum"
	"github.com/google/wuffs/lib/parsecolor"

	_ "image/gif"
	_ "image/jpeg"

	_ "golang.org/x/image/bmp"
	_ "golang.org/x/image/tiff"
	_ "golang.org/x/image/webp"
)

var (
	decodeFlag    = flag.Bool("decode", false, "")
	encodeFlag    = flag.Bool("encode", false, "")
	roundtripFlag = flag.Bool("roundtrip", false, "")

	bFlag  = flag.String("b", "", "")
	bgFlag = flag.String("bg", "", "")
	cFlag  = flag.String("c", "rgb", "")
	qFlag  = flag.Int("q", 4, "")
)

func getBFlag() string {
	if *bFlag != "" {
		return *bFlag
	}
	return *bgFlag
}

const usageStr = `handsum decodes and encodes the Handsum lossy image file format.

Usage: choose one of

    handsum -decode    [path]
    handsum -encode    [path]
    handsum -roundtrip [path]

The path to the input image file is optional. If omitted, stdin is read.

The output image (in Handsum or PNG format) is written to stdout.

Decode inputs Handsum and outputs PNG.
Encode inputs BMP, GIF, JPEG, PNG, TIFF or WEBP and outputs Handsum.
Roundtrip is equivalent to encode (to an ephemeral file) and then decode.

For encode or roundtrip, the default color and quality is -c=rgb -q=4 (best
quality; 147 bytes per file) but you can choose a lower setting. The -b or -bg
flag sets a background color (in case the input is transparent). For example:

handsum -encode -bg=darkblue -c=rgb -q=2 foo.png > foo.handsum

----

Flags:

  -decode     Whether to decode the input.
  -encode     Whether to encode the input.
  -roundtrip  Whether to encode-and-decode the input.

  -b or -bg  Encoding background color: e.g. red, white, 9c27b0 or #ff8.
      The default is ff9e9e9e (for -c=1 or -c=3) or 00000000 (for -c=4).
  -c  Encoding color: gray (1), rgb (3, default) or rgba (4).
  -q  Encoding quality: 1, 2, 3 or 4 (default).
`

func main() {
	if err := main1(); err != nil {
		os.Stderr.WriteString(err.Error() + "\n")
		os.Exit(1)
	}
}

func main1() error {
	flag.Usage = func() { os.Stderr.WriteString(usageStr) }
	flag.Parse()

	color := handsum.ColorRGB
	switch *cFlag {
	case "1", "GRAY", "Gray", "gray":
		color = handsum.ColorGray
	case "4", "RGBA", "Rgba", "rgba":
		color = handsum.ColorRGBA
	}
	quality := handsum.Quality(max(1, min(4, *qFlag)))

	if (color <= handsum.ColorRGB) && (getBFlag() == "") {
		*bFlag = "ff9e9e9e"
	}

	inFile := os.Stdin
	switch flag.NArg() {
	case 0:
		// No-op.
	case 1:
		f, err := os.Open(flag.Arg(0))
		if err != nil {
			return err
		}
		defer f.Close()
		inFile = f
	default:
		return errors.New("too many filenames; the maximum is one")
	}

	if *decodeFlag && !*encodeFlag && !*roundtripFlag {
		return decode(inFile)
	}
	if !*decodeFlag && *encodeFlag && !*roundtripFlag {
		return encode(os.Stdout, inFile, color, quality)
	}
	if !*decodeFlag && !*encodeFlag && *roundtripFlag {
		return roundtrip(inFile, color, quality)
	}
	return errors.New("must specify exactly one of -decode, -encode, -roundtrip or -help")
}

func decode(inFile *os.File) error {
	src, err := handsum.Decode(inFile)
	if err != nil {
		return err
	}
	return png.Encode(os.Stdout, src)
}

func encode(w io.Writer, inFile *os.File, color handsum.Color, quality handsum.Quality) error {
	src, _, err := image.Decode(inFile)
	if err != nil {
		return err
	}

	if o, ok := src.(interface{ Opaque() bool }); !ok || !o.Opaque() {
		src = applyBackgroundColor(src)
	}

	return handsum.Encode(w, src, &handsum.EncodeOptions{
		Color:   color,
		Quality: quality,
	})
}

func roundtrip(inFile *os.File, color handsum.Color, quality handsum.Quality) error {
	buf := &bytes.Buffer{}
	err := encode(buf, inFile, color, quality)
	if err != nil {
		return err
	}
	dst, err := handsum.Decode(buf)
	if err != nil {
		return err
	}
	return png.Encode(os.Stdout, dst)
}

func applyBackgroundColor(src image.Image) image.Image {
	backgroundColor, _ := parsecolor.Parse(getBFlag())
	r16, g16, b16, a16 := backgroundColor.RGBA()
	if (r16 == 0) && (g16 == 0) && (b16 == 0) && (a16 == 0) {
		return src
	}
	r8, g8, b8 := uint8(r16>>8), uint8(g16>>8), uint8(b16>>8)

	bounds := src.Bounds()
	ret := image.NewRGBA(bounds)
	pix := ret.Pix
	n4 := len(pix) / 4
	for i4 := range n4 {
		i := i4 * 4
		pix[i+0] = r8
		pix[i+1] = g8
		pix[i+2] = b8
		pix[i+3] = 0xff
	}

	draw.Draw(ret, bounds, src, bounds.Min, draw.Over)
	return ret
}
