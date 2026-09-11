/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2009 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef D_RPC_FIELDS_H
#define D_RPC_FIELDS_H

namespace aria2::rpc::fields {

inline constexpr char VLB_TRUE[] = "true";
inline constexpr char VLB_FALSE[] = "false";
inline constexpr char VLB_ACTIVE[] = "active";
inline constexpr char VLB_WAITING[] = "waiting";
inline constexpr char VLB_PAUSED[] = "paused";
inline constexpr char VLB_REMOVED[] = "removed";
inline constexpr char VLB_ERROR[] = "error";
inline constexpr char VLB_COMPLETE[] = "complete";
inline constexpr char VLB_USED[] = "used";
inline constexpr char VLB_ZERO[] = "0";

inline constexpr char KEY_GID[] = "gid";
inline constexpr char KEY_ERROR_CODE[] = "errorCode";
inline constexpr char KEY_ERROR_MESSAGE[] = "errorMessage";
inline constexpr char KEY_STATUS[] = "status";
inline constexpr char KEY_TOTAL_LENGTH[] = "totalLength";
inline constexpr char KEY_COMPLETED_LENGTH[] = "completedLength";
inline constexpr char KEY_DOWNLOAD_SPEED[] = "downloadSpeed";
inline constexpr char KEY_UPLOAD_SPEED[] = "uploadSpeed";
inline constexpr char KEY_UPLOAD_LENGTH[] = "uploadLength";
inline constexpr char KEY_CONNECTIONS[] = "connections";
inline constexpr char KEY_BITFIELD[] = "bitfield";
inline constexpr char KEY_PIECE_LENGTH[] = "pieceLength";
inline constexpr char KEY_NUM_PIECES[] = "numPieces";
inline constexpr char KEY_FOLLOWED_BY[] = "followedBy";
inline constexpr char KEY_FOLLOWING[] = "following";
inline constexpr char KEY_BELONGS_TO[] = "belongsTo";
inline constexpr char KEY_INFO_HASH[] = "infoHash";
inline constexpr char KEY_NUM_SEEDERS[] = "numSeeders";
inline constexpr char KEY_PEER_ID[] = "peerId";
inline constexpr char KEY_IP[] = "ip";
inline constexpr char KEY_PORT[] = "port";
inline constexpr char KEY_LISTEN_PORT[] = "listenPort";
inline constexpr char KEY_AM_CHOKING[] = "amChoking";
inline constexpr char KEY_AM_INTERESTED[] = "amInterested";
inline constexpr char KEY_PEER_CHOKING[] = "peerChoking";
inline constexpr char KEY_PEER_INTERESTED[] = "peerInterested";
inline constexpr char KEY_PEER_CLIENT_NAME[] = "peerClientName";
inline constexpr char KEY_DOWNLOADED[] = "downloaded";
inline constexpr char KEY_UPLOADED[] = "uploaded";
inline constexpr char KEY_PROGRESS[] = "progress";
inline constexpr char KEY_FLAGS[] = "flags";
inline constexpr char KEY_INCOMING[] = "incoming";
inline constexpr char KEY_SNUBBED[] = "snubbed";
inline constexpr char KEY_OPTIMISTIC_UNCHOKE[] = "optimisticUnchoke";
inline constexpr char KEY_PRIVATE_TORRENT[] = "privateTorrent";
inline constexpr char KEY_SEEDER[] = "seeder";
inline constexpr char KEY_INDEX[] = "index";
inline constexpr char KEY_PATH[] = "path";
inline constexpr char KEY_SELECTED[] = "selected";
inline constexpr char KEY_PRIORITY[] = "priority";
inline constexpr char KEY_LENGTH[] = "length";
inline constexpr char KEY_URI[] = "uri";
inline constexpr char KEY_CURRENT_URI[] = "currentUri";
inline constexpr char KEY_VERSION[] = "version";
inline constexpr char KEY_PRODUCT[] = "product";
inline constexpr char KEY_RPC_VERSION[] = "rpcVersion";
inline constexpr char KEY_ENABLED_FEATURES[] = "enabledFeatures";

inline constexpr char PRODUCT_NAME[] = "aria2-next";
inline constexpr char RPC_VERSION[] = "1.1.0";
inline constexpr char KEY_METHOD_NAME[] = "methodName";
inline constexpr char KEY_PARAMS[] = "params";
inline constexpr char KEY_SESSION_ID[] = "sessionId";
inline constexpr char KEY_FILES[] = "files";
inline constexpr char KEY_DIR[] = "dir";
inline constexpr char KEY_URIS[] = "uris";
inline constexpr char KEY_BITTORRENT[] = "bittorrent";
inline constexpr char KEY_ED2K[] = "ed2k";
inline constexpr char KEY_INFO[] = "info";
inline constexpr char KEY_NAME[] = "name";
inline constexpr char KEY_ANNOUNCE_LIST[] = "announceList";
inline constexpr char KEY_COMMENT[] = "comment";
inline constexpr char KEY_CREATION_DATE[] = "creationDate";
inline constexpr char KEY_MODE[] = "mode";
inline constexpr char KEY_SERVERS[] = "servers";
inline constexpr char KEY_NUM_WAITING[] = "numWaiting";
inline constexpr char KEY_NUM_STOPPED[] = "numStopped";
inline constexpr char KEY_NUM_ACTIVE[] = "numActive";
inline constexpr char KEY_NUM_STOPPED_TOTAL[] = "numStoppedTotal";
inline constexpr char KEY_VERIFIED_LENGTH[] = "verifiedLength";
inline constexpr char KEY_VERIFY_PENDING[] = "verifyIntegrityPending";
inline constexpr char KEY_HASH[] = "hash";
inline constexpr char KEY_SOURCE_COUNT[] = "sourceCount";
inline constexpr char KEY_COMPLETE_SOURCE_COUNT[] = "completeSourceCount";
inline constexpr char KEY_FILE_TYPE[] = "fileType";
inline constexpr char KEY_EXTENSION[] = "extension";
inline constexpr char KEY_MEDIA_ARTIST[] = "mediaArtist";
inline constexpr char KEY_MEDIA_ALBUM[] = "mediaAlbum";
inline constexpr char KEY_MEDIA_TITLE[] = "mediaTitle";
inline constexpr char KEY_MEDIA_LENGTH[] = "mediaLength";
inline constexpr char KEY_MEDIA_BITRATE[] = "mediaBitrate";
inline constexpr char KEY_MEDIA_CODEC[] = "mediaCodec";
inline constexpr char KEY_SOURCE_NETWORK[] = "sourceNetwork";
inline constexpr char KEY_ED2K_LINK[] = "ed2kLink";
inline constexpr char KEY_MAGNET_LINK[] = "magnetLink";
inline constexpr char KEY_MORE_RESULTS[] = "moreResults";

} // namespace aria2::rpc::fields

#endif // D_RPC_FIELDS_H
