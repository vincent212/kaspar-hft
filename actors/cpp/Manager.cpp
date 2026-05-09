/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <list>
#include <map>
#include <string>
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "actors/Actor.hpp"
#include "actors/act/Group.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/act/Manager.hpp"
#include "actors/registry/RegistryClient.hpp"
#include "actors/remote/ZmqSender.hpp"

#ifdef __linux__
#include <sched.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#endif

using namespace actors;
using namespace std;

static int set_thread_affinity([[maybe_unused]] set<int> core_ids, [[maybe_unused]] pthread_t thread)
{
  if (core_ids.empty())
    return 0;

#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  for (auto core_id : core_ids)
  {
    if (core_id < 0 || core_id >= num_cores)
    {
      cerr << "bad core id: " << core_id << endl;
      return EINVAL;
    }
    CPU_SET(core_id, &cpuset);
  }

  auto rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  return rc;
#elif defined(__APPLE__)
  // macOS uses thread affinity tags (hints), not hard binding
  // This is a best-effort approach
  if (!core_ids.empty()) {
    thread_affinity_policy_data_t policy = { static_cast<integer_t>(*core_ids.begin()) };
    thread_policy_set(pthread_mach_thread_np(thread),
                      THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&policy, 1);
  }
  return 0;
#else
  return 0;
#endif
}

Manager::Manager(const std::string& manager_name) {
  // If a manager name is provided, use it instead of the mangled typeid name
  if (!manager_name.empty()) {
    strncpy(name, manager_name.c_str(), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  }
  // Otherwise, Actor's constructor already set name to typeid(*this).name()
  // We could optionally set a default like "Manager" here if desired
}

Manager::~Manager()
{
  // Stop registry heartbeat if running
  if (registry_client_) {
    registry_client_->stop_heartbeat_thread();
  }

  // Send terminate to all actors if not already done
  if (!terminate_called) {
    for (auto actor : actor_list) {
      actor->terminate();
    }
  }

  // Join all threads with timeout - if they don't join in 2 seconds, detach them
  for (auto p : thread_list) {
    if (p->joinable()) {
      // Try to join with a timeout using a detach fallback
      // C++ std::thread doesn't have timed join, so we use a simple approach:
      // - Send terminate (already done above)
      // - Try to join (this will block)
      // - If it hangs, the program will just exit anyway
      p->join();
    }
    delete p;
  }
}

void Manager::set_registry(const string& registry_endpoint,
                           const string& local_endpoint,
                           shared_ptr<ZmqSender> zmq_sender)
{
  zmq_sender_ = zmq_sender;
  local_endpoint_ = local_endpoint;

  // Create registry client using the new binary protocol
  registry_client_ = make_unique<registry::RegistryClient>(registry_endpoint);
  registry_client_->connect();

  // Register this manager with no actors initially (actors registered as they're managed)
  std::vector<std::string> initial_actors;
  registry_client_->register_manager(get_name(), local_endpoint_, initial_actors);

  // Start heartbeat thread
  registry_client_->start_heartbeat_thread(get_name());
}

void Manager::init()
{
  for (auto actor : actor_list)
  {
    auto initmsg = new actors::msg::Start();
    cout << "Manager::init sending start to " << actor->get_name() << endl;
    actor->fast_send(initmsg, nullptr);
  }

  for (auto actor : actor_list)
  {
    auto t = new std::thread([actor]() { (*actor)(); });
    thread_list.push_back(t);

    if (!actor->affinity.empty())
    {
      cout << actor->get_name() << " setting affinity" << endl;
      if (set_thread_affinity(actor->affinity, t->native_handle()) != 0)
      {
        perror("could not assign affinity\n");
      }
    }

    if (actor->priority > 0)
    {
      cout << actor->get_name() << " setting priority to SCHED_FIFO " << actor->priority << endl;
      struct sched_param sp;
      sp.sched_priority = actor->priority;
      if (pthread_setschedparam(t->native_handle(), SCHED_FIFO, &sp) != 0)
      {
        perror("sched_setscheduler");
        cerr << "could not set priority for " << actor->get_name() << endl;
      }
      else
        cout << " priority set ok\n";
    }
    else
    {
      cout << actor->get_name() << " NOT setting priority " << actor->priority << endl;
    }
  }

  // NOTE: Don't send Start to Manager itself - Manager doesn't need it
  // and it causes issues when Manager is used standalone (not managed by another Manager)
  // this->send(new msg::Start());
}

void Manager::end()
{
  // If terminate wasn't called, set it now
  if (!terminate_called) {
    terminate_called = true;
  }

  // Send shutdown to all actors so they exit their run loops
  for (auto actor : actor_list) {
    actor->send(new msg::Shutdown());
  }

  // Join all threads
  for (auto t : thread_list) {
    if (t->joinable()) {
      t->join();
    }
  }
}

void Manager::process_message(const Message *m)
{
  if (typeid(*m) == typeid(actors::msg::Start))
  {
    // Manager started
  }
  else if (typeid(*m) == typeid(actors::msg::Shutdown))
  {
    // Send shutdown to all actors so they exit their run loops
    for (auto actor : actor_list)
    {
      actor->fast_terminate();
    }
  }
}

void Manager::add_to_manage_q(actor_ptr actor, set<int> affinity, int priority, int priority_type)
{
  assert(actor != nullptr && "cannot manage null actor");

  if (actor->is_managed || managed_name_map.find(actor->get_name()) != managed_name_map.end())
  {
    cout << "actors already managed:\n";
    for (const auto &p : managed_name_map)
    {
      cout << p.first << endl;
    }
    assert(false && "actor with this name already managed");
  }

  if (expanded_name_map.find(actor->get_name()) != expanded_name_map.end())
  {
    assert(false && "actor cannot be managed because it's part of a group that was already managed");
  }

  // Check affinity
#ifdef __linux__
  for (auto core_id : affinity)
  {
    if (core_id < 0 || core_id >= sysconf(_SC_NPROCESSORS_ONLN))
    {
      cerr << "bad core id: " << core_id << endl;
      assert(false && "core id out of range");
    }
  }
#elif defined(__APPLE__)
  // macOS: skip strict core validation, affinity is advisory
  (void)affinity;
#endif

  managed_name_map[actor->get_name()] = actor;
  expanded_name_map[actor->get_name()] = actor;

  actor->set_manager(this);
  cout << "set_manager(" << actor->get_name() << ") actor=" << (void*)actor << " mgr=" << (void*)this << ", get=" << (void*)actor->get_manager() << endl;
  if (actor->is_group())
  {
    Group *g = static_cast<Group *>(actor);
    for (auto a : g->members)
      a->set_manager(this);
  }

  actor_list.push_back(actor);

  if (actor->is_group())
  {
    auto g = static_cast<Group *>(actor);
    assert(!g->name_to_actor.empty() && "add actors to group before managing group");

    for (auto it = g->name_to_actor.begin(); it != g->name_to_actor.end(); ++it)
    {
      auto it2 = expanded_name_map.find(it->first);
      assert(it2 == expanded_name_map.end() && "actor (part of a group) already managed somewhere else");
      expanded_name_map[it->first] = it->second;
    }
  }

  actor->is_managed = true;
  actor->affinity = affinity;
  actor->priority = priority;
  actor->priority_type = priority_type;

  // Auto-register with GlobalRegistry if connected
  if (registry_client_ && !local_endpoint_.empty()) {
    // Collect all current actor names and update registration
    std::vector<std::string> actor_names;
    for (const auto& a : actor_list) {
      if (a->is_group()) {
        // do not register group members individually - they are accessed via the group name (qualified)
        // Group* g = static_cast<Group*>(a);
        // for (const auto& member : g->members) {
        //   actor_names.push_back(member->get_name());
        // }
      } else {
        actor_names.push_back(a->get_name());
      }
    }

    bool success = registry_client_->update_actors(get_name(), local_endpoint_, actor_names);
    if (!success) {
      throw std::runtime_error("Manager: Failed to update GlobalRegistry for " + std::string(get_name()));
    }
    cout << "Manager: Updated GlobalRegistry with " << actor_names.size() << " actors" << endl;
  }
}

map<string, size_t> Manager::get_queue_lengths() const noexcept
{
  map<string, size_t> ret;
  for (auto &[name, actor] : managed_name_map)
  {
    ret[name] = actor->queue_length();
  }
  return ret;
}

map<string, tuple<pid_t, int>> Manager::get_message_counts() const noexcept
{
  map<string, tuple<pid_t, int>> ret;
  for (auto &[name, actor] : managed_name_map)
    ret[name] = make_tuple(actor->tid, int(actor->msg_cnt));
  return ret;
}

list<string> Manager::get_managed_names() const noexcept
{
  list<string> ret;
  for (auto &[name, _] : expanded_name_map)
    ret.push_back(name);
  return ret;
}

actor_ptr Manager::get_local_actor(const string &name) const noexcept
{
  // Handle qualified names like "sim_manager.Timer" or "strategy_group.BacktestPositionManager"
  string local_name = name;
  string qualified_prefix = string(get_name()) + ".";

  if (name.find(".") != string::npos) {
    // It's a qualified name - check if it's for us (Manager) or a managed Group
    if (name.find(qualified_prefix) == 0) {
      // Strip our prefix
      local_name = name.substr(qualified_prefix.length());
    } else {
      // Check if it's qualified for a managed Group (e.g., "strategy_group.BacktestPositionManager")
      size_t dot_pos = name.find(".");
      string group_name = name.substr(0, dot_pos);
      string actor_in_group = name.substr(dot_pos + 1);

      // Search for the Group by name
      for (auto actor : actor_list) {
        if (actor->is_group() && actor->get_name() == group_name) {
          Group *g = static_cast<Group *>(actor);
          for (auto a : g->members) {
            if (a->get_name() == actor_in_group) {
              return a;
            }
          }
          // Group found but actor not in it
          return nullptr;
        }
      }
      // Not our manager prefix and not a managed Group - different manager
      return nullptr;
    }
  }

  // Search for the actor by local name
  for (auto actor : actor_list)
  {
    if (actor->get_name() == local_name)
      return actor;
    else if (actor->is_group())
    {
      Group *g = static_cast<Group *>(actor);
      for (auto a : g->members)
      {
        if (a->get_name() == local_name)
          return a;
      }
    }
  }
  return nullptr;
}

ActorRef Manager::get_actor_by_name(const string &name)
{
  // First check local actors
  if (auto* local = get_local_actor(name)) {
    return ActorRef(local);
  }

  // If not found locally, try GlobalRegistry
  if (registry_client_ && zmq_sender_) {
    auto result = registry_client_->lookup_actor(name);
    if (result.found) {
      return zmq_sender_->remote_ref(name, result.endpoint);
    }
    if (result.ambiguous) {
      throw std::runtime_error("Actor '" + name + "' is ambiguous. Use qualified name like 'manager.actor'");
    }
    throw std::runtime_error("Actor '" + name + "' not found in registry");
  }

  // No registry connected and not found locally
  throw std::runtime_error("Actor '" + name + "' not found locally and no registry configured");
}

size_t Manager::total_queue_length()
{
  size_t total = 0;
  for (auto actor : actor_list)
  {
    total += actor->queue_length();
  }
  return total;
}

void Manager::manage_and_start(actor_ptr actor, set<int> affinity, int priority, int priority_type)
{
  // First, add to manage queue (adds to lists, sets manager, etc.)
  add_to_manage_q(actor, affinity, priority, priority_type);

  // Then immediately start it
  auto initmsg = new actors::msg::Start();
  cout << "Manager::manage_and_start sending start to " << actor->get_name() << endl;
  actor->fast_send(initmsg, nullptr);

  // Launch its thread
  auto t = new std::thread([actor]() { (*actor)(); });
  thread_list.push_back(t);

  // Set affinity if specified
  if (!actor->affinity.empty())
  {
    cout << actor->get_name() << " setting affinity" << endl;
    if (set_thread_affinity(actor->affinity, t->native_handle()) != 0)
    {
      perror("could not assign affinity\n");
    }
  }

  // Set priority if specified
  if (actor->priority > 0)
  {
    cout << actor->get_name() << " setting priority to SCHED_FIFO " << actor->priority << endl;
    struct sched_param sp;
    sp.sched_priority = actor->priority;
    if (pthread_setschedparam(t->native_handle(), SCHED_FIFO, &sp) != 0)
    {
      perror("sched_setscheduler");
      cerr << "could not set priority for " << actor->get_name() << endl;
    }
    else
      cout << " priority set ok\n";
  }
  else
  {
    cout << actor->get_name() << " NOT setting priority " << actor->priority << endl;
  }
}

void Manager::unmanage(actor_ptr actor)
{
  cout << "Manager::unmanage " << actor->get_name() << endl;

  // Send Shutdown message
  actor->send(new msg::Shutdown());

  // Find and join the actor's thread
  for (auto it = thread_list.begin(); it != thread_list.end(); ++it)
  {
    // We need to identify which thread belongs to this actor
    // Since we don't store a mapping, we'll join all and remove after
    // For now, just join the thread - this assumes unmanage is called in order
    if ((*it)->joinable())
    {
      (*it)->join();
      delete *it;
      thread_list.erase(it);
      break;  // Found and joined the thread
    }
  }

  // Remove from actor_list
  actor_list.remove(actor);

  // Remove from name maps
  managed_name_map.erase(actor->get_name());
  expanded_name_map.erase(actor->get_name());

  // Delete the actor
  delete actor;

  cout << "Manager::unmanage complete for " << actor->get_name() << endl;
}
