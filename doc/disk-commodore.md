commodore
====
## 1541, 1581, 8050 and variations
<!-- This file is automatically generated. Do not edit. -->

Commodore 8-bit computer disks come in two varieties: GCR, which are the
overwhelming majority; and MFM, only used on the 1571 and 1581. The latter were
(as far as I can tell) standard IBM PC format disks with a slightly odd sector
count.

The GCR disks are much more interesting. They could store 170kB on a
single-sided disk (although later drives were double-sided), using a proprietary
encoding and record scheme; like [Apple Macintosh disks](macintosh.md) they
stored varying numbers of sectors per track to make the most of the physical
disk area, although unlike them they did it by changing the bitrate rather than
adjusting the motor speed.

The drives were also intelligent and ran DOS on a CPU inside them. The
computer itself knew nothing about file systems. You could even upload
programs onto the drive and run them there, allowing all sorts of custom disk
formats, although this was mostly used to compensate for the [cripplingly
slow connection to the
computer](https://ilesj.wordpress.com/2014/05/14/1541-why-so-complicated/) of
300 bytes per second (!). (The drive itself could transfer data reasonably
quickly.)

  - a 1541 disk has 35 tracks of 17 to 21 sectors, each 256 bytes long
	(sometimes 40 tracks), and uses GCR encoding.

  - a standard 1581 disk has 80 tracks and two sides, each with 10 sectors, 512
	bytes long, and uses normal IBM encoding.

  - an 8050 disk has 77 tracks and two sides, with four speed zones; the number
	of sectors varies from 23 to 29, using GCR encoding. These will store
	1042kB. These drives are peculiar because they are 100tpi and therefore the
	disks cannot be read in normal 96tpi drives.

  - a CMD FD2000 disk (a popular third-party Commodore disk drive) has 81
	tracks and two sides, each with 10 1024-byte sectors, for a massive 1620kB
	of storage. This also uses IBM encoding.

A CMD FD2000 disk (a popular third-party Commodore disk drive) 

## Options

  - Format variants:
      - `171`: 171kB 1541, 35-track variant
      - `192`: 192kB 1541, 40-track variant
      - `800`: 800kB 3.5" 1581
      - `1042`: 1042kB 5.25" 8051
      - `1620`: 1620kB, CMD FD2000

## Examples

To read:

  - `fluxengine read commodore --171 -s drive:0 -o commodore.d64`
  - `fluxengine read commodore --192 -s drive:0 -o commodore.d64`
  - `fluxengine read commodore --800 -s drive:0 -o commodore.d64`
  - `fluxengine read commodore --1042 -s drive:0 -o commodore.d64`
  - `fluxengine read commodore --1620 -s drive:0 -o commodore.d64`

To write:

  - `fluxengine write commodore --171 -d drive:0 -i commodore.d64`
  - `fluxengine write commodore --192 -d drive:0 -i commodore.d64`
  - `fluxengine write commodore --800 -d drive:0 -i commodore.d64`
  - `fluxengine write commodore --1620 -d drive:0 -i commodore.d64`

## References

  - [Ruud's Commodore Site: 1541](http://www.baltissen.org/newhtm/1541c.htm):
    documentation on the 1541 disk format.
