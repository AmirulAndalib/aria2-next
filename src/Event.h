/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2010 Tatsuhiro Tsujikawa
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
#ifndef D_EVENT_H
#define D_EVENT_H

#include "common.h"

#include <deque>
#include <algorithm>
#include <functional>
#include <memory>

#include "a2netcompat.h"
#include "Command.h"

namespace aria2 {

template <typename SocketEntry> class Event {
public:
  virtual ~Event() = default;

  virtual void processEvents(int events) = 0;

  virtual int getEvents() const = 0;

  virtual void addSelf(SocketEntry* socketEntry) const = 0;

  virtual void removeSelf(SocketEntry* socketEntry) const = 0;
};

template <typename SocketEntry, typename EventPoll>
class CommandEvent : public Event<SocketEntry> {
private:
  Command* command_;
  int events_;

public:
  CommandEvent(Command* command, int events)
      : command_(command), events_(events)
  {
  }

  Command* getCommand() const { return command_; }

  void addEvents(int events) { events_ |= events; }

  void removeEvents(int events) { events_ &= (~events); }

  bool eventsEmpty() const { return events_ == 0; }

  bool operator==(const CommandEvent& commandEvent) const
  {
    return command_ == commandEvent.command_;
  }

  virtual int getEvents() const { return events_; }

  virtual void processEvents(int events)
  {
    if ((events_ & events) ||
        ((EventPoll::IEV_ERROR | EventPoll::IEV_HUP) & events)) {
      command_->setStatusActive();
    }
    if (EventPoll::IEV_READ & events) {
      command_->readEventReceived();
    }
    if (EventPoll::IEV_WRITE & events) {
      command_->writeEventReceived();
    }
    if (EventPoll::IEV_ERROR & events) {
      command_->errorEventReceived();
    }
    if (EventPoll::IEV_HUP & events) {
      command_->hupEventReceived();
    }
  }

  virtual void addSelf(SocketEntry* socketEntry) const
  {
    socketEntry->addCommandEvent(*this);
  }

  virtual void removeSelf(SocketEntry* socketEntry) const
  {
    socketEntry->removeCommandEvent(*this);
  }
};

template <typename CommandEvent> class SocketEntry {
protected:
  sock_t socket_;

  std::deque<CommandEvent> commandEvents_;

public:
  SocketEntry(sock_t socket) : socket_(socket) {}

  SocketEntry(const SocketEntry&) = delete;
  SocketEntry(SocketEntry&&) = default;

  bool operator==(const SocketEntry& entry) const
  {
    return socket_ == entry.socket_;
  }

  bool operator<(const SocketEntry& entry) const
  {
    return socket_ < entry.socket_;
  }

  void addCommandEvent(const CommandEvent& cev)
  {
    typename std::deque<CommandEvent>::iterator i =
        std::find(commandEvents_.begin(), commandEvents_.end(), cev);
    if (i == commandEvents_.end()) {
      commandEvents_.push_back(cev);
    }
    else {
      (*i).addEvents(cev.getEvents());
    }
  }

  void removeCommandEvent(const CommandEvent& cev)
  {
    typename std::deque<CommandEvent>::iterator i =
        std::find(commandEvents_.begin(), commandEvents_.end(), cev);
    if (i == commandEvents_.end()) {
      // not found
    }
    else {
      (*i).removeEvents(cev.getEvents());
      if ((*i).eventsEmpty()) {
        commandEvents_.erase(i);
      }
    }
  }

  sock_t getSocket() const { return socket_; }

  void setSocket(sock_t socket) { socket_ = socket; }

  bool eventEmpty() const { return commandEvents_.empty(); }

  void processEvents(int events)
  {
    using namespace std::placeholders;
    std::for_each(commandEvents_.begin(), commandEvents_.end(),
                  std::bind(&CommandEvent::processEvents, _1, events));
  }
};

} // namespace aria2

#endif // D_EVENT_H
