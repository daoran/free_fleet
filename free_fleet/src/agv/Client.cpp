/*
 * Copyright (C) 2002 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <chrono>
#include <unordered_set>

#include <rmf_traffic/Time.hpp>

#include <free_fleet/agv/Client.hpp>

namespace free_fleet {
namespace agv {

//==============================================================================
class Client::Implementation
{
public:

  Implementation()
  {}

  ~Implementation() = default;

  bool _connected() const
  {
    return _command_handle && _status_handle && _middleware;
  }

  template<class T> 
  bool _is_valid_request(const std::shared_ptr<T>& request)
  {
    if (request->robot_name == _robot_name &&
      _task_ids.find(request->task_id) == _task_ids.end())
      return true;
    return false;
  }

  std::string _robot_name;
  std::string _robot_model;

  std::string _task_id;
  std::string _paused_task_id;
  std::unordered_set<std::string> _task_ids;

  std::shared_ptr<CommandHandle> _command_handle;
  std::shared_ptr<StatusHandle> _status_handle;
  std::shared_ptr<transport::Middleware> _middleware;
};

//==============================================================================
Client::SharedPtr Client::make(
  const std::string& robot_name,
  const std::string& robot_model,
  std::shared_ptr<CommandHandle> command_handle,
  std::shared_ptr<StatusHandle> status_handle,
  std::shared_ptr<transport::Middleware> middleware)
{
  if (!command_handle || !status_handle || !middleware)
    return nullptr;

  Client::SharedPtr new_client(new Client);
  new_client->_pimpl->_robot_name = robot_name;
  new_client->_pimpl->_robot_model = robot_model;
  new_client->_pimpl->_command_handle = std::move(command_handle);
  new_client->_pimpl->_status_handle = std::move(status_handle);
  new_client->_pimpl->_middleware = std::move(middleware);

  return new_client;
}

//==============================================================================
Client::Client()
: _pimpl(rmf_utils::make_impl<Implementation>(Implementation()))
{}

//==============================================================================
void Client::start(uint32_t frequency)
{
  double seconds_per_iteration = 1.0 / frequency;
  rmf_traffic::Duration duration_per_iteration =
    rmf_traffic::time::from_seconds(seconds_per_iteration);
  rmf_traffic::Time t_prev = std::chrono::steady_clock::now();

  while (_pimpl->_connected())
  {
    if (std::chrono::steady_clock::now() - t_prev < duration_per_iteration)
      continue;

    // send state
    free_fleet::messages::RobotState new_state {
      _pimpl->_robot_name,
      _pimpl->_robot_model,
      _pimpl->_task_id,
      _pimpl->_status_handle->mode(),
      _pimpl->_status_handle->battery_percent(),
      _pimpl->_status_handle->location(),
      _pimpl->_status_handle->path()
    };
    _pimpl->_middleware->send_state(new_state);

    // read mode request
    auto mode_request = _pimpl->_middleware->read_mode_request();
    if (mode_request && _pimpl->_is_valid_request(mode_request))
    {
      if (mode_request->mode.mode == messages::RobotMode::MODE_PAUSED)
      {
        _pimpl->_task_ids.insert(mode_request->task_id);
        _pimpl->_paused_task_id = _pimpl->_task_id;
        _pimpl->_task_id = mode_request->task_id;
        _pimpl->_command_handle->stop();
      }
      else if (mode_request->mode.mode == messages::RobotMode::MODE_MOVING)
      {
        _pimpl->_task_ids.insert(mode_request->task_id);
        _pimpl->_task_id = _pimpl->_paused_task_id;
        _pimpl->_paused_task_id = "";
        _pimpl->_command_handle->resume();
      }
      continue;
    }
    
    // read navigation request
    auto navigation_request = _pimpl->_middleware->read_navigation_request();
    if (navigation_request && _pimpl->_is_valid_request(navigation_request))
    {
      _pimpl->_task_ids.insert(navigation_request->task_id);
      _pimpl->_task_id = navigation_request->task_id;
      free_fleet::agv::CommandHandle::RequestCompleted callback =
        [this]() { _pimpl->_task_id = ""; };
      _pimpl->_command_handle->follow_new_path(
        navigation_request->path,
        callback);
    }
  }
}

//==============================================================================

} // namespace agv
} // namespace free_fleet
