# Third-party game data

The Quake II game data is not stored in this repository.

During `make eap`, the build downloads the game data used by
[drags/docker-quake2][data-source] from the pinned commit:

```text
c8b00cbc4bce0c7bcad8d490af4cd1d45cb4cd7c
```

The packaged files are:

```text
baseq2/pak0.pak
baseq2/pak1.pak
baseq2/pak2.pak
```

[oci/build_eap.sh][build-script] checks each downloaded file against its expected Git blob ID.
These IDs identify the exact file contents recorded by Git; the build stops if a file does not match.

```text
pak0.pak  1b5d5e28410cf6d90f6e1e3fae4c590a811629cf
pak1.pak  8189343fc45aaa784fab54578405ec88d883642a
pak2.pak  462bb0d42eba1eeacee45e52a3443f5c5a141574
```

The upstream repository describes its included game data as resources from the Quake II demo.
The game data is copyrighted by id Software and is not covered by this project's GPL license.

The pinned source repository does not include a separate license file for the game data.
A copy of its [README][data-readme] is included in the EAP as `DEMO_DATA_SOURCE.md`.

Review the applicable game-data terms before redistributing an EAP.

[data-source]: https://github.com/drags/docker-quake2/tree/c8b00cbc4bce0c7bcad8d490af4cd1d45cb4cd7c
[data-readme]: https://github.com/drags/docker-quake2/blob/c8b00cbc4bce0c7bcad8d490af4cd1d45cb4cd7c/README.md
[build-script]: https://github.com/ErikMN/acap_quake2/blob/main/oci/build_eap.sh
