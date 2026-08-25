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
#ifndef D_EPOLL_EVENT_POLL_H
#define D_EPOLL_EVENT_POLL_H

#include "EventPoll.h"

#include <sys/epoll.h>

#include <map>

#include "Event.h"
#include "a2functional.h"

namespace aria2 {

class EpollEventPoll : public EventPoll {
private:
  class KSocketEntry;

  typedef Event<KSocketEntry> KEvent;
  typedef CommandEvent<KSocketEntry, EpollEventPoll> KCommandEvent;

  class KSocketEntry : public SocketEntry<KCommandEvent> {
  public:
    KSocketEntry(sock_t socket);

    KSocketEntry(const KSocketEntry&) = delete;
    KSocketEntry(KSocketEntry&&) = default;

    struct epoll_event getEvents();
  };

  friend int accumulateEvent(int events, const KEvent& event);

private:
  typedef std::map<sock_t, KSocketEntry> KSocketEntrySet;
  KSocketEntrySet socketEntries_;

  int epfd_;

  size_t epEventsSize_;

  std::unique_ptr<struct epoll_event[]> epEvents_;

  static constexpr size_t EPOLL_EVENTS_MAX = 1024;

  bool addEvents(sock_t socket, const KEvent& event);

  bool deleteEvents(sock_t socket, const KEvent& event);

public:
  EpollEventPoll();

  bool good() const;

  virtual ~EpollEventPoll();

  virtual void poll(const struct timeval& tv) override;

  virtual bool addEvents(sock_t socket, Command* command,
                         EventPoll::EventType events) override;

  virtual bool deleteEvents(sock_t socket, Command* command,
                            EventPoll::EventType events) override;

  static constexpr int IEV_READ = EPOLLIN;
  static constexpr int IEV_WRITE = EPOLLOUT;
  static constexpr int IEV_ERROR = EPOLLERR;
  static constexpr int IEV_HUP = EPOLLHUP;
};

} // namespace aria2

#endif // D_EPOLL_EVENT_POLL_H
