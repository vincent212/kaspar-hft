
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "light/act/light22.hpp"
#include "light/if/light22.hpp"

cfsmp create_light22_Shadow_BUY(
    const std::string &prefix,
    cfsmp db,
    cfsmp rm,
    const std::string &_name,
    en::trader _owner,
    std::string _sym,
    en::x _mdvenue,
    en::x _tradingvenue,
    light::QCoord *_qcoord,
    light::PCoord *_pcoord,
    cfsmp _ob,
    cfsmp _super,
    int tier,
    cfsmp _timer,
    cfsmp _som,
    int _mmid,
    const boost::property_tree::ptree &_pt,
    uint8_t VG,
    bool dry_run)
{
    return new light::act::light22<en::bs::BUY>(
        prefix,
        db,
        rm,
        _name,
        _owner,
        _sym,
        _mdvenue,
        _tradingvenue,
        _qcoord,
        _pcoord,
        _ob,
        _super,
        tier,
        _timer,
        _som,
        _mmid,
        _pt,
        VG,
        dry_run);
}

cfsmp create_light22_Shadow_SEL(
    const std::string &prefix,
    cfsmp db,
    cfsmp rm,
    const std::string &_name,
    en::trader _owner,
    std::string _sym,
    en::x _mdvenue,
    en::x _tradingvenue,
    light::QCoord *_qcoord,
    light::PCoord *_pcoord,
    cfsmp _ob,
    cfsmp _super,
    int tier,
    cfsmp _timer,
    cfsmp _som,
    int _mmid,
    const boost::property_tree::ptree &_pt,
    uint8_t VG,
    bool dry_run)
{
    return new light::act::light22<en::bs::SEL>(
        prefix,
        db,
        rm,
        _name,
        _owner,
        _sym,
        _mdvenue,
        _tradingvenue,
        _qcoord,
        _pcoord,
        _ob,
        _super,
        tier,
        _timer,
        _som,
        _mmid,
        _pt,
        VG,
        dry_run);
}

cfsmp create_light22(
    const std::string &prefix,
    cfsmp db,
    cfsmp rm,
    const std::string &_name,
    en::bs _side,
    en::trader _owner,
    std::string _sym,
    en::x _mdvenue,
    en::x _tradingvenue,
    light::QCoord *_qcoord,
    light::PCoord *_pcoord,
    cfsmp _ob,
    cfsmp _super,
    int tier,
    cfsmp _timer,
    cfsmp _som,
    int _mmid,
    const boost::property_tree::ptree &_pt,
    uint8_t VG,
    bool dry_run)
{
    if (_side == en::bs::BUY)
    {
        return create_light22_Shadow_BUY(
            prefix, db, rm, _name, _owner, _sym,
            _mdvenue, _tradingvenue, _qcoord, _pcoord, _ob, _super,
            tier, _timer, _som, _mmid,
            _pt, VG, dry_run);
    }
    else
    {
        return create_light22_Shadow_SEL(
            prefix, db, rm, _name, _owner, _sym,
            _mdvenue, _tradingvenue, _qcoord, _pcoord, _ob, _super,
            tier, _timer, _som, _mmid,
            _pt, VG, dry_run);
    }
}
