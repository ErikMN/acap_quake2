# Third-party game data

The Quake II game data is not stored in this repository.

During `make eap`, the build downloads the original Quake II 3.14 demo installer from
the mirror documented by [Yamagi Quake II][yamagi-installation]:

```text
https://deponie.yamagi.org/quake2/idstuff/q2-314-demo-x86.exe
```

Yamagi documents this installer as the supported source for demo game data and
instructs users to extract `baseq2/pak0.pak` and the `baseq2/players/` directory.
The demo must not be patched.

The installer is 39,015,499 bytes. The build verifies its SHA-256 before extraction:

```text
7ace5a43983f10d6bdc9d9b6e17a1032ba6223118d389bd170df89b945a04a1e
```

Yamagi also publishes the installer MD5 `4d1cd4618e80a38db59304132ea0856c`.
The SHA-256 above is independently recorded by the
[Phoronix Test Suite][phoronix-checksum] for the same filename, size, MD5, and
Yamagi download URL.

After extraction, [oci/build_eap.sh][build-script] verifies `pak0.pak` against
the MD5 published by Yamagi:

```text
baseq2/pak0.pak  27d77240466ec4f3253256832b54db8a
```

The packaged demo data is:

```text
baseq2/pak0.pak
baseq2/players/
```

The installer is a self-extracting ZIP archive. The build extracts it with
`unzip`; it does not execute the Windows program.

For additional provenance, the [GWDG mirror][id-archive] of the historical
`ftp.idsoftware.com/idstuff/quake2/` archive contains
`q2-314-demo-x86.exe` with the same 39,015,499-byte size.

The Quake II game data is copyrighted by id Software and is not covered by this
project's GPL license. Review the applicable game-data terms before
redistributing an EAP.

[yamagi-installation]: https://github.com/yquake2/yquake2/blob/ad3b5f7e10b178bf4e470f16adcac569d0329d74/doc/020_installation.md
[phoronix-checksum]: https://fossies.org/linux/phoronix-test-suite/ob-cache/test-profiles/pts/yquake2-1.2.0/downloads.xml
[id-archive]: https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake2/
[build-script]: ../oci/build_eap.sh
