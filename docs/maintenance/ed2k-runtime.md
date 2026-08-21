# ED2K Runtime

aria2-next uses one process-wide ED2K session for the TCP server connection,
Kad UDP socket, client identity, routing table, upload queue, and peer credits.
Concurrent ED2K downloads register with the session and share network discovery
without duplicate listeners or server connections.

Durable state is stored in the binary file selected by
`--ed2k-state-file`. The default is `${HOME}/.aria2/ed2k.state`. State format
version 2 stores identity, Kad routing state, server metadata, peer credits, and
up to ten useful source seeds per incomplete file. Version 1 is intentionally
not migrated.

The interoperability baseline is aMule. TCP and UDP obfuscation use aMule's key
derivation and datagram framing. Server source requests use the persistent TCP
connection and the complete UDP server pool. The server connector keeps two
connection attempts active, refills an individual failed slot, and limits each
attempt to 25 seconds. Every connection, callback, UDP reask, and Kad
transaction has a terminal success, failure, or timeout path. Kad distance and
bucket calculations decode the four little-endian words used on the wire. Kad
v8 hello contacts require receiver-key or HELLO ACK verification. Callback-only
Kad sources remain in the peer lifecycle while their buddy request is pending.
Direct callback sources require the advertised capability and a reachable local
TCP endpoint. TCP firewall state changes only after an inbound TCP probe ACK.

Peer data is accepted only inside outstanding requested ranges. Download and
upload traffic uses aria2's task and global rate limits. AICH roots supplied by
links are trusted immediately; roots learned from peers use aMule's ten-peer,
92-percent quorum before recovery data is accepted. Upload waiters expire after
one hour and active slots rotate after one hour or 10 MiB.

The controlled runtime fixture directory is
`/Users/sekiro/Desktop/aria2-next-ed2k-debug`. Its frozen `server.met` and
`nodes.dat` inputs are used for aMule and aria2-next comparison runs.
