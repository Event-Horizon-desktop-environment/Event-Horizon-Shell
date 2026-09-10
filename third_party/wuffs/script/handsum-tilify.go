// Copyright 2026 The Wuffs Authors.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// https://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT

//go:build ignore
// +build ignore

package main

// handsum-tilify.go decodes JPEG or PNG from stdin, partitions it into a grid
// of 16×16 tiles, round-trips each tile through the Handsum codec and writes
// the resultant PNG to stdout.
//
// Usage: go run handsum-tilify.go < input.png > output.png

import (
	"bytes"
	"flag"
	"image"
	"image/draw"
	"image/png"
	"os"

	"github.com/google/wuffs/lib/handsum"

	_ "image/jpeg"
)

var qFlag = flag.Int("q", 4, "Encoding quality: 1, 2, 3 or 4 (default)")

func main() {
	if err := main1(); err != nil {
		os.Stderr.WriteString(err.Error() + "\n")
		os.Exit(1)
	}
}

func main1() error {
	flag.Parse()
	quality := handsum.Quality(max(1, min(4, *qFlag)))

	src0, _, err := image.Decode(os.Stdin)
	if err != nil {
		return err
	}
	bounds := src0.Bounds()
	src1, _ := src0.(interface {
		SubImage(image.Rectangle) image.Image
	})
	dst := image.NewRGBA(bounds)
	buf := &bytes.Buffer{}

	for y := bounds.Min.Y; y < bounds.Max.Y; y += 16 {
		for x := bounds.Min.X; x < bounds.Max.X; x += 16 {
			buf.Reset()
			rect := image.Rect(x, y, x+16, y+16)
			tile := src1.SubImage(rect)
			handsum.Encode(buf, tile, &handsum.EncodeOptions{
				Quality: quality,
			})
			roundTripped, _ := handsum.Decode(buf)
			draw.Draw(dst, rect, roundTripped, roundTripped.Bounds().Min, draw.Src)
		}
	}

	return png.Encode(os.Stdout, dst)
}
