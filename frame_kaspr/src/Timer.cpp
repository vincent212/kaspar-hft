/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include "frame/mda/msg/Data.hpp"
#include "frame/ob/msg/EndOfBurst2.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/mtim/msg/TimeOutSub.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "chutil/Time.hpp"
#include "actors/msg/Start.hpp"

#include "frame/mtim/act/Timer.hpp"

using namespace frame;
using namespace frame::mtim;

act::Timer::Timer(
    actor_ptr book) : book(book)
{
  ASSERT(the_timer == 0, "timer instantiated already");
  the_timer = this;

  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(ob::msg::EndOfBurst2, eob2_handler);
  MESSAGE_HANDLER(msg::TimeOutSub, timeout_sub_handler);
  MESSAGE_HANDLER(msg::AlarmClockSub, alarm_sub_handler);
}

void act::Timer::start_handler(const actors::msg::Start*) noexcept
{
  int cnt = 0;
  if (book)
  {
    mda::msg::Subscribe *m_ = new mda::msg::Subscribe(mda::msg::Subscribe::LOW);
    book->send(m_, this);
    cnt++;
  }
  ASSERT(cnt>0,"no adapters to subsribe to");
}

void act::Timer::eob2_handler(const ob::msg::EndOfBurst2* msg) noexcept
{
#ifdef DEBUGtimer_EOB
  std::cerr << "Timer: EndOfBurst2 received" << std::endl;
#endif
  ASSERT(msg->txtim > 0, "bad txtim");
  currtim = chutil::Time::from_epoch(msg->txtim);
  ASSERT(currtim.is_valid(), "bad time 1");
  processTimeOuts();
  processAlarmClock();
}

void act::Timer::timeout_sub_handler(const msg::TimeOutSub* msg) noexcept
{
  timer_list.push_back(*msg);
}

// #define DEBUGtimer


void act::Timer::alarm_sub_handler(const msg::AlarmClockSub* msg) noexcept
{
  ASSERT(msg->sender, "need to be able to reply to alarm sub");
#ifdef DEBUGtimer
  std::cerr << "Timer: Received AlarmClockSub timer_id=" << msg->timer_id << std::endl;
#endif
  if (currtim.is_valid())
  {
    const_cast<msg::AlarmClockSub *>(msg)->adjustToCurrentTime(currtim);
  }
  alarmList.push_back(*msg);
#ifdef DEBUGtimer
  std::cerr << "Timer: AlarmList size=" << alarmList.size() << std::endl;
#endif
}

msg::Alarm *
act::Timer::createDatMsg(msg::Alarm::reply_reason_t reply_reason, int timer_id)
{
  ASSERT(currtim.is_valid(), "bad time 2");
  auto m = new msg::Alarm(reply_reason);
  m->timer_id = timer_id;
  m->currtim = currtim;
  return m;
}

void act::Timer::processAlarmClock()
{
  auto i = alarmList.begin();
  while (i != alarmList.end())
  {
    msg::AlarmClockSub &m = (*i);
    m.adjustToCurrentTime(currtim);

    if (m.tim_to_wake_up <= currtim)
    {
#ifdef DEBUGtimer
      std::cerr << "Timer: ALARM FIRING timer_id=" << m.timer_id
                << " currtim=" << currtim.to_string() << std::endl;
#endif
      auto r = new msg::Alarm(msg::Alarm::ALARMCLOCK);
      r->timer_id = m.timer_id;
      r->currtim = currtim;
      r->sym = m.sym;
      m.sender->send(r, this);
      if (i->periodic)
      {
        i->resetEvents();
        i++;
      }
      else
        i = alarmList.erase(i);
    }
    else
      i++;
  }
}

void act::Timer::processTimeOuts(int volume)
{
  auto i = timer_list.begin();
  while (i != timer_list.end())
  {
    i->num_events -= volume; // an event is one unit in volume
    if (i->num_events <= 0)
    {
      while (i->num_events <= 0)
      {
        auto dat =
            createDatMsg(msg::Alarm::TIME_OUT,
                         i->timer_id);
        i->sender->send(dat, this);
        i->resetEvents();
      }
      if (i->periodic)
        i++;
      else
        i = timer_list.erase(i);
    }
    else
      i++;
  }
}

frame::mtim::act::Timer *frame::mtim::act::Timer::the_timer = 0;
