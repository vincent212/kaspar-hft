#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <boost/property_tree/ptree.hpp>
#include "actors/Actor.hpp"
#include "light/qcoord.hpp"

actor_ptr create_light22_Shadow_BUY(
    const std::string& prefix,
    actor_ptr db,
    actor_ptr rm,
    const std::string &_name,
    en::trader _owner,
    std::string _sym,
    en::x _mdvenue,
    en::x _tradingvenue,
    light::QCoord *_qcoord,
    light::PCoord *_pcoord,
    actor_ptr _ob,
    actor_ptr _super,
    int tier,
    actor_ptr _timer,
    actor_ptr _som,
    int _mmid,
    const boost::property_tree::ptree &_pt,
    uint8_t VG = 0,
    bool dry_run = false);

actor_ptr create_light22_Shadow_SEL(
    const std::string& prefix,
    actor_ptr db,
    actor_ptr rm,
    const std::string &_name,
    en::trader _owner,
    std::string _sym,
    en::x _mdvenue,
    en::x _tradingvenue,
    light::QCoord *_qcoord,
    light::PCoord *_pcoord,
    actor_ptr _ob,
    actor_ptr _super,
    int tier,
    actor_ptr _timer,
    actor_ptr _som,
    int _mmid,
    const boost::property_tree::ptree &_pt,
    uint8_t VG = 0,
    bool dry_run = false);

actor_ptr create_light22(
    const std::string& prefix,
    actor_ptr db,
    actor_ptr rm,
    const std::string &_name,
    en::bs _side,
    en::trader _owner,
    std::string _sym,
    en::x _mdvenue,
    en::x _tradingvenue,
    light::QCoord *_qcoord,
    light::PCoord *_pcoord,
    actor_ptr _ob,
    actor_ptr _super,
    int tier,
    actor_ptr _timer,
    actor_ptr _som,
    int _mmid,
    const boost::property_tree::ptree &_pt,
    uint8_t VG = 0,
    bool dry_run = false);
