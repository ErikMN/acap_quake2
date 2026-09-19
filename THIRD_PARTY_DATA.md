# Third-party game data

The Quake II game data is not stored in this repository.

During `make eap`, the build downloads the game data used by
`drags/docker-quake2` from the pinned commit:

```text
c8b00cbc4bce0c7bcad8d490af4cd1d45cb4cd7c
```

The packaged files are:

```text
baseq2/pak0.pak
baseq2/pak1.pak
baseq2/pak2.pak
```

The expected Git blob IDs are:

```text
pak0.pak  1b5d5e28410cf6d90f6e1e3fae4c590a811629cf
pak1.pak  8189343fc45aaa784fab54578405ec88d883642a
pak2.pak  462bb0d42eba1eeacee45e52a3443f5c5a141574
```

The upstream repository describes its included game data as resources from
the Quake II demo. The game data is copyrighted by id Software and is not
covered by this project's GPL license.

The pinned source repository does not include a separate license file for
the game data. Its README describes the included resources as data from the
Quake II demo. A copy of that pinned README is included in the EAP as
`DEMO_DATA_SOURCE.md`.

Review the applicable game-data terms before redistributing an EAP.
