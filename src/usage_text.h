/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
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

// clang-format off

#define TEXT_DIR                                                        \
  _(" -d, --dir=DIR                The directory to store the downloaded file.")
#define TEXT_OUT                                                        \
  _(" -o, --out=FILE               The file name of the downloaded file. It is\n" \
    "                              always relative to the directory given in -d\n" \
    "                              option. When the -Z option is used, this option\n" \
    "                              will be ignored.")
#define TEXT_LOG                                                        \
  _(" -l, --log=LOG                The file name of the log file. If '-' is\n" \
    "                              specified, log is written to stdout.")
#define TEXT_DAEMON                                                     \
  _(" -D, --daemon[=true|false]    Run as daemon. The current working directory will\n" \
    "                              be changed to \"/\" and standard input, standard\n" \
    "                              output and standard error will be redirected to\n" \
    "                              \"/dev/null\". Requires RPC, an input file, or a URI.")
#define TEXT_RETRY_WAIT                                                 \
  _(" --retry-wait=SEC             Set the seconds to wait between retries. \n" \
    "                              With SEC > 0, aria2 will retry download when the\n" \
    "                              HTTP server returns 503 response.")
#define TEXT_STREAM_MAX_CONNECTIONS                                     \
  _(" --stream-max-connections=N  Set the per-file HTTP connection ceiling.")
#define TEXT_TIMEOUT                                            \
  _(" -t, --timeout=SEC            Set timeout in seconds.")
#define TEXT_MAX_TRIES                                                  \
  _(" -m, --max-tries=N            Set number of tries. 0 means unlimited.")
#define TEXT_HTTP_PROXY                                                 \
  _(" --http-proxy=PROXY           Use a proxy server for HTTP. To override a\n"\
    "                              previously defined proxy, use \"\".\n"   \
    "                              See also the --all-proxy option.\n"     \
    "                              This affects all http downloads.")
#define TEXT_HTTPS_PROXY                                                \
  _(" --https-proxy=PROXY          Use a proxy server for HTTPS. To override a \n"  \
    "                              previously defined proxy, use \"\".\n" \
    "                              See also the --all-proxy option.\n"     \
    "                              This affects all https downloads.")
#define TEXT_ALL_PROXY                                                  \
  _(" --all-proxy=PROXY            Use a proxy server for all protocols. To override\n" \
    "                              a previously defined proxy, use \"\".\n" \
    "                              You also can override this setting and specify a\n" \
    "                              proxy server for a particular protocol using the\n" \
    "                              --http-proxy and --https-proxy\n" \
    "                              options.\n"                          \
    "                              This affects all downloads.")
#define TEXT_HTTP_USER                                                  \
  _(" --http-user=USER             Set HTTP user. This affects all URLs.")
#define TEXT_HTTP_PASSWD                                                \
  _(" --http-passwd=PASSWD         Set HTTP password. This affects all URLs.")
#define TEXT_REFERER                                                    \
  _(" --referer=REFERER            Set an http referrer (Referer). This affects\n" \
    "                              all http/https downloads. If \"*\" is given,\n" \
    "                              the download URI is also used as the referrer.\n" \
    "                              This may be useful when used together with\n" \
    "                              the -P option.")
#define TEXT_SFTP_USER                                                  \
  _(" --sftp-user=USER             Set the SFTP user.")
#define TEXT_SFTP_PASSWD                                                \
  _(" --sftp-passwd=PASSWD         Set the SFTP password.")
#define TEXT_LOWEST_SPEED_LIMIT                                         \
  _(" --lowest-speed-limit=SPEED   Close connection if download speed is lower than\n" \
    "                              or equal to this value(bytes per sec).\n" \
    "                              0 means aria2 does not have a lowest speed limit.\n" \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.\n"                 \
    "                              This option does not affect BitTorrent downloads.")
#define TEXT_MAX_OVERALL_DOWNLOAD_LIMIT                                 \
  _(" --max-overall-download-limit=SPEED Set max overall download speed in bytes/sec.\n" \
    "                              0 means unrestricted.\n"             \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.\n"                 \
    "                              To limit the download speed per download, use\n" \
    "                              --max-download-limit option.")
#define TEXT_MAX_DOWNLOAD_LIMIT                                         \
  _(" --max-download-limit=SPEED   Set max download speed per each download in\n" \
    "                              bytes/sec. 0 means unrestricted.\n"  \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.\n"                 \
    "                              To limit the overall download speed, use\n" \
    "                              --max-overall-download-limit option.")
#define TEXT_FILE_ALLOCATION                                            \
  _(" --file-allocation=METHOD     Specify file allocation method.\n"   \
    "                              'none' doesn't pre-allocate file space. 'prealloc'\n" \
    "                              pre-allocates file space before download begins\n" \
    "                              using native platform allocation when available.\n" \
    "                              If you are using newer file systems such as ext4\n" \
    "                              (with extents support), btrfs, xfs or APFS,\n" \
    "                              'falloc' is your best\n"   \
    "                              choice. It allocates large(few GiB) files quickly.\n" \
    "                              Don't use 'falloc' with legacy\n" \
    "                              file systems such as ext3 and FAT32 because it\n" \
    "                              takes almost the same time as 'prealloc' and it\n" \
    "                              blocks aria2 entirely until allocation finishes.\n" \
    "                              'trunc' uses ftruncate() system call or\n" \
    "                              platform-specific counterpart to truncate a file\n" \
    "                              to a specified length.")
#define TEXT_NO_FILE_ALLOCATION_LIMIT                                   \
  _(" --no-file-allocation-limit=SIZE No file allocation is made for files whose\n" \
    "                              size is smaller than SIZE.\n"        \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.")
#define TEXT_ENABLE_DIRECT_IO                                          \
  _(" --enable-direct-io[=true|false] Enable directI/O, which lowers cpu usage while\n" \
    "                              allocating files.\n"                 \
    "                              Turn off if you encounter any error")
#define TEXT_ALLOW_OVERWRITE                                            \
  _(" --allow-overwrite[=true|false] Replace an existing output file when no valid\n" \
    "                              persisted transfer state is available. See\n" \
    "                              also --auto-file-renaming option.")
#define TEXT_FORCE_SEQUENTIAL                                           \
  _(" -Z, --force-sequential[=true|false] Fetch URIs in the command-line sequentially\n" \
    "                              and download each URI in a separate session, like\n" \
    "                              the usual command-line download utilities.")
#define TEXT_AUTO_FILE_RENAMING                                         \
  _(" --auto-file-renaming[=true|false] Rename file name if the same file already\n" \
    "                              exists for stream downloads.\n"     \
    "                              The new file name has a dot and a number(1..9999)\n" \
    "                              appended after the name, but before the file\n" \
    "                              extension, if any.")
#define TEXT_PARAMETERIZED_URI                                          \
  _(" -P, --parameterized-uri[=true|false] Enable parameterized URI support.\n" \
    "                              You can specify set of parts:\n"     \
    "                              http://{sv1,sv2,sv3}/foo.iso\n"      \
    "                              Also you can specify numeric sequences with step\n" \
    "                              counter:\n"                          \
    "                              http://host/image[000-100:2].img\n"  \
    "                              A step counter can be omitted.\n"    \
    "                              If all URIs do not point to the same file, such\n" \
    "                              as the second example above, -Z option is\n" \
    "                              required.")
#define TEXT_ENABLE_HTTP_KEEP_ALIVE                                     \
  _(" --enable-http-keep-alive[=true|false] Enable HTTP/1.1 persistent connection.")
#define TEXT_CHECK_INTEGRITY                                            \
  _(" -V, --check-integrity[=true|false] Check file integrity by validating piece\n" \
    "                              hashes or a hash of entire file. This option has\n" \
    "                              effect only in BitTorrent, Metalink downloads\n" \
    "                              with checksums or HTTP(S)/SFTP downloads with\n" \
    "                              --checksum option. If piece hashes are provided,\n" \
    "                              this option can detect damaged portions of a file\n" \
    "                              and re-download them. If a hash of entire file is\n" \
    "                              provided, hash check is only done when file has\n" \
    "                              been already download. This is determined by file\n" \
    "                              length. If hash check fails, file is\n" \
    "                              re-downloaded from scratch. If both piece hashes\n" \
    "                              and a hash of entire file are provided, only\n" \
    "                              piece hashes are used.")
#define TEXT_REALTIME_CHUNK_CHECKSUM                                    \
  _(" --realtime-chunk-checksum[=true|false]  Validate chunk of data by calculating\n" \
    "                              checksum while downloading a file if chunk\n" \
    "                              checksums are provided.")
#define TEXT_CONTINUE                                                   \
  _(" -c, --continue[=true|false]  Verify and continue an existing stream file.\n" \
    "                              Equal-length HTTP files complete without\n" \
    "                              transferring payload data.")
#define TEXT_USER_AGENT                                                 \
  _(" -U, --user-agent=USER_AGENT  Set user agent for http(s) downloads.")
#define TEXT_NO_NETRC                                           \
  _(" -n, --no-netrc[=true|false]  Disables netrc support.")
#define TEXT_NETRC_PATH                                           \
  _(" --netrc-path=FILE            Specify the path to the netrc file.")
#define TEXT_INPUT_FILE                                                 \
  _(" -i, --input-file=FILE        Downloads URIs found in FILE. You can specify\n" \
    "                              multiple URIs for a single entity: separate\n" \
    "                              URIs on a single line using the TAB character.\n" \
    "                              Reads input from stdin when '-' is specified.\n" \
    "                              Additionally, options can be specified after each\n" \
    "                              line of URI. This optional line must start with\n" \
    "                              one or more white spaces and have one option per\n" \
    "                              single line. See INPUT FILE section of man page\n" \
    "                              for details. See also --deferred-input option.")
#define TEXT_MAX_CONCURRENT_DOWNLOADS                                   \
  _(" -j, --max-concurrent-downloads=N Set maximum number of parallel downloads for\n" \
    "                              every static HTTP(S)/SFTP URL, torrent and metalink.\n" \
    "                              See also --optimize-concurrent-downloads.")
#define TEXT_OPTIMIZE_CONCURRENT_DOWNLOADS\
  _(" --optimize-concurrent-downloads[=true|false|A:B] Optimizes the number of\n" \
    "                              concurrent downloads according to the bandwidth\n" \
    "                              available. aria2 uses the download speed observed\n" \
    "                              in the previous downloads to adapt the number of\n" \
    "                              downloads launched in parallel according to the\n" \
    "                              rule N = A + B Log10(speed in Mbps). The\n" \
    "                              coefficients A and B can be customized in the\n" \
    "                              option arguments with A and B separated by a\n" \
    "                              colon. The default values (A=5,B=25) lead to\n" \
    "                              using typically 5 parallel downloads on 1Mbps\n" \
    "                              networks and above 50 on 100Mbps networks. The\n" \
    "                              number of parallel downloads remains constrained\n" \
    "                              under the maximum defined by the\n" \
    "                              max-concurrent-downloads parameter.")
#define TEXT_LOAD_COOKIES                                               \
  _(" --load-cookies=FILE          Load Cookies from FILE using the Firefox3 format\n" \
    "                              and Mozilla/Firefox(1.x/2.x)/Netscape format.")
#define TEXT_SAVE_COOKIES                                               \
  _(" --save-cookies=FILE          Save Cookies to FILE in Mozilla/Firefox(1.x/2.x)/\n" \
    "                              Netscape format. If FILE already exists, it is\n" \
    "                              overwritten. Session Cookies are also saved and\n" \
    "                              their expiry values are treated as 0.")
#define TEXT_SHOW_FILES                                                 \
  _(" -S, --show-files[=true|false] Print file listing of .torrent, .meta4 and\n" \
    "                              .metalink file and exit. More detailed\n" \
    "                              information will be listed in case of torrent\n" \
    "                              file.")
#define TEXT_SELECT_FILE                                                \
  _(" --select-file=INDEX...       Set file to download by specifying its index.\n" \
    "                              You can find the file index using the\n" \
    "                              --show-files option. Multiple indexes can be\n" \
    "                              specified by using ',', for example: \"3,6\".\n" \
    "                              You can also use '-' to specify a range: \"1-5\".\n" \
    "                              ',' and '-' can be used together.\n" \
    "                              When used with the -M option, index may vary\n" \
    "                              depending on the query(see --metalink-* options).")
#define TEXT_TORRENT_FILE                                               \
  _(" -T, --torrent-file=TORRENT_FILE  The path to the .torrent file.")
#define TEXT_FOLLOW_TORRENT                                             \
  _(" --follow-torrent=true|false|mem If true or mem is specified, when a file\n" \
    "                              whose suffix is .torrent or content type is\n" \
    "                              application/x-bittorrent is downloaded, aria2\n" \
    "                              parses it as a torrent file and downloads files\n" \
    "                              mentioned in it.\n"                  \
    "                              If mem is specified, a torrent file is not\n" \
    "                              written to the disk, but is just kept in memory.\n" \
    "                              If false is specified, the .torrent file is\n" \
    "                              downloaded to the disk, but is not parsed as a\n" \
    "                              torrent and its contents are not downloaded.")
#define TEXT_LISTEN_PORT                                                \
  _(" --listen-port=PORT          Set the TCP and UDP BitTorrent listen port.")
#define TEXT_MAX_OVERALL_UPLOAD_LIMIT                                   \
  _(" --max-overall-upload-limit=SPEED Set max overall upload speed in bytes/sec.\n" \
    "                              0 means unrestricted.\n"             \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.\n"                 \
    "                              To limit the upload speed per torrent, use\n" \
    "                              --max-upload-limit option.")
#define TEXT_MAX_UPLOAD_LIMIT                                           \
  _(" -u, --max-upload-limit=SPEED Set max upload speed per each torrent in\n" \
    "                              bytes/sec. 0 means unrestricted.\n"  \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.\n"                 \
    "                              To limit the overall upload speed, use\n" \
    "                              --max-overall-upload-limit option.")
#define TEXT_SEED_TIME                                                  \
  _(" --seed-time=MINUTES          Specify sharing time in (fractional) minutes.\n" \
    "                              Also see the --seed-ratio option.")
#define TEXT_SEED_RATIO                                                 \
  _(" --seed-ratio=RATIO           Specify share ratio. Share completed P2P\n" \
    "                              downloads until share ratio reaches RATIO.\n" \
    "                              You are strongly encouraged to specify equals or\n" \
    "                              more than 1.0 here. Specify 0.0 if you intend to\n" \
    "                              keep sharing regardless of share ratio.\n" \
    "                              If --seed-time option is specified along with\n" \
    "                              this option, sharing ends when at least one of\n" \
    "                              the conditions is satisfied.")
#define TEXT_ENABLE_PEER_EXCHANGE                                       \
  _(" --enable-peer-exchange[=true|false] Enable Peer Exchange extension.")
#define TEXT_ENABLE_DHT                                         \
  _(" --enable-dht[=true|false]    Enable IPv4 and IPv6 DHT. Private torrents never\n" \
    "                              use DHT.")
#define TEXT_BT_ENCRYPTION                                               \
  _(" --bt-encryption=MODE         Select preferred, required, or disabled peer\n" \
    "                              transport encryption.")
#define TEXT_BT_TRANSPORT                                                \
  _(" --bt-transport=MODE          Select tcp, utp, or both peer transports.")
#define TEXT_BT_DHT_BOOTSTRAP_NODES                                      \
  _(" --bt-dht-bootstrap-nodes=NODES Set comma-separated DHT bootstrap HOST:PORT entries.")
#define TEXT_BT_MAX_CONNECTIONS                                          \
  _(" --bt-max-connections=NUM    Set the session-wide peer connection limit.")
#define TEXT_BT_MAX_UPLOADS                                              \
  _(" --bt-max-uploads=NUM        Set the session-wide upload slot limit.")
#define TEXT_BT_PROXY                                                    \
  _(" --bt-proxy=URI              Route BitTorrent traffic through HTTP, SOCKS4, or SOCKS5.")
#define TEXT_BT_PORT_MAPPING                                             \
  _(" --bt-port-mapping[=true|false] Enable UPnP and NAT-PMP port mapping.")
#define TEXT_BT_MAX_OPEN_FILES                                          \
  _(" --bt-max-open-files=NUM      Specify the session-wide maximum number of open\n" \
    "                              BitTorrent files.")
#define TEXT_BT_IO_THREADS                                               \
  _(" --bt-io-threads=NUM          Set native libtorrent disk I/O threads.")
#define TEXT_BT_HASHING_THREADS                                          \
  _(" --bt-hashing-threads=NUM     Set native libtorrent recheck threads.")
#define TEXT_BT_CONNECTION_SPEED                                         \
  _(" --bt-connection-speed=NUM    Set outgoing BitTorrent connection attempts per second.")
#define TEXT_BT_MAX_OUT_REQUEST_QUEUE                                    \
  _(" --bt-max-out-request-queue=NUM Limit outstanding block requests sent to each peer.")
#define TEXT_BT_MAX_IN_REQUEST_QUEUE                                     \
  _(" --bt-max-in-request-queue=NUM Limit outstanding block requests accepted from each peer.")
#define TEXT_BT_DISK_QUEUE_SIZE                                          \
  _(" --bt-disk-queue-size=SIZE    Limit bytes waiting for native disk I/O.")
#define TEXT_BT_DISK_IO                                                  \
  _(" --bt-disk-io=MODE           Select default, pread, mmap, or posix disk I/O.")
#define TEXT_BT_DISK_READ_CACHE                                          \
  _(" --bt-disk-read-cache=MODE   Enable or disable the operating-system read cache.")
#define TEXT_BT_DISK_WRITE_CACHE                                         \
  _(" --bt-disk-write-cache=MODE  Select enabled, disabled, or write-through writes.")
#define TEXT_BT_CHECKING_MEMORY                                          \
  _(" --bt-checking-memory=SIZE   Limit memory used by torrent checking jobs.")
#define TEXT_BT_PIECE_EXTENT_AFFINITY                                   \
  _(" --bt-piece-extent-affinity[=true|false] Prefer adjacent piece extents.")
#define TEXT_BT_PEER_TURNOVER                                            \
  _(" --bt-peer-turnover=PERCENT  Replace this percentage of peers at the connection limit.")
#define TEXT_BT_PEER_TURNOVER_CUTOFF                                     \
  _(" --bt-peer-turnover-cutoff=PERCENT Set the peer-limit threshold for turnover.")
#define TEXT_BT_PEER_TURNOVER_INTERVAL                                   \
  _(" --bt-peer-turnover-interval=SEC Set the peer turnover interval.")
#define TEXT_BT_MIXED_MODE                                               \
  _(" --bt-mixed-mode=MODE        Select prefer-tcp or peer-proportional mixed transport.")
#define TEXT_BT_UPLOAD_SLOT_ALGORITHM                                    \
  _(" --bt-upload-slot-algorithm=MODE Select fixed or rate-based upload slots.")
#define TEXT_BT_SEED_CHOKING_ALGORITHM                                   \
  _(" --bt-seed-choking-algorithm=MODE Select the seed peer rotation policy.")
#define TEXT_BT_SEND_BUFFER_LOW_WATERMARK                                \
  _(" --bt-send-buffer-low-watermark=SIZE Set the initial per-peer send buffer target.")
#define TEXT_BT_SEND_BUFFER_WATERMARK                                    \
  _(" --bt-send-buffer-watermark=SIZE Set the maximum per-peer send buffer target.")
#define TEXT_BT_SEND_BUFFER_WATERMARK_FACTOR                             \
  _(" --bt-send-buffer-watermark-factor=PERCENT Scale send buffers from upload rate.")
#define TEXT_BT_SEEDING_OUTGOING_CONNECTIONS                             \
  _(" --bt-seeding-outgoing-connections[=true|false] Connect to peers while seeding.")
#define TEXT_BT_RATE_LIMIT_OVERHEAD                                      \
  _(" --bt-rate-limit-overhead[=true|false] Include protocol overhead in rate limits.")
#define TEXT_BT_STOP_TRACKER_TIMEOUT                                     \
  _(" --bt-stop-tracker-timeout=SEC Wait for stopped tracker announces on shutdown.")
#define TEXT_BT_BLOCKLIST_SCOPE                                          \
  _(" --bt-blocklist-scope=MODE  Apply the peer blocklist to peers, trackers, or DHT.")
#define TEXT_BT_RESUME_SAVE_INTERVAL                                     \
  _(" --bt-resume-save-interval=MIN Save native fast-resume data periodically.")
#define TEXT_BT_UPLOAD_SUGGESTIONS                                       \
  _(" --bt-upload-suggestions[=true|false] Suggest recently read pieces to peers.")
#define TEXT_BT_FILE_PRIORITY                                            \
  _(" --bt-file-priority=SPEC     Set INDEX=off|normal|high|top file priorities.")
#define TEXT_BT_MAX_CONCURRENT_HTTP_ANNOUNCES                            \
  _(" --bt-max-concurrent-http-announces=NUM Limit parallel HTTP tracker announces.")
#define TEXT_BT_ANNOUNCE_ALL_TIERS                                       \
  _(" --bt-announce-all-tiers[=true|false] Announce to every tracker tier.")
#define TEXT_BT_ANNOUNCE_ALL_TRACKERS                                    \
  _(" --bt-announce-all-trackers[=true|false] Announce to every tracker in a tier.")
#define TEXT_BT_USER_AGENT                                               \
  _(" --bt-user-agent=USER_AGENT Set the BitTorrent tracker and peer identity.")
#define TEXT_BT_PEER_ID_PREFIX                                           \
  _(" --bt-peer-id-prefix=PREFIX Set the BitTorrent peer ID prefix.")
#define TEXT_BT_ANONYMOUS_MODE                                           \
  _(" --bt-anonymous-mode[=true|false] Hide identifying client information.")
#define TEXT_BT_SEED_UNVERIFIED                                         \
  _(" --bt-seed-unverified[=true|false] Seed previously downloaded files without\n" \
    "                              verifying piece hashes.")
#define TEXT_BT_MAX_PEERS                                               \
  _(" --bt-max-peers=NUM           Specify the maximum number of peers per torrent.\n" \
    "                              0 means unlimited.")
#define TEXT_BT_MAX_UPLOADS_PER_TORRENT                                  \
  _(" --bt-max-uploads-per-torrent=NUM Set upload slots per torrent.")
#define TEXT_BT_FIRST_LAST_PIECE_FIRST                                   \
  _(" --bt-first-last-piece-first[=true|false] Prioritize file boundaries.")
#define TEXT_BT_SUPER_SEEDING                                            \
  _(" --bt-super-seeding[=true|false] Enable super seeding for this torrent.")
#define TEXT_BT_PEER_BLOCKLIST                                           \
  _(" --bt-peer-blocklist=PATH      Reject BitTorrent peers whose IP address matches\n" \
    "                              an IP or CIDR rule in PATH. Both IPv4 and IPv6\n" \
    "                              are supported. Lines beginning with '#' are ignored.")
#define TEXT_METALINK_FILE                                              \
  _(" -M, --metalink-file=METALINK_FILE The file path to the .meta4 and .metalink\n" \
    "                              file. Reads input from stdin when '-' is\n" \
    "                              specified.")
#define TEXT_METALINK_SERVERS                                           \
  _(" -C, --metalink-servers=NUM_SERVERS The number of servers to connect to\n" \
    "                              simultaneously. Some Metalinks regulate the\n" \
    "                              number of servers to connect. aria2 strictly\n" \
    "                              respects them. This means that if Metalink defines\n" \
    "                              the maxconnections attribute lower than\n" \
    "                              NUM_SERVERS, then aria2 uses the value of\n" \
    "                              maxconnections attribute instead of NUM_SERVERS.\n" \
    "                              See also -s and -j options.")
#define TEXT_METALINK_VERSION                                           \
  _(" --metalink-version=VERSION   The version of the file to download.")
#define TEXT_METALINK_LANGUAGE                                          \
  _(" --metalink-language=LANGUAGE The language of the file to download.")
#define TEXT_METALINK_OS                                                \
  _(" --metalink-os=OS             The operating system of the file to download.")
#define TEXT_METALINK_LOCATION                                          \
  _(" --metalink-location=LOCATION[,...] The location of the preferred server.\n" \
    "                              A comma-delimited list of locations is\n" \
    "                              acceptable.")
#define TEXT_METALINK_PREFERRED_PROTOCOL                                \
  _(" --metalink-preferred-protocol=PROTO Specify preferred protocol. Specify 'none'\n" \
    "                              if you don't have any preferred protocol.")
#define TEXT_FOLLOW_METALINK                                            \
  _(" --follow-metalink=true|false|mem If true or mem is specified, when a file\n" \
    "                              whose suffix is .meta4 or .metalink, or content\n" \
    "                              type of application/metalink4+xml or\n" \
    "                              application/metalink+xml is downloaded, aria2\n" \
    "                              parses it as a metalink file and downloads files\n" \
    "                              mentioned in it.\n"                  \
    "                              If mem is specified, a metalink file is not\n" \
    "                              written to the disk, but is just kept in memory.\n" \
    "                              If false is specified, the .metalink file is\n" \
    "                              downloaded to the disk, but is not parsed as a\n" \
    "                              metalink file and its contents are not\n" \
    "                              downloaded.")
#define TEXT_METALINK_ENABLE_UNIQUE_PROTOCOL                            \
  _(" --metalink-enable-unique-protocol[=true|false] If true is given and several\n" \
    "                              protocols are available for a mirror in a metalink\n" \
    "                              file, aria2 uses one of them.\n"     \
    "                              Use --metalink-preferred-protocol option to\n" \
    "                              specify the preference of protocol.")
#define TEXT_VERSION                                                    \
  _(" -v, --version                Print the version number and exit.")
#define TEXT_HELP                                                       \
  _(" -h, --help[=TAG|KEYWORD]     Print usage and exit.\n"             \
    "                              The help messages are classified with tags. A tag\n" \
    "                              starts with \"#\". For example, type \"--help=#http\"\n" \
    "                              to get the usage for the options tagged with\n" \
    "                              \"#http\". If non-tag word is given, print the usage\n" \
    "                              for the options whose name includes that word.")
#define TEXT_NO_CONF                                                    \
  _(" --no-conf[=true|false]       Disable loading aria2.conf file.")
#define TEXT_CONF_PATH                                                  \
  _(" --conf-path=PATH             Change the configuration file path to PATH.")
#define TEXT_STOP                                                       \
  _(" --stop=SEC                   Stop application after SEC seconds has passed.\n" \
    "                              If 0 is given, this feature is disabled.")
#define TEXT_HEADER                                                     \
  _(" --header=HEADER              Append HEADER to HTTP request header. You can use\n" \
    "                              this option repeatedly to specify more than one\n" \
    "                              header:\n"                           \
    "                              aria2-next --header=\"X-A: b78\" --header=\"X-B: 9J1\"\n" \
    "                              http://host/file")
#define TEXT_QUIET                                                      \
  _(" -q, --quiet[=true|false]     Make aria2 quiet(no console output).")
#define TEXT_SUMMARY_INTERVAL                                           \
  _(" --summary-interval=SEC       Set interval to output download progress summary.\n" \
    "                              Setting 0 suppresses the output.")
#define TEXT_LOG_LEVEL                                          \
  _(" --log-level=LEVEL            Set log level to output to file specified using\n" \
    "                             --log option.")
#define TEXT_LOG_MAX_SIZE                                               \
  _(" --log-max-size=SIZE          Set the maximum size of each log file.")
#define TEXT_LOG_MAX_FILES                                              \
  _(" --log-max-files=N            Set the maximum number of log files, including\n" \
    "                              the active file.")
#define TEXT_REMOTE_TIME                                                \
  _(" -R, --remote-time[=true|false] Retrieve timestamp of the remote file from the\n" \
    "                              remote HTTP/SFTP server and if it is available,\n" \
    "                              apply it to the local file.")
#define TEXT_CONNECT_TIMEOUT                                            \
  _(" --connect-timeout=SEC        Set the connect timeout in seconds to establish\n" \
    "                              connection to HTTP/SFTP/proxy server. After the\n" \
    "                              connection is established, this option makes no\n" \
    "                              effect and --timeout option is used instead.")
#define TEXT_MAX_FILE_NOT_FOUND                                         \
  _(" --max-file-not-found=NUM     If aria2 receives `file not found' status from the\n" \
    "                              remote HTTP/SFTP servers NUM times without getting\n" \
    "                              a single byte, then force the download to fail.\n" \
    "                              Specify 0 to disable this option.\n" \
    "                              This options is effective only when using\n" \
    "                              HTTP/SFTP servers. The number of retry attempt is\n" \
    "                              counted toward --max-tries, so it should be\n" \
    "                              configured too.")
#define TEXT_ED2K_SERVER                                                \
  _(" --ed2k-server=HOST:PORT[,..] Use ED2K servers to discover file sources.")
#define TEXT_ED2K_SERVER_LIST                                           \
  _(" --ed2k-server-list=FILE      Load ED2K servers from a local server.met file.")
#define TEXT_ED2K_NODE_LIST                                             \
  _(" --ed2k-node-list=FILE        Load ED2K Kad bootstrap nodes from a local nodes.dat file.")
#define TEXT_ED2K_LISTEN_PORT                                           \
  _(" --ed2k-listen-port=PORT     Set TCP port number for incoming ED2K peer connections.")
#define TEXT_ED2K_UDP_LISTEN_PORT                                       \
  _(" --ed2k-udp-listen-port=PORT Set UDP port number for ED2K Kad and peer reask packets.")
#define TEXT_ED2K_UPLOAD_SLOTS                                          \
  _(" --ed2k-upload-slots=NUM     Set maximum active ED2K upload slots.")
#define TEXT_ED2K_MAX_CONNECTIONS                                      \
  _(" --ed2k-max-connections=NUM  Set maximum concurrent ED2K peer connections.")
#define TEXT_ED2K_PREVIEW_PRIORITY                                      \
  _(" --ed2k-preview-priority[=true|false] Prioritize first and last ED2K parts. (default: false)")
#define TEXT_STATE_DIR                                                  \
  _(" --state-dir=DIR             Store persistent engine state under DIR.")
#define TEXT_STATE_SAVE_INTERVAL                                        \
  _(" --state-save-interval=SEC    Checkpoint persistent engine state every SEC seconds.")
#define TEXT_CERTIFICATE                                                \
  _(" --certificate=FILE           Use the client certificate in FILE.\n" \
    "                              The certificate must be in PEM format.\n" \
    "                              You may use --private-key option to specify the\n" \
    "                              private key.")
#define TEXT_PRIVATE_KEY                                                \
  _(" --private-key=FILE           Use the private key in FILE.\n"      \
    "                              The private key must be decrypted and in PEM\n" \
    "                              format. See also --certificate option.")
#define TEXT_CA_CERTIFICATE                                             \
  _(" --ca-certificate=FILE        Use the certificate authorities in FILE to verify\n" \
    "                              the peers. The certificate file must be in PEM\n" \
    "                              format and can contain multiple CA certificates.\n" \
    "                              Use --check-certificate option to enable\n" \
    "                              verification.")
#define TEXT_CHECK_CERTIFICATE                                          \
  _(" --check-certificate[=true|false] Verify the peer using certificates specified\n" \
    "                              in --ca-certificate option.")
#define TEXT_NO_PROXY                                                   \
  _(" --no-proxy=DOMAINS           Specify comma separated hostnames, domains or\n" \
    "                              network address with or without CIDR block where\n" \
    "                              proxy should not be used.")
#define TEXT_EVENT_POLL                                                 \
  _(" --event-poll=POLL            Specify the method for polling events.")
#define TEXT_BT_EXTERNAL_IP                                             \
  _(" --bt-external-ip=IPADDRESS   Specify the external IP address announced to\n" \
    "                              BitTorrent trackers and DHT.")
#define TEXT_BT_EXTERNAL_PORT                                           \
  _(" --bt-external-port=PORT      Specify the external TCP port announced to\n" \
    "                              trackers, DHT, and supported peers.")
#define TEXT_INDEX_OUT                                                  \
  _(" -O, --index-out=INDEX=PATH   Set file path for file with index=INDEX. You can\n" \
    "                              find the file index using the --show-files option.\n" \
    "                              PATH is a relative path to the path specified in\n" \
    "                              --dir option. You can use this option multiple\n" \
    "                              times.")
#define TEXT_DRY_RUN                                                    \
  _(" --dry-run[=true|false]       If true is given, aria2 just checks whether the\n" \
    "                              remote file is available and doesn't download\n" \
    "                              data. This option has effect on HTTP downloads.\n" \
    "                              BitTorrent downloads are canceled if true is\n" \
    "                              specified.")
#define TEXT_ON_DOWNLOAD_COMPLETE                                       \
  _(" --on-download-complete=COMMAND Set the command to be executed after download\n" \
    "                              completed.\n"                        \
    "                              See --on-download-start option for the\n" \
    "                              requirement of COMMAND.\n"           \
    "                              See also --on-download-stop option.")
#define TEXT_ON_DOWNLOAD_START                                          \
  _(" --on-download-start=COMMAND  Set the command to be executed after download\n" \
    "                              got started. aria2 passes 3 arguments to COMMAND:\n" \
    "                              GID, the number of files and file path. See Event\n" \
    "                              Hook in man page for more details.")
#define TEXT_ON_DOWNLOAD_PAUSE                                          \
  _(" --on-download-pause=COMMAND  Set the command to be executed after download\n" \
    "                              was paused.\n"\
    "                              See --on-download-start option for the\n" \
    "                              requirement of COMMAND.")
#define TEXT_ON_DOWNLOAD_ERROR                                          \
  _(" --on-download-error=COMMAND  Set the command to be executed after download\n" \
    "                              aborted due to error.\n"              \
    "                              See --on-download-start option for the\n" \
    "                              requirement of COMMAND.\n"           \
    "                              See also --on-download-stop option.")
#define TEXT_ON_DOWNLOAD_STOP                                           \
  _(" --on-download-stop=COMMAND   Set the command to be executed after download\n" \
    "                              stopped. You can override the command to be\n" \
    "                              executed for particular download result using\n" \
    "                              --on-download-complete and --on-download-error. If\n" \
    "                              they are specified, command specified in this\n" \
    "                              option is not executed.\n"           \
    "                              See --on-download-start option for the\n" \
    "                              requirement of COMMAND.")
#define TEXT_INTERFACE                                                  \
  _(" --interface=INTERFACE        Bind sockets to given interface. You can specify\n" \
    "                              interface name, IP address and hostname.\n" \
    "                              BitTorrent uses --bt-interface.")
#define TEXT_MULTIPLE_INTERFACE                                         \
  _(" --multiple-interface=INTERFACES Comma separated list of interfaces to bind\n" \
    "                              sockets to. Requests will be split among the\n" \
    "                              interfaces to achieve link aggregation. You can\n" \
    "                              specify interface name, IP address and hostname.\n" \
    "                              If --interface is used, this option will be\n" \
    "                              ignored. BitTorrent uses --bt-interface.")
#define TEXT_DISABLE_IPV6                               \
  _(" --disable-ipv6[=true|false]  Disable IPv6.")
#define TEXT_HTTP_NO_CACHE                      \
  _(" --http-no-cache[=true|false] Send Cache-Control: no-cache and Pragma: no-cache\n" \
    "                              header to avoid cached content.  If false is\n" \
    "                              given, these headers are not sent and you can add\n" \
    "                              Cache-Control header with a directive you like\n" \
    "                              using --header option.")
#define TEXT_HUMAN_READABLE                     \
  _(" --human-readable[=true|false] Print sizes and speed in human readable format\n" \
    "                              (e.g., 1.2Ki, 3.4Mi) in the console readout.")
#define TEXT_BT_ENABLE_LPD                      \
  _(" --bt-enable-lpd[=true|false] Enable Local Peer Discovery.")
#define TEXT_ALL_PROXY_USER                                             \
  _(" --all-proxy-user=USER        Set user for --all-proxy.")
#define TEXT_ALL_PROXY_PASSWD                                           \
  _(" --all-proxy-passwd=PASSWD    Set password for --all-proxy.")
#define TEXT_HTTP_PROXY_USER                                            \
  _(" --http-proxy-user=USER       Set user for --http-proxy.")
#define TEXT_HTTP_PROXY_PASSWD                                          \
  _(" --http-proxy-passwd=PASSWD   Set password for --http-proxy.")
#define TEXT_HTTPS_PROXY_USER                                           \
  _(" --https-proxy-user=USER      Set user for --https-proxy.")
#define TEXT_HTTPS_PROXY_PASSWD                                         \
  _(" --https-proxy-passwd=PASSWD  Set password for --https-proxy.")
#define TEXT_BT_TRACKER_COMPLETION_TIMEOUT                              \
  _(" --bt-tracker-completion-timeout=SEC Set the total tracker request timeout.")
#define TEXT_BT_TRACKER_RECEIVE_TIMEOUT                                 \
  _(" --bt-tracker-receive-timeout=SEC Set the tracker no-data timeout.")
#define TEXT_BT_INTERFACE                                                  \
  _(" --bt-interface=INTERFACE,... Bind BitTorrent traffic to interfaces or IPs.\n" \
    "                              By default, listen on all addresses and let the\n" \
    "                              operating system select outgoing routes.")
#define TEXT_HTTP_ACCEPT_GZIP                   \
  _(" --http-accept-gzip[=true|false] Send 'Accept-Encoding: deflate, gzip' request\n" \
    "                              header and inflate response if remote server\n" \
    "                              responds with 'Content-Encoding: gzip' or\n"  \
    "                              'Content-Encoding: deflate'.")
#define TEXT_SAVE_SESSION                       \
  _(" --save-session=FILE          Save error/unfinished downloads to FILE on exit.\n" \
    "                              You can pass this output file to aria2-next with -i\n" \
    "                              option on restart. Please note that downloads\n" \
    "                              added by aria2.addMetalink whose metadata could\n" \
    "                              not be saved will not be restored. BitTorrent\n" \
    "                              uploads use private state. Downloads removed\n" \
    "                              using aria2.remove and aria2.forceRemove will not\n" \
    "                              be saved.")
#define TEXT_ED2K_MIN_SPLIT_SIZE                                       \
  _(" --ed2k-min-split-size=SIZE Keep at least SIZE bytes between parallel ED2K ranges.")
#define TEXT_ON_BT_DOWNLOAD_COMPLETE            \
  _(" --on-bt-download-complete=COMMAND For BitTorrent, a command specified in\n" \
    "                              --on-download-complete is called after download\n" \
    "                              completed and seeding is over. On the other hand,\n" \
    "                              this option sets the command to be executed after\n" \
    "                              download completed but before seeding.\n" \
    "                              See --on-download-start option for the\n" \
    "                              requirement of COMMAND.")
#define TEXT_BT_TRACKER                                                 \
  _(" --bt-tracker=URI[,...]       Comma separated list of additional BitTorrent\n" \
    "                              tracker announce URIs in one fallback tier.\n" \
    "                              These URIs are not\n" \
    "                              affected by --bt-exclude-tracker option because\n" \
    "                              they are added after URIs in --bt-exclude-tracker\n" \
    "                              option are removed.")
#define TEXT_BT_EXCLUDE_TRACKER                                         \
  _(" --bt-exclude-tracker=URI[,...] Comma separated list of BitTorrent tracker's\n" \
    "                              announce URI to remove. You can use special value\n" \
    "                              '*' which matches all URIs, thus removes all\n" \
    "                              announce URIs. When specifying '*' in shell\n" \
    "                              command-line, don't forget to escape or quote it.\n" \
    "                              See also --bt-tracker option.")
#define TEXT_MAX_DOWNLOAD_RESULT                \
  _(" --max-download-result=NUM    Set maximum number of download result kept in\n" \
    "                              memory. The download results are completed/error/\n" \
    "                              removed downloads. The download results are stored\n" \
    "                              in FIFO queue and it can store at most NUM\n" \
    "                              download results. When queue is full and new\n" \
    "                              download result is created, oldest download result\n" \
    "                              is removed from the front of the queue and new one\n" \
    "                              is pushed to the back. Setting big number in this\n" \
    "                              option may result high memory consumption after\n" \
    "                              thousands of downloads. Specifying 0 means no\n" \
    "                              download result is kept. Note that unfinished\n" \
    "                              downloads are kept in memory regardless of this\n" \
    "                              option value. See\n" \
    "                              --keep-unfinished-download-result option.")
#define TEXT_ENABLE_RPC                                               \
  _(" --enable-rpc[=true|false]    Enable JSON-RPC/XML-RPC server.\n" \
    "                              It is strongly recommended to set secret\n" \
    "                              authorization token using --rpc-secret option.\n" \
    "                              See also --rpc-listen-port option.")
#define TEXT_RPC_MAX_REQUEST_SIZE                                   \
  _(" --rpc-max-request-size=SIZE  Set max size of JSON-RPC/XML-RPC request. If aria2\n" \
    "                              detects the request is more than SIZE bytes, it\n" \
    "                              drops connection. Decimal values are allowed.\n" \
    "                              You can append K or M(1K = 1024, 1M = 1024K).\n" \
    "                              Fractional bytes are rounded down.")
#define TEXT_RPC_LISTEN_ALL                                         \
  _(" --rpc-listen-all[=true|false] Listen incoming JSON-RPC/XML-RPC requests on all\n" \
    "                              network interfaces. If false is given, listen only\n" \
    "                              on local loopback interface.")
#define TEXT_RPC_LISTEN_PORT                                        \
  _(" --rpc-listen-port=PORT       Specify a port number for JSON-RPC/XML-RPC server\n" \
    "                              to listen to.")
#define TEXT_SHOW_CONSOLE_READOUT                                       \
  _(" --show-console-readout[=true|false] Show console readout.")
#define TEXT_METALINK_BASE_URI                  \
  _(" --metalink-base-uri=URI      Specify base URI to resolve relative URI in\n" \
    "                              metalink:url and metalink:metaurl element in a\n" \
    "                              metalink file stored in local disk. If URI points\n" \
    "                              to a directory, URI must end with '/'.")
#define TEXT_ED2K_PIECE_SELECTOR                \
  _(" --ed2k-piece-selector=SELECTOR Specify piece selection algorithm\n" \
    "                              used in ED2K downloads. Piece means fixed\n" \
    "                              length segment which is downloaded in parallel\n" \
    "                              in segmented download. If 'default' is given,\n" \
    "                              aria2 selects piece so that it reduces the\n" \
    "                              number of establishing connection. This is\n" \
    "                              reasonable default behaviour because\n" \
    "                              establishing connection is an expensive\n" \
    "                              operation.\n"                        \
    "                              If 'inorder' is given, aria2 selects piece which\n" \
    "                              has minimum index. Index=0 means first of the\n" \
    "                              file. This will be useful to view movie while\n" \
    "                              Please note that aria2 honors\n"     \
    "                              --ed2k-min-split-size option, so it will be necessary\n" \
    "                              to specify a reasonable value to\n"  \
    "                              --ed2k-min-split-size option.\n"     \
    "                              If 'random' is given, aria2 selects piece\n" \
    "                              randomly. Like 'inorder', --ed2k-min-split-size\n" \
    "                              option is honored.\n"                \
    "                              If 'geom' is given, at the beginning aria2\n" \
    "                              selects piece which has minimum index like\n" \
    "                              'inorder', but it exponentially increasingly\n" \
    "                              keeps space from previously selected piece. This\n" \
    "                              will reduce the number of establishing connection\n" \
    "                              and at the same time it will download the\n" \
    "                              beginning part of the file first. This will be\n" \
    "                              useful to view movie while downloading it.")
#define TEXT_TRUNCATE_CONSOLE_READOUT                                   \
  _(" --truncate-console-readout[=true|false] Truncate console readout to fit in\n"\
    "                              a single line.")
#define TEXT_PAUSE                              \
  _(" --pause[=true|false]         Pause download after added. This option is\n" \
    "                              effective only when --enable-rpc=true is given.")
#define TEXT_RPC_ALLOW_ORIGIN_ALL                                       \
  _(" --rpc-allow-origin-all[=true|false] Add Access-Control-Allow-Origin header\n" \
    "                              field with value '*' to the RPC response.")
#define TEXT_DOWNLOAD_RESULT                    \
  _(" --download-result=OPT        This option changes the way \"Download Results\"\n" \
    "                              is formatted. If OPT is 'default', print GID,\n" \
    "                              status, average download speed and path/URI. If\n" \
    "                              multiple files are involved, path/URI of first\n" \
    "                              requested file is printed and remaining ones are\n" \
    "                              omitted.\n"                          \
    "                              If OPT is 'full', print GID, status, average\n" \
    "                              download speed, percentage of progress and\n" \
    "                              path/URI. The percentage of progress and\n" \
    "                              path/URI are printed for each requested file in\n" \
    "                              each row.\n" \
    "                              If OPT is 'hide', \"Download Results\" is hidden.")
#define TEXT_HASH_CHECK_ONLY                    \
  _(" --hash-check-only[=true|false] If true is given, after hash check using\n" \
    "                              --check-integrity option, abort download whether\n" \
    "                              or not download is complete.")
#define TEXT_CHECKSUM                                                   \
  _(" --checksum=TYPE=DIGEST       Set checksum. TYPE is hash type. The supported\n" \
    "                              hash type is listed in \"Hash Algorithms\" in\n" \
    "                              \"aria2-next -v\". DIGEST is hex digest.\n" \
    "                              For example, setting sha-1 digest looks like\n" \
    "                              this:\n"                             \
    "                              sha-1=0192ba11326fe2298c8cb4de616f4d4140213838\n" \
    "                              This option applies only to HTTP(S)/SFTP\n" \
    "                              downloads.")
#define TEXT_PIECE_LENGTH                       \
  _(" --piece-length=LENGTH        Set a piece length for segmented downloads. This\n" \
    "                              is the boundary when aria2 splits a file. All\n" \
    "                              splits occur at multiple of this length. This\n" \
    "                              option will be ignored in BitTorrent downloads.\n" \
    "                              It will be also ignored if Metalink file\n" \
    "                              contains piece hashes.")
#define TEXT_STOP_WITH_PROCESS                                          \
  _(" --stop-with-process=PID      Stop application when process PID is not running.\n" \
    "                              This is useful if aria2 process is forked from a\n" \
    "                              parent process. The parent process can fork aria2\n" \
    "                              with its own pid and when parent process exits\n" \
    "                              for some reason, aria2 can detect it and shutdown\n" \
    "                              itself.")
#define TEXT_DEFERRED_INPUT                     \
  _(" --deferred-input[=true|false] If true is given, aria2 does not read all URIs\n" \
    "                              and options from file specified by -i option at\n" \
    "                              startup, but it reads one by one when it needs\n" \
    "                              later. This may reduce memory usage if input\n" \
    "                              file contains a lot of URIs to download.\n" \
    "                              If false is given, aria2 reads all URIs and\n" \
    "                              options at startup.")
#define TEXT_ENABLE_MMAP                        \
  _(" --enable-mmap[=true|false]   Map files into memory.")
#define TEXT_RPC_CERTIFICATE                                            \
  _(" --rpc-certificate=FILE       Use the certificate in FILE for RPC server.\n" \
    "                              The certificate must be in PEM format.\n" \
    "                              Use --rpc-private-key option to specify the\n" \
    "                              private key. Use --rpc-secure option to enable\n" \
    "                              encryption.")
#define TEXT_RPC_PRIVATE_KEY                                            \
  _(" --rpc-private-key=FILE       Use the private key in FILE for RPC server.\n" \
    "                              The private key must be decrypted and in PEM\n" \
    "                              format. Use --rpc-secure option to enable\n" \
    "                              encryption. See also --rpc-certificate option.")
#define TEXT_RPC_SECURE                         \
  _(" --rpc-secure[=true|false]    RPC transport will be encrypted by SSL/TLS.\n" \
    "                              The RPC clients must use https scheme to access\n" \
    "                              the server. For WebSocket client, use wss\n" \
    "                              scheme. Use --rpc-certificate and\n" \
    "                              --rpc-private-key options to specify the\n" \
    "                              server certificate and private key.")
#define TEXT_RPC_SAVE_UPLOAD_METADATA                                   \
  _(" --rpc-save-upload-metadata[=true|false] Save uploaded metalink metadata\n" \
    "                              in the directory specified by --dir. Torrent\n" \
    "                              uploads are always private engine state under\n" \
    "                              --state-dir and never enter the download\n" \
    "                              directory.")
#define TEXT_FORCE_SAVE                         \
  _(" --force-save[=true|false]    Save download with --save-session option even\n" \
    "                              if the download is completed or removed. This\n" \
    "                              may be useful to save\n" \
    "                              BitTorrent seeding which is recognized as\n" \
    "                              completed state.")
#define TEXT_SAVE_NOT_FOUND                         \
  _(" --save-not-found[=true|false] Save download with --save-session option even\n" \
    "                              if the file was not found on the server.")
#define TEXT_DISK_CACHE                         \
  _(" --disk-cache=SIZE            Enable disk cache. If SIZE is 0, the disk cache\n" \
    "                              is disabled. This feature caches the downloaded\n" \
    "                              data in memory, which grows to at most SIZE\n" \
    "                              bytes. The cache storage is created for aria2\n" \
    "                              instance and shared by all downloads. The one\n" \
    "                              advantage of the disk cache is reduce the disk\n" \
    "                              I/O because the data are written in larger unit\n" \
    "                              and it is reordered by the offset of the file.\n" \
    "                              If hash checking is involved and the data are\n" \
    "                              cached in memory, we don't need to read them\n" \
    "                              from the disk.\n"                    \
    "                              Decimal SIZE values can include K or M\n" \
    "                              (1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.")
#define TEXT_GID                                \
  _(" --gid=GID                    Set GID manually. aria2 identifies each\n" \
    "                              download by the ID called GID. The GID must be\n" \
    "                              hex string of 16 characters, thus [0-9a-fA-F]\n" \
    "                              are allowed and leading zeros must not be\n" \
    "                              stripped. The GID all 0 is reserved and must\n" \
    "                              not be used. The GID must be unique, otherwise\n" \
    "                              error is reported and the download is not added.\n" \
    "                              This option is useful when restoring the\n" \
    "                              sessions saved using --save-session option. If\n" \
    "                              this option is not used, new GID is generated\n" \
    "                              by aria2.")
#define TEXT_CONSOLE_LOG_LEVEL                                          \
  _(" --console-log-level=LEVEL    Set log level to output to console.")
#define TEXT_SAVE_SESSION_INTERVAL                                      \
  _(" --save-session-interval=SEC  Save error/unfinished downloads to a file\n" \
    "                              specified by --save-session option every SEC\n" \
    "                              seconds. If 0 is given, file will be saved only\n" \
    "                              when aria2 exits.")
#define TEXT_ENABLE_COLOR                                               \
  _(" --enable-color[=true|false]  Enable color output for a terminal.")
#define TEXT_RPC_SECRET                                                 \
  _(" --rpc-secret=TOKEN           Set RPC secret authorization token.")
#define TEXT_DSCP                                                       \
  _(" --dscp=DSCP                  Set DSCP value in outgoing IP packets of\n" \
    "                              BitTorrent traffic for QoS. This parameter sets\n" \
    "                              only DSCP bits in TOS field of IP packets,\n" \
    "                              not the whole field. If you take values\n" \
    "                              from /usr/include/netinet/ip.h divide them by 4\n" \
    "                              (otherwise values would be incorrect, e.g. your\n" \
    "                              CS1 class would turn into CS4). If you take\n" \
    "                              commonly used values from RFC, network vendors'\n" \
    "                              documentation, Wikipedia or any other source,\n" \
    "                              use them as they are.")
#define TEXT_RLIMIT_NOFILE                                              \
  _(" --rlimit-nofile=NUM          Set the soft limit of open file descriptors.\n" \
    "                              This open will only have effect when:\n" \
    "                                a) The system supports it (posix)\n" \
    "                                b) The limit does not exceed the hard limit.\n" \
    "                                c) The specified limit is larger than the\n" \
    "                                   current soft limit.\n" \
    "                              This is equivalent to setting nofile via ulimit,\n" \
    "                              except that it will never decrease the limit.")
#define TEXT_PAUSE_METADATA                  \
  _(" --pause-metadata[=true|false]\n"       \
    "                              Pause a Magnet download on the same GID after its\n" \
    "                              file metadata is available and before payload is\n" \
    "                              requested. Set a valid select-file before unpausing.\n" \
    "                              Generated Metalink and torrent downloads are also\n" \
    "                              paused before they start. This option is effective\n" \
    "                              only when --enable-rpc=true is given.")
#define TEXT_DETACH_SHARE_ONLY                \
  _(" --detach-share-only[=true|false]\n"     \
    "                              Exclude share-only P2P downloads when counting\n" \
    "                              concurrent active downloads (See -j option).\n" \
    "                              This means that if -j3 is given and this option\n" \
    "                              is turned on and 3 downloads are active and one\n" \
    "                              of those enters share-only mode, then it is excluded\n" \
    "                              from active download count (thus it becomes 2),\n" \
    "                              and the next download waiting in queue gets\n" \
    "                              started. But be aware that share-only item is still\n" \
    "                              recognized as active download in RPC method.")
#define TEXT_MIN_TLS_VERSION                                            \
  _(" --min-tls-version=VERSION    Specify minimum SSL/TLS version to enable.")
#define TEXT_SSH_HOST_KEY_SHA256                                       \
  _(" --ssh-host-key-sha256=DIGEST Validate the SFTP server host key with its\n" \
    "                              SHA-256 Base64 digest.")
#define TEXT_SOCKET_RECV_BUFFER_SIZE                                    \
  _(" --socket-recv-buffer-size=SIZE\n"                                 \
    "                              Set the maximum socket receive buffer in bytes.\n" \
    "                              Specifying 0 will disable this option. This value\n" \
    "                              will be set to socket file descriptor using\n" \
    "                              SO_RCVBUF socket option with setsockopt() call.\n" \
    "                              Decimal values are allowed. You can append K or\n" \
    "                              M(1K = 1024, 1M = 1024K). Fractional bytes are\n" \
    "                              rounded down.")
#define TEXT_MAX_MMAP_LIMIT                                             \
  _(" --max-mmap-limit=SIZE        Set the maximum file size to enable mmap (see\n" \
    "                              --enable-mmap option). The file size is\n" \
    "                              determined by the sum of all files contained in\n" \
    "                              one download. For example, if a download\n" \
    "                              contains 5 files, then file size is the total\n" \
    "                              size of those files. If file size is strictly\n" \
    "                              greater than the size specified in this option,\n" \
    "                              mmap will be disabled. Decimal values are allowed.\n" \
    "                              You can append K or M(1K = 1024, 1M = 1024K).\n" \
    "                              Fractional bytes are rounded down.")
#define TEXT_STDERR \
  _(" --stderr[=true|false]        Redirect all console output that would be\n" \
    "                              otherwise printed in stdout to stderr.")
#define TEXT_KEEP_UNFINISHED_DOWNLOAD_RESULT \
  _(" --keep-unfinished-download-result[=true|false]\n" \
    "                              Keep unfinished download results even if doing\n" \
    "                              so exceeds --max-download-result. This is useful\n" \
    "                              if all unfinished downloads must be saved in\n" \
    "                              session file (see --save-session option). Please\n" \
    "                              keep in mind that there is no upper bound to the\n" \
    "                              number of unfinished download result to keep. If\n" \
    "                              that is undesirable, turn this option off.")

// clang-format on
